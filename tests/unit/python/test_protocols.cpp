#include "api/cfg.h"
#include "api/cg.h"
#include "api/pta.h"
#include "api/python.h"
#include "bytecode/adapters/binary_operations.h"
#include "bytecode/adapters/comparison_operations.h"
#include "bytecode/adapters/python310/adapter.h"
#include "bytecode/adapters/python310/opcodes.h"
#include "bytecode/adapters/python311/adapter.h"
#include "bytecode/adapters/python311/opcodes.h"
#include "bytecode/adapters/python312/adapter.h"
#include "bytecode/adapters/python312/opcodes.h"
#include "bytecode/adapters/python313/adapter.h"
#include "bytecode/adapters/python313/opcodes.h"
#include "bytecode/adapters/python314/adapter.h"
#include "bytecode/adapters/python314/opcodes.h"
#include "test_support.h"

namespace {

cpygraph::bytecode::SemanticInstruction instruction(
    cpygraph::bytecode::SemanticOpcode opcode) {
    cpygraph::bytecode::SemanticInstruction result;
    result.opcode = opcode;
    return result;
}

void requireFamily(const cpygraph::bytecode::CPythonAdapter& adapter,
                   std::uint8_t opcode,
                   const cpygraph::bytecode::CodeMetadata& metadata,
                   cpygraph::python::ProtocolFamily expected,
                   const char* message) {
    const auto lifted = adapter.lift(
        cpygraph::bytecode::RawInstruction{0U, opcode, 0U}, metadata);
    require(cpygraph::python::protocolFamily(lifted) == expected, message);
}

}  // namespace

