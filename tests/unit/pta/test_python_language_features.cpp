#include "analysis/python/language_features.h"
#include "test_support.h"

namespace {

using cpygraph::bytecode::PythonConstantKind;
using cpygraph::bytecode::PythonProtocolOperation;
using cpygraph::bytecode::PythonSpecialMethod;
using cpygraph::bytecode::SemanticInstruction;
using cpygraph::bytecode::SemanticOpcode;
using cpygraph::bytecode::pythonMethod;
using cpygraph::python::ReceiverProtocolFacts;

constexpr auto kBoolMethod = PythonSpecialMethod::Bool;
constexpr auto kLenMethod = PythonSpecialMethod::Len;
constexpr auto kAddMethod = PythonSpecialMethod::Add;
constexpr auto kReflectedAddMethod = PythonSpecialMethod::ReflectedAdd;
constexpr auto kLessMethod = PythonSpecialMethod::Less;
constexpr auto kGreaterMethod = PythonSpecialMethod::Greater;
constexpr auto kGetAttributeMethod = PythonSpecialMethod::GetAttribute;
constexpr auto kGetAttrMethod = PythonSpecialMethod::GetAttr;
constexpr auto kAsyncEnterMethod = PythonSpecialMethod::AsyncEnter;
constexpr auto kAsyncExitMethod = PythonSpecialMethod::AsyncExit;

bool contains(cpygraph::bytecode::PythonMethodSet methods,
              PythonSpecialMethod method) {
    return cpygraph::bytecode::containsPythonMethod(methods, method);
}

SemanticInstruction instruction(SemanticOpcode opcode) {
    SemanticInstruction result;
    result.opcode = opcode;
    return result;
}

}  // namespace

