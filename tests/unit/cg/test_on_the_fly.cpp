#include "api/cg.h"
#include "api/pta.h"
#include "test_support.h"

#include <cstddef>
#include <stdexcept>

namespace {

constexpr cpygraph::FunctionId kFirstFunction = 11;
constexpr cpygraph::FunctionId kSecondFunction = 12;
constexpr cpygraph::CodeObjectId kFirstCode = 21;
constexpr cpygraph::CodeObjectId kSecondCode = 22;
constexpr cpygraph::CodeObjectId kRootCode = 20;
constexpr std::uint64_t kRootCallSite = 31;
constexpr std::uint64_t kNestedCallSite = 32;
constexpr std::size_t kExpectedActivatedCallableCount = 2;
constexpr std::size_t kExpectedCallSiteCount = 2;
constexpr std::size_t kExpectedResolvedEdgeCount = 2;
constexpr std::size_t kExpectedUnresolvedEdgeCount = 1;
constexpr cpygraph::FunctionId kEntryFunction = 41;
constexpr cpygraph::CodeObjectId kEntryCode = 42;
constexpr std::uint64_t kRecursiveCallSite = 43;
constexpr std::uint64_t kAtomicCallSite = 51;
constexpr std::uint64_t kAssignmentCallSite = 61;

}  // namespace