int main() {
    using cpygraph::bytecode::SemanticOpcode;
    using cpygraph::python::ProtocolFamily;
    using cpygraph::python::protocolFamily;

    require(protocolFamily(instruction(SemanticOpcode::ImportModule)) ==
                ProtocolFamily::Import &&
                protocolFamily(instruction(SemanticOpcode::ImportAttribute)) ==
                    ProtocolFamily::Import,
            "module loading and from-import attribute lookup retain implicit dispatch");
    require(protocolFamily(instruction(SemanticOpcode::LoadAttribute)) ==
                ProtocolFamily::AttributeAccess &&
                protocolFamily(instruction(SemanticOpcode::StoreAttribute)) ==
                    ProtocolFamily::AttributeAccess,
            "descriptor-capable attribute operations retain implicit dispatch");
    require(protocolFamily(instruction(SemanticOpcode::LoadElement)) ==
                ProtocolFamily::ItemAccess &&
                protocolFamily(instruction(SemanticOpcode::StoreElement)) ==
                    ProtocolFamily::ItemAccess,
            "subscription operations retain Python item protocol dispatch");
    require(protocolFamily(instruction(SemanticOpcode::ConditionalBranch)) ==
                ProtocolFamily::Truthiness,
            "conditional bytecode retains __bool__ and __len__ dispatch");
    require(protocolFamily(instruction(SemanticOpcode::Generic)) ==
                ProtocolFamily::DynamicOperation,
            "normalized operators, iteration, class, context, async, and pattern operations retain dispatch");
    require(protocolFamily(instruction(SemanticOpcode::Call)) ==
                ProtocolFamily::None,
            "explicit calls are handled by ordinary PTA/CG construction without a duplicate implicit edge");
    require(!cpygraph::python::mayInvokeUserCode(
                instruction(SemanticOpcode::LoadConst)),
            "non-dispatching constant loads do not create false protocol calls");

    cpygraph::bytecode::CodeMetadata metadata;
    metadata.names = {"feature"};
    const cpygraph::bytecode::Python310Adapter py310;
    const cpygraph::bytecode::Python311Adapter py311;
    const cpygraph::bytecode::Python312Adapter py312;
    const cpygraph::bytecode::Python313Adapter py313;
    const cpygraph::bytecode::Python314Adapter py314;
    requireFamily(py310, CPYGRAPH_PY310_IMPORT_NAME, metadata,
                  ProtocolFamily::Import,
                  "Python 3.10 import bytecode retains protocol classification");
    requireFamily(py311, CPYGRAPH_PY311_IMPORT_NAME, metadata,
                  ProtocolFamily::Import,
                  "Python 3.11 import bytecode retains protocol classification");
    requireFamily(py312, CPYGRAPH_PY312_IMPORT_NAME, metadata,
                  ProtocolFamily::Import,
                  "Python 3.12 import bytecode retains protocol classification");
    requireFamily(py313, CPYGRAPH_PY313_IMPORT_NAME, metadata,
                  ProtocolFamily::Import,
                  "Python 3.13 import bytecode retains protocol classification");
    requireFamily(py314, CPYGRAPH_PY314_IMPORT_NAME, metadata,
                  ProtocolFamily::Import,
                  "Python 3.14 import bytecode retains protocol classification");
    requireFamily(py310, CPYGRAPH_PY310_SETUP_WITH, metadata,
                  ProtocolFamily::DynamicOperation,
                  "Python 3.10 context cleanup retains dynamic dispatch");
    requireFamily(py311, CPYGRAPH_PY311_BEFORE_WITH, metadata,
                  ProtocolFamily::DynamicOperation,
                  "Python 3.11 context setup retains dynamic dispatch");
    requireFamily(py312, CPYGRAPH_PY312_BEFORE_WITH, metadata,
                  ProtocolFamily::DynamicOperation,
                  "Python 3.12 context setup retains dynamic dispatch");
    requireFamily(py313, CPYGRAPH_PY313_BEFORE_WITH, metadata,
                  ProtocolFamily::DynamicOperation,
                  "Python 3.13 context setup retains dynamic dispatch");
    requireFamily(py314, CPYGRAPH_PY314_LOAD_SPECIAL, metadata,
                  ProtocolFamily::AttributeAccess,
                  "Python 3.14 special-method loading retains receiver-aware dispatch");

    const auto require_add_protocol = [](const auto& adapter,
                                         std::uint8_t opcode) {
        const auto add = adapter.lift(cpygraph::bytecode::RawInstruction{
            0U, opcode, CPYGRAPH_BINARY_OP_ADD},
            cpygraph::bytecode::CodeMetadata{});
        require(add.protocol_operation ==
                    cpygraph::bytecode::PythonProtocolOperation::Add &&
                    cpygraph::bytecode::containsPythonMethod(
                        add.protocol_methods,
                        cpygraph::bytecode::PythonSpecialMethod::Add) &&
                    cpygraph::bytecode::containsPythonMethod(
                        add.protocol_methods,
                        cpygraph::bytecode::PythonSpecialMethod::ReflectedAdd),
                "binary addition retains __add__ and __radd__ identities");
    };
    require_add_protocol(py310, CPYGRAPH_PY310_BINARY_ADD);
    require_add_protocol(py311, CPYGRAPH_PY311_BINARY_OP);
    require_add_protocol(py312, CPYGRAPH_PY312_BINARY_OP);
    require_add_protocol(py313, CPYGRAPH_PY313_BINARY_OP);
    require_add_protocol(py314, CPYGRAPH_PY314_BINARY_OP);

    const auto require_protocol = [](const auto& adapter,
                                     std::uint8_t opcode,
                                     cpygraph::bytecode::PythonProtocolOperation operation,
                                     cpygraph::bytecode::PythonSpecialMethod method) {
        const auto lifted = adapter.lift(
            cpygraph::bytecode::RawInstruction{0U, opcode, 0U},
            cpygraph::bytecode::CodeMetadata{});
        require(lifted.protocol_operation == operation &&
                    cpygraph::bytecode::containsPythonMethod(
                        lifted.protocol_methods, method),
                "shared adapter core retains an exact unary protocol identity");
    };
    using P = cpygraph::bytecode::PythonProtocolOperation;
    using M = cpygraph::bytecode::PythonSpecialMethod;
    require_protocol(py310, CPYGRAPH_PY310_UNARY_NEGATIVE,
                     P::Negative, M::Negative);
    require_protocol(py311, CPYGRAPH_PY311_UNARY_NEGATIVE,
                     P::Negative, M::Negative);
    require_protocol(py312, CPYGRAPH_PY312_UNARY_NEGATIVE,
                     P::Negative, M::Negative);
    require_protocol(py313, CPYGRAPH_PY313_UNARY_NEGATIVE,
                     P::Negative, M::Negative);
    require_protocol(py314, CPYGRAPH_PY314_UNARY_NEGATIVE,
                     P::Negative, M::Negative);
    require_protocol(py310, CPYGRAPH_PY310_DELETE_ATTR,
                     P::DeleteAttribute, M::DeleteAttribute);
    require_protocol(py314, CPYGRAPH_PY314_GET_ITER,
                     P::Iteration, M::Iter);
    require_protocol(py310, CPYGRAPH_PY310_GET_AWAITABLE,
                     P::DynamicProtocol, M::Await);
    require_protocol(py311, CPYGRAPH_PY311_GET_AITER,
                     P::Iteration, M::AsyncIter);
    require_protocol(py312, CPYGRAPH_PY312_CONTAINS_OP,
                     P::DynamicProtocol, M::Contains);
    require_protocol(py313, CPYGRAPH_PY313_FORMAT_SIMPLE,
                     P::DynamicProtocol, M::Format);
    require_protocol(py314, CPYGRAPH_PY314_GET_ANEXT,
                     P::Iteration, M::AsyncNext);
    const auto extended_methods =
        cpygraph::bytecode::pythonMethod(M::AsyncEnter) |
        cpygraph::bytecode::pythonMethod(M::Reversed) |
        cpygraph::bytecode::pythonMethod(M::Add);
    require(cpygraph::bytecode::containsPythonMethod(
                extended_methods, M::AsyncEnter) &&
                cpygraph::bytecode::containsPythonMethod(
                    extended_methods, M::Reversed) &&
                cpygraph::bytecode::containsPythonMethod(
                    extended_methods, M::Add) &&
                !cpygraph::bytecode::containsPythonMethod(
                    extended_methods, M::Hash),
            "packed numeric protocol sets retain methods above 64 without aliases");
    const auto conversion =
        cpygraph::python::builtinProtocolDispatch("float");
    require(cpygraph::bytecode::containsPythonMethod(
                conversion.candidate_methods, M::Float) &&
                !cpygraph::bytecode::containsPythonMethod(
                    conversion.candidate_methods, M::Int),
            "builtin protocol dispatch retains the exact conversion method");
    const auto range_dispatch =
        cpygraph::python::builtinProtocolDispatch("range");
    require(range_dispatch.external_callee ==
                cpygraph::bytecode::PythonExternalCallee::Range,
            "builtin dispatch retains a compact external callee identity");
    require(cpygraph::python::builtinProtocolDispatch("__import__")
                    .external_callee ==
                cpygraph::bytecode::PythonExternalCallee::Import,
            "builtin dispatch retains the exact dynamic-import identity");
    require(
        cpygraph::python::moduleAttributeCallee(
            "importlib", "import_module") ==
            cpygraph::bytecode::PythonExternalCallee::ImportModule,
        "module attribute dispatch retains exact importlib identity");
    require(
        cpygraph::python::moduleAttributeCallee(
            "not_importlib", "import_module") ==
            cpygraph::bytecode::PythonExternalCallee::None,
        "module attribute dispatch rejects a same-named unrelated method");
    require(cpygraph::python::builtinAttributeCallee("items") ==
                cpygraph::bytecode::PythonExternalCallee::DictItems,
            "builtin attribute dispatch retains a compact external method identity");
    const auto less = py310.lift(cpygraph::bytecode::RawInstruction{
        0U, CPYGRAPH_PY310_COMPARE_OP, CPYGRAPH_COMPARE_LESS}, metadata);
    require(less.protocol_operation == P::Comparison &&
                less.comparison_kind ==
                    cpygraph::bytecode::PythonComparisonKind::Less &&
                cpygraph::bytecode::containsPythonMethod(
                    less.protocol_methods, M::Less) &&
                cpygraph::bytecode::containsPythonMethod(
                    less.protocol_methods, M::Greater),
            "comparison retains normalized kind and protocol method identities");
    const auto not_equal = py314.lift(
        cpygraph::bytecode::RawInstruction{
            0U, CPYGRAPH_PY314_COMPARE_OP,
            CPYGRAPH_COMPARE_NOT_EQUAL <<
                CPYGRAPH_PY314_COMPARE_ARGUMENT_SHIFT},
        metadata);
    require(not_equal.comparison_kind ==
                cpygraph::bytecode::PythonComparisonKind::NotEqual,
            "comparison identity is version independent");

    auto dynamic_operation = instruction(SemanticOpcode::Generic);
    dynamic_operation.offset = 0U;
    dynamic_operation.stack_output_count = 1U;
    dynamic_operation.fresh_result = true;
    auto implicit_return = instruction(SemanticOpcode::Return);
    implicit_return.offset = 2U;
    implicit_return.implicit_constant = true;
    const auto cfg = cpygraph::cfg::CFGBuilder().build(
        {dynamic_operation, implicit_return});
    cpygraph::AndersenPointerAnalysis pta;
    const auto constraints =
        cpygraph::SemanticPTAConstraintBuilder(pta).build(cfg, 1U);
    require(constraints.call_sites.size() == 1U,
            "implicit Python protocol operation creates a CG call site");
    const auto graph = cpygraph::cg::CallGraphBuilder(pta).build(
        constraints.call_sites);
    require(graph.edges().size() == 1U && !graph.edges().front().target,
            "unmodeled Python protocol dispatch remains an explicit unknown edge");
}