int main() {
    const auto truth_candidates =
        pythonMethod(kBoolMethod) | pythonMethod(kLenMethod);
    ReceiverProtocolFacts truth_receiver;
    truth_receiver.available_methods = truth_candidates;
    truth_receiver.non_deferred_methods = truth_candidates;
    const auto truth = cpygraph::python::selectProtocolMethods(
        PythonProtocolOperation::Truth, truth_candidates, truth_receiver);
    require(contains(truth.primary_methods, kBoolMethod) &&
                !contains(truth.primary_methods, kLenMethod),
            "__bool__ suppresses the __len__ truthiness fallback");

    ReceiverProtocolFacts length_receiver;
    length_receiver.available_methods = pythonMethod(kLenMethod);
    length_receiver.non_deferred_methods = pythonMethod(kLenMethod);
    const auto length = cpygraph::python::selectProtocolMethods(
        PythonProtocolOperation::Truth, truth_candidates, length_receiver);
    require(contains(length.primary_methods, kLenMethod) &&
                !contains(length.primary_methods, kBoolMethod),
            "__len__ remains reachable when __bool__ is absent");

    const auto add_candidates =
        pythonMethod(kAddMethod) | pythonMethod(kReflectedAddMethod);
    ReceiverProtocolFacts left_without_add;
    ReceiverProtocolFacts right_with_reflected;
    right_with_reflected.available_methods =
        pythonMethod(kReflectedAddMethod) | pythonMethod(kAddMethod);
    right_with_reflected.non_deferred_methods =
        right_with_reflected.available_methods;
    const auto reflected = cpygraph::python::selectProtocolMethods(
        PythonProtocolOperation::Add, add_candidates, left_without_add,
        right_with_reflected);
    require(contains(reflected.secondary_methods, kReflectedAddMethod) &&
                !contains(reflected.secondary_methods, kAddMethod),
            "binary fallback uses the reflected method on the right operand");

    const auto comparison_candidates =
        pythonMethod(kLessMethod) | pythonMethod(kGreaterMethod);
    ReceiverProtocolFacts direct_comparison;
    direct_comparison.available_methods = pythonMethod(kLessMethod);
    direct_comparison.non_deferred_methods = pythonMethod(kLessMethod);
    ReceiverProtocolFacts reflected_comparison;
    reflected_comparison.available_methods = pythonMethod(kGreaterMethod);
    reflected_comparison.non_deferred_methods = pythonMethod(kGreaterMethod);
    const auto comparison = cpygraph::python::selectProtocolMethods(
        PythonProtocolOperation::Comparison, comparison_candidates,
        direct_comparison, reflected_comparison);
    require(contains(comparison.primary_methods, kLessMethod) &&
                !contains(comparison.secondary_methods, kGreaterMethod),
            "a non-deferred comparison suppresses reflected dispatch");
    direct_comparison.non_deferred_methods = {};
    const auto deferred_comparison =
        cpygraph::python::selectProtocolMethods(
            PythonProtocolOperation::Comparison, comparison_candidates,
            direct_comparison, reflected_comparison);
    require(contains(deferred_comparison.secondary_methods, kGreaterMethod),
            "a possibly deferred comparison preserves reflected dispatch");
    ReceiverProtocolFacts right_equality;
    right_equality.available_methods =
        pythonMethod(PythonSpecialMethod::Equal);
    right_equality.non_deferred_methods = right_equality.available_methods;
    ReceiverProtocolFacts deferred_equality = right_equality;
    deferred_equality.non_deferred_methods = {};
    const auto equality = cpygraph::python::selectProtocolMethods(
        PythonProtocolOperation::Comparison,
        pythonMethod(PythonSpecialMethod::Equal), deferred_equality,
        right_equality);
    require(contains(equality.primary_methods, PythonSpecialMethod::Equal) &&
                contains(equality.secondary_methods,
                         PythonSpecialMethod::Equal),
            "NotImplemented preserves right-side equality dispatch");

    const auto attribute_candidates =
        pythonMethod(kGetAttributeMethod) | pythonMethod(kGetAttrMethod);
    ReceiverProtocolFacts attribute_receiver;
    attribute_receiver.available_methods = attribute_candidates;
    const auto existing = cpygraph::python::selectAttributeLoadMethods(
        attribute_candidates, attribute_receiver, true, false);
    require(contains(existing.primary_methods, kGetAttributeMethod) &&
                !contains(existing.primary_methods, kGetAttrMethod),
            "an existing member suppresses __getattr__");
    const auto missing = cpygraph::python::selectAttributeLoadMethods(
        attribute_candidates, attribute_receiver, false, false);
    require(contains(missing.primary_methods, kGetAttrMethod),
            "a missing member preserves __getattr__");

    ReceiverProtocolFacts async_context_receiver;
    async_context_receiver.available_methods =
        pythonMethod(kAsyncEnterMethod) | pythonMethod(kAsyncExitMethod);
    const auto async_context = cpygraph::python::selectProtocolMethods(
        PythonProtocolOperation::DynamicProtocol,
        async_context_receiver.available_methods, async_context_receiver);
    require(contains(async_context.primary_methods, kAsyncEnterMethod) &&
                contains(async_context.primary_methods, kAsyncExitMethod),
            "compound protocols preserve independent enter and exit hooks");

    auto constant = instruction(SemanticOpcode::LoadConst);
    constant.constant_kind = PythonConstantKind::Boolean;
    const auto return_instruction = instruction(SemanticOpcode::Return);
    require(!cpygraph::python::mayReturnNotImplemented(
                {constant, return_instruction}),
            "a concrete non-sentinel return suppresses reflected fallback");
    constant.constant_kind = PythonConstantKind::NotImplemented;
    require(cpygraph::python::mayReturnNotImplemented(
                {constant, return_instruction}),
            "NotImplemented preserves reflected fallback");
    require(cpygraph::python::hasIntrinsicProtocolResult(
                SemanticOpcode::ImportModule),
            "import dispatch preserves its resolved module result");
}
