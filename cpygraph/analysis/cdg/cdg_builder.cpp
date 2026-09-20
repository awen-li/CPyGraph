#include "analysis/cdg/cdg_builder.h"

#include <algorithm>
#include <set>

namespace cpygraph::cdg {
namespace {

BranchOutcome outcome(cfg::EdgeKind kind) noexcept {
    switch (kind) {
        case cfg::EdgeKind::BranchTrue: return BranchOutcome::True;
        case cfg::EdgeKind::BranchFalse: return BranchOutcome::False;
        case cfg::EdgeKind::Exception: return BranchOutcome::Exception;
        case cfg::EdgeKind::Resume: return BranchOutcome::Resume;
        case cfg::EdgeKind::Fallthrough:
        case cfg::EdgeKind::Jump: return BranchOutcome::Normal;
    }
    return BranchOutcome::Normal;
}

}  // namespace

ControlDependenceAnalysis CDGBuilder::build(
    const cfg::ControlFlowGraph& control_flow) const {
    auto postdominators = PostDominatorBuilder().build(control_flow);
    std::vector<Edge> dependencies;
    for (const auto& block : control_flow.blocks()) {
        const auto outgoing = control_flow.outgoingEdges(block.id);
        std::set<cfg::BlockId> distinct_successors;
        for (const auto& edge : outgoing)
            distinct_successors.insert(edge.target);
        if (distinct_successors.size() < 2U) continue;

        const auto stop = postdominators.immediatePostDominator(block.id);
        for (const auto& edge : outgoing) {
            if (postdominators.postdominates(edge.target, block.id)) continue;
            auto dependent = edge.target;
            std::set<cfg::BlockId> visited;
            while (dependent != 0U && dependent != stop &&
                   visited.insert(dependent).second) {
                dependencies.push_back(
                    {block.id, dependent, outcome(edge.kind)});
                dependent = postdominators.immediatePostDominator(dependent);
            }
        }
    }
    return {std::move(postdominators),
            ControlDependencyGraph(
                control_flow.blocks().size(), std::move(dependencies))};
}

}  // namespace cpygraph::cdg
