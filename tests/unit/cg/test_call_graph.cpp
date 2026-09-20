#include "api/cg.h"
#include "api/pta.h"
#include "test_support.h"

#include <stdexcept>

namespace {

constexpr std::size_t kMaximumCallEdgeBytes = 40U;
constexpr std::size_t kMaximumGroupDescriptorBytes = 16U;
constexpr std::size_t kMaximumCallGraphNodeBytes = 16U;
constexpr cpygraph::CodeObjectId kCallerCode = 7U;
constexpr cpygraph::CodeObjectId kFirstTargetCode = 21U;
constexpr cpygraph::CodeObjectId kSecondTargetCode = 22U;
constexpr cpygraph::CodeObjectId kAbsentCode = 404U;
constexpr cpygraph::cg::CallGraphDegree kExpectedCallerInDegree = 0U;
constexpr cpygraph::cg::CallGraphDegree kExpectedCallerOutDegree = 4U;
constexpr cpygraph::cg::CallGraphDegree kExpectedTargetInDegree = 1U;
constexpr cpygraph::cg::CallGraphDegree kExpectedTargetOutDegree = 0U;
constexpr std::size_t kExpectedCallGraphNodeCount = 3U;

static_assert(sizeof(cpygraph::cg::CallEdge) <= kMaximumCallEdgeBytes);
static_assert(sizeof(cpygraph::cg::UnresolvedCalleeGroup) <=
              kMaximumGroupDescriptorBytes);
static_assert(sizeof(cpygraph::cg::CallGraphNode) <=
              kMaximumCallGraphNodeBytes);
static_assert(sizeof(cpygraph::bytecode::PythonMethodSet) ==
              sizeof(std::uint64_t));

}  // namespace

