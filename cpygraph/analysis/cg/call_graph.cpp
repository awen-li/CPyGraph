#include "analysis/cg/call_graph.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace cpygraph::cg {
namespace {

CalleeGroupId internGroup(std::vector<UnresolvedCalleeGroup>& groups,
                          const UnresolvedCalleeGroup& group) {
    const auto found = std::find(groups.begin(), groups.end(), group);
    if (found != groups.end())
        return static_cast<CalleeGroupId>(found - groups.begin() + 1U);
    groups.push_back(group);
    return static_cast<CalleeGroupId>(groups.size());
}

void incrementDegree(CallGraphDegree& degree) {
    if (degree == std::numeric_limits<CallGraphDegree>::max())
        throw std::overflow_error("call-graph node degree overflow");
    ++degree;
}

}  // namespace

CallGraph::CallGraph(std::vector<CallEdge> edges,
                     std::vector<UnresolvedCalleeGroup> groups)
    : edges_(std::move(edges)), groups_(std::move(groups)) {
    std::unordered_map<std::uint64_t, CodeObjectId> callers;
    std::unordered_map<CodeObjectId, CallGraphNode> nodes;
    for (auto& edge : edges_) {
        if (edge.site == 0U || edge.caller == 0U)
            throw std::invalid_argument(
                "call edge site and caller ids must be nonzero");
        const auto [caller, inserted] = callers.emplace(edge.site, edge.caller);
        if (!inserted && caller->second != edge.caller)
            throw std::invalid_argument(
                "one call-site id cannot have multiple callers");

        const auto& target = *edge.target;
        if ((target.function == 0U) != (target.code == 0U))
            throw std::invalid_argument(
                "call edge target ids must both be zero or both be nonzero");
        if (edge.target) {
            edge.unresolved_group = 0U;
        } else if (edge.unresolved_group == 0U) {
            edge.unresolved_group = internGroup(
                groups_, UnresolvedCalleeGroup{});
        } else if (edge.unresolved_group > groups_.size()) {
            throw std::invalid_argument(
                "call edge references an undefined callee group");
        }

        auto& caller_node = nodes[edge.caller];
        caller_node.code = edge.caller;
        incrementDegree(caller_node.out_degree);
        if (edge.target) {
            auto& target_node = nodes[target.code];
            target_node.code = target.code;
            incrementDegree(target_node.in_degree);
        }
    }
    nodes_.reserve(nodes.size());
    for (const auto& entry : nodes) nodes_.push_back(entry.second);
    std::sort(nodes_.begin(), nodes_.end(),
              [](const auto& first, const auto& second) {
                  return first.code < second.code;
              });
}

const CallGraphNode* CallGraph::node(CodeObjectId code) const noexcept {
    const auto found = std::lower_bound(
        nodes_.begin(), nodes_.end(), code,
        [](const auto& candidate, CodeObjectId expected) {
            return candidate.code < expected;
        });
    return found == nodes_.end() || found->code != code ? nullptr : &*found;
}

std::vector<CallEdge> CallGraph::targets(std::uint64_t site) const {
    std::vector<CallEdge> result;
    for (const auto& edge : edges_) if (edge.site == site) result.push_back(edge);
    return result;
}

const UnresolvedCalleeGroup& CallGraph::unresolvedGroup(
    CalleeGroupId id) const {
    if (id == 0U || id > groups_.size())
        throw std::out_of_range("call-graph callee-group id is out of range");
    return groups_[id - 1U];
}

CallGraphBuilder::CallGraphBuilder(PointerAnalysis& pointer_analysis) noexcept
    : pointer_analysis_(pointer_analysis) {}

void CallGraphBuilder::registerCallable(ObjectId object, CallableTarget target) {
    if (object == 0 || object == pointer_analysis_.unknownObject())
        throw std::invalid_argument("callable object id is reserved");
    if (target.function == 0 || target.code == 0)
        throw std::invalid_argument("callable target ids must be nonzero");
    const auto found = callables_.find(object);
    if (found != callables_.end() &&
        (found->second.function != target.function || found->second.code != target.code))
        throw std::invalid_argument(
            "callable object " + std::to_string(object) +
            " already targets function/code " +
            std::to_string(found->second.function) + "/" +
            std::to_string(found->second.code) +
            "; cannot retarget to " + std::to_string(target.function) + "/" +
            std::to_string(target.code));
    callables_.insert_or_assign(object, std::move(target));
}

CallGraph CallGraphBuilder::build(const std::vector<CallSite>& sites) {
    pointer_analysis_.solve();
    std::vector<CallEdge> edges;
    std::vector<UnresolvedCalleeGroup> groups;
    std::unordered_set<std::uint64_t> site_ids;
    for (const auto& site : sites) {
        if (site.id == 0 || !site_ids.insert(site.id).second)
            throw std::invalid_argument("call site ids must be nonzero and unique");
        if (site.caller == 0 || site.callee_value == 0)
            throw std::invalid_argument("call site caller and callee ids must be nonzero");
        const auto& points_to = pointer_analysis_.pointsTo(site.callee_value);
        bool unresolved = points_to.empty();
        for (const auto object : points_to) {
            const auto callable = callables_.find(object);
            if (callable != callables_.end()) {
                edges.push_back({site.id, site.caller, callable->second, 0U});
            } else unresolved = true;
        }
        if (unresolved)
            edges.push_back({site.id, site.caller, std::nullopt,
                             internGroup(groups, site.unresolved_group)});
    }
    return CallGraph(std::move(edges), std::move(groups));
}

}  // namespace cpygraph::cg