int main() {
    cpygraph::AndersenPointerAnalysis pta;
    const auto root_callee = pta.newValue();
    const auto root_actual = pta.newValue();
    const auto root_result = pta.newValue();
    const auto first_callable = pta.newObject();
    const auto second_callable = pta.newObject();
    pta.addAddressOf(root_callee, first_callable);
    pta.addUnknown(root_callee);
    pta.addAddressOf(root_actual, second_callable);

    cpygraph::cg::OnTheFlyCallGraphBuilder builder(pta);
    builder.registerCallable(first_callable, {kFirstFunction, kFirstCode});
    builder.registerCallable(second_callable, {kSecondFunction, kSecondCode});
    builder.addCallSite({{kRootCallSite, kRootCode, root_callee, {}},
                         {{root_actual}}, root_result, {}});

    std::size_t provider_calls = 0;
    const auto result = builder.build(
        [&](cpygraph::cg::CallableTarget target) {
            ++provider_calls;
            if (target.code == kFirstCode) {
                const auto formal = pta.newValue();
                const auto nested_result = pta.newValue();
                return cpygraph::cg::CallableBodySummary{
                    {formal}, {},
                    {{{kNestedCallSite, kFirstCode, formal, {}}, {},
                      nested_result, {}}},
                };
            }
            if (target.code == kSecondCode)
                return cpygraph::cg::CallableBodySummary{};
            throw std::logic_error("unexpected callable activated by on-the-fly test");
        });

    require(provider_calls == kExpectedActivatedCallableCount &&
                result.activated_callable_count == kExpectedActivatedCallableCount,
            "call edges activate each reachable body exactly once");
    require(result.call_site_count == kExpectedCallSiteCount,
            "body activation adds nested call sites lazily");

    std::size_t resolved = 0;
    std::size_t unresolved = 0;
    for (const auto& edge : result.graph.edges())
        edge.target ? ++resolved : ++unresolved;
    require(resolved == kExpectedResolvedEdgeCount,
            "interprocedural argument flow resolves a nested call in a later PTA round");
    require(unresolved == kExpectedUnresolvedEdgeCount,
            "on-the-fly construction preserves unknown call alternatives");
    const auto nested_targets = result.graph.targets(kNestedCallSite);
    require(nested_targets.size() == 1U && nested_targets.front().target &&
                nested_targets.front().target->code == kSecondCode,
            "nested call resolves to the callable carried through the activated formal");

    cpygraph::AndersenPointerAnalysis rooted_pta;
    const auto recursive_callee = rooted_pta.newValue();
    const auto recursive_result = rooted_pta.newValue();
    const auto recursive_callable = rooted_pta.newObject();
    rooted_pta.addAddressOf(recursive_callee, recursive_callable);
    cpygraph::cg::OnTheFlyCallGraphBuilder rooted_builder(rooted_pta);
    const cpygraph::cg::CallableTarget entry{kEntryFunction, kEntryCode};
    rooted_builder.registerCallable(recursive_callable, entry);
    rooted_builder.addEntryPoint(entry, {{}, {},
        {{{kRecursiveCallSite, kEntryCode, recursive_callee, {}}, {},
          recursive_result, {}}}});
    std::size_t rooted_provider_calls = 0U;
    const auto rooted_result = rooted_builder.build(
        [&](cpygraph::cg::CallableTarget) {
            ++rooted_provider_calls;
            return cpygraph::cg::CallableBodySummary{};
        });
    require(rooted_provider_calls == 0U &&
                rooted_result.activated_callable_count == 1U,
            "an entry body is reused when a recursive call reaches its own target");
    const auto recursive_targets = rooted_result.graph.targets(kRecursiveCallSite);
    require(recursive_targets.size() == 1U && recursive_targets.front().target &&
                recursive_targets.front().target->code == kEntryCode,
            "a rooted recursive call resolves without duplicate call-site registration");

    cpygraph::AndersenPointerAnalysis atomic_pta;
    cpygraph::cg::OnTheFlyCallGraphBuilder atomic_builder(atomic_pta);
    const auto atomic_callee = atomic_pta.newValue();
    const auto atomic_result = atomic_pta.newValue();
    bool rejected_batch = false;
    try {
        atomic_builder.addCallSites({
            {{kAtomicCallSite, kRootCode, atomic_callee, {}}, {}, atomic_result, {}},
            {{kAtomicCallSite, kRootCode, atomic_callee, {}}, {}, atomic_result, {}},
        });
    } catch (const std::invalid_argument&) {
        rejected_batch = true;
    }
    require(rejected_batch,
            "batched call-site registration rejects duplicate ids");
    atomic_builder.addCallSite(
        {{kAtomicCallSite, kRootCode, atomic_callee, {}}, {}, atomic_result, {}});
    atomic_builder.updateUnresolvedGroup(
        kAtomicCallSite,
        {cpygraph::cg::UnresolvedCalleeKind::PythonProtocol,
         cpygraph::bytecode::PythonProtocolOperation::Truth,
         cpygraph::bytecode::pythonMethod(
             cpygraph::bytecode::PythonSpecialMethod::Bool)});
    const auto atomic_result_graph = atomic_builder.build(
        [](cpygraph::cg::CallableTarget) {
            return cpygraph::cg::CallableBodySummary{};
        });
    require(atomic_result_graph.call_site_count == 1U,
            "a rejected call-site batch does not partially mutate the builder");
    const auto atomic_targets = atomic_result_graph.graph.targets(
        kAtomicCallSite);
    require(atomic_targets.size() == 1U && !atomic_targets.front().target &&
                cpygraph::bytecode::containsPythonMethod(
                    atomic_result_graph.graph.unresolvedGroup(
                        atomic_targets.front().unresolved_group)
                        .candidate_methods,
                    cpygraph::bytecode::PythonSpecialMethod::Bool),
            "late PTA refinement updates the registered unresolved group");

    cpygraph::AndersenPointerAnalysis assignment_pta;
    const auto assigned_callee = assignment_pta.newValue();
    const auto assigned_result = assignment_pta.newValue();
    const auto assigned_callable = assignment_pta.newObject();
    assignment_pta.addAddressOf(assigned_callee, assigned_callable);
    cpygraph::cg::OnTheFlyCallGraphBuilder assignment_builder(assignment_pta);
    assignment_builder.registerCallable(
        assigned_callable, {kFirstFunction, kFirstCode});
    assignment_builder.assignCallable(
        assigned_callable, {kSecondFunction, kSecondCode});
    assignment_builder.addCallSite(
        {{kAssignmentCallSite, kRootCode, assigned_callee, {}}, {},
         assigned_result, {}});
    const auto assignment_graph = assignment_builder.build(
        [](cpygraph::cg::CallableTarget) {
            return cpygraph::cg::CallableBodySummary{};
        });
    const auto assignment_targets =
        assignment_graph.graph.targets(kAssignmentCallSite);
    require(assignment_targets.size() == 1U &&
                assignment_targets.front().target &&
                assignment_targets.front().target->code == kSecondCode,
            "explicit callable assignment models Python name overwrite semantics");
}
