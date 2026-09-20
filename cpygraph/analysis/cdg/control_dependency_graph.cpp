#include "analysis/cdg/control_dependency_graph.h"

#include <algorithm>
#include <stdexcept>

namespace cpygraph::cdg {
namespace {

void validateBlock(cfg::BlockId block, std::size_t count) {
    if (block == 0U || block > count)
        throw std::out_of_range("invalid control-dependency block id");
}

}  // namespace

ControlDependencyGraph::ControlDependencyGraph(
    std::size_t block_count, std::vector<Edge> edges)
    : block_count_(block_count), edges_(std::move(edges)) {
    for (const auto& edge : edges_) {
        validateBlock(edge.controller, block_count_);
        validateBlock(edge.dependent, block_count_);
    }
    std::sort(edges_.begin(), edges_.end(), [](const auto& left, const auto& right) {
        if (left.controller != right.controller)
            return left.controller < right.controller;
        if (left.dependent != right.dependent)
            return left.dependent < right.dependent;
        return left.outcome < right.outcome;
    });
    edges_.erase(std::unique(edges_.begin(), edges_.end(),
        [](const auto& left, const auto& right) {
            return left.controller == right.controller &&
                   left.dependent == right.dependent &&
                   left.outcome == right.outcome;
        }), edges_.end());
}

bool ControlDependencyGraph::hasEdge(
    cfg::BlockId controller, cfg::BlockId dependent,
    BranchOutcome outcome) const noexcept {
    return std::any_of(edges_.begin(), edges_.end(), [&](const auto& edge) {
        return edge.controller == controller && edge.dependent == dependent &&
               edge.outcome == outcome;
    });
}

std::vector<cfg::BlockId> ControlDependencyGraph::controllers(
    cfg::BlockId dependent) const {
    validateBlock(dependent, block_count_);
    std::vector<cfg::BlockId> result;
    for (const auto& edge : edges_)
        if (edge.dependent == dependent &&
            std::find(result.begin(), result.end(), edge.controller) == result.end())
            result.push_back(edge.controller);
    return result;
}

std::vector<cfg::BlockId> ControlDependencyGraph::dependents(
    cfg::BlockId controller) const {
    validateBlock(controller, block_count_);
    std::vector<cfg::BlockId> result;
    for (const auto& edge : edges_)
        if (edge.controller == controller &&
            std::find(result.begin(), result.end(), edge.dependent) == result.end())
            result.push_back(edge.dependent);
    return result;
}

std::vector<Edge> ControlDependencyGraph::outgoingEdges(
    cfg::BlockId controller) const {
    validateBlock(controller, block_count_);
    std::vector<Edge> result;
    for (const auto& edge : edges_)
        if (edge.controller == controller) result.push_back(edge);
    return result;
}

}  // namespace cpygraph::cdg