int main() {
    cpygraph::AndersenPointerAnalysis pta;
    pta.addAddressOf(1, 101);
    pta.addAddressOf(1, 102);
    pta.addAddressOf(1, 999);  // unknown/non-callable alternative
    pta.addCopy(1, 2);
    cpygraph::cg::CallGraphBuilder builder(pta);
    builder.registerCallable(101, {11, 21});
    builder.registerCallable(102, {12, 22});
    const auto graph = builder.build({
        {1, 7, 2},
        {2, 7, 999,
         {cpygraph::cg::UnresolvedCalleeKind::PythonProtocol,
          cpygraph::bytecode::PythonProtocolOperation::ContextExit,
          cpygraph::bytecode::pythonMethod(
              cpygraph::bytecode::PythonSpecialMethod::Exit)}},
    });
    const auto resolved = graph.targets(1);
    require(resolved.size() == 3, "call graph retains resolved and unknown points-to alternatives");
    std::size_t known = 0;
    std::size_t unknown_count = 0;
    for (const auto& edge : resolved) edge.target ? ++known : ++unknown_count;
    require(known == 2 && unknown_count == 1,
            "mixed callable/non-callable set preserves uncertainty");
    const auto unknown = graph.targets(2);
    require(unknown.size() == 1 && !unknown[0].target.has_value(),
            "call graph keeps unresolved target explicit");
    const auto& unknown_group = graph.unresolvedGroup(
        unknown[0].unresolved_group);
    require(unknown_group.operation ==
                cpygraph::bytecode::PythonProtocolOperation::ContextExit &&
                cpygraph::bytecode::containsPythonMethod(
                    unknown_group.candidate_methods,
                    cpygraph::bytecode::PythonSpecialMethod::Exit),
            "unresolved edge preserves its Python protocol target group");
    const cpygraph::cg::UnresolvedCalleeGroup external_group{
        cpygraph::cg::UnresolvedCalleeKind::PythonProtocol,
        cpygraph::bytecode::PythonProtocolOperation::DynamicProtocol,
        {}, cpygraph::bytecode::PythonExternalCallee::Range};
    require(external_group.external_callee ==
                cpygraph::bytecode::PythonExternalCallee::Range,
            "unresolved groups retain numeric external callee identities");
    const auto* caller_node = graph.node(kCallerCode);
    const auto* first_target_node = graph.node(kFirstTargetCode);
    const auto* second_target_node = graph.node(kSecondTargetCode);
    require(graph.nodes().size() == kExpectedCallGraphNodeCount &&
                caller_node != nullptr && first_target_node != nullptr &&
                second_target_node != nullptr,
            "call graph materializes compact node degree records");
    require(caller_node->in_degree == kExpectedCallerInDegree &&
                caller_node->out_degree == kExpectedCallerOutDegree,
            "resolved and unresolved edges update caller out degree");
    require(first_target_node->in_degree == kExpectedTargetInDegree &&
                first_target_node->out_degree == kExpectedTargetOutDegree &&
                second_target_node->in_degree == kExpectedTargetInDegree &&
                second_target_node->out_degree == kExpectedTargetOutDegree,
            "resolved edges update target in degree");
    require(graph.node(kAbsentCode) == nullptr,
            "call graph node lookup rejects absent code objects");

    const auto repeated_group_graph = builder.build({
        {5, 7, 999,
         {cpygraph::cg::UnresolvedCalleeKind::PythonProtocol,
          cpygraph::bytecode::PythonProtocolOperation::ContextExit,
          cpygraph::bytecode::pythonMethod(
              cpygraph::bytecode::PythonSpecialMethod::Exit)}},
        {6, 7, 999,
         {cpygraph::cg::UnresolvedCalleeKind::PythonProtocol,
          cpygraph::bytecode::PythonProtocolOperation::ContextExit,
          cpygraph::bytecode::pythonMethod(
              cpygraph::bytecode::PythonSpecialMethod::Exit)}},
    });
    require(repeated_group_graph.targets(5).front().unresolved_group ==
                repeated_group_graph.targets(6).front().unresolved_group &&
                repeated_group_graph.unresolvedGroupCount() == 1U,
            "identical unresolved protocols share one interned group id");

    bool rejected_duplicate_site = false;
    try { builder.build({{3, 7, 1}, {3, 8, 2}}); }
    catch (const std::invalid_argument&) { rejected_duplicate_site = true; }
    require(rejected_duplicate_site, "call graph rejects ambiguous duplicate call-site ids");

    bool rejected_unknown_callable = false;
    try { builder.registerCallable(pta.unknownObject(), {13, 23}); }
    catch (const std::invalid_argument&) { rejected_unknown_callable = true; }
    require(rejected_unknown_callable,
            "call graph cannot register the unknown summary object as a concrete callable");

    bool rejected_conflicting_callable = false;
    try { builder.registerCallable(101, {99, 99}); }
    catch (const std::invalid_argument&) { rejected_conflicting_callable = true; }
    require(rejected_conflicting_callable,
            "one callable object cannot silently overwrite its registered target");

    bool rejected_invalid_site = false;
    try { builder.build({{4, 0, 1}}); }
    catch (const std::invalid_argument&) { rejected_invalid_site = true; }
    require(rejected_invalid_site, "call graph rejects call sites with reserved endpoint ids");

    bool rejected_invalid_target = false;
    try { builder.registerCallable(103, {0, 24}); }
    catch (const std::invalid_argument&) { rejected_invalid_target = true; }
    require(rejected_invalid_target, "call graph rejects callable targets with reserved ids");

    bool rejected_invalid_edge = false;
    try {
        cpygraph::cg::CallGraph(std::vector<cpygraph::cg::CallEdge>{
            {0U, 7U, cpygraph::cg::CallableTarget{11U, 21U}, 0U},
        });
    } catch (const std::invalid_argument&) {
        rejected_invalid_edge = true;
    }
    require(rejected_invalid_edge,
            "call graph constructor rejects edges with reserved endpoint ids");

    bool rejected_partial_target = false;
    try {
        cpygraph::cg::CallGraph(std::vector<cpygraph::cg::CallEdge>{
            {9U, 7U, cpygraph::cg::CallableTarget{0U, 21U}, 0U},
        });
    } catch (const std::invalid_argument&) {
        rejected_partial_target = true;
    }
    require(rejected_partial_target,
            "call graph constructor rejects half-initialized callable targets");

    bool rejected_conflicting_caller = false;
    try {
        cpygraph::cg::CallGraph(std::vector<cpygraph::cg::CallEdge>{
            {9U, 7U, cpygraph::cg::CallableTarget{11U, 21U}, 0U},
            {9U, 8U, cpygraph::cg::CallableTarget{12U, 22U}, 0U},
        });
    } catch (const std::invalid_argument&) {
        rejected_conflicting_caller = true;
    }
    require(rejected_conflicting_caller,
            "one call-site id cannot be attached to multiple callers");
}
