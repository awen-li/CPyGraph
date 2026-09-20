#include "analysis/ddg/data_dependency_graph.h"

#include <algorithm>
#include <stdexcept>

namespace cpygraph::ddg {

NodeId DataDependencyGraph::addNode(NodeKind kind, std::string label, std::uint32_t offset,
                                    CodeObjectId code) {
    const NodeId id = nodes_.size() + 1;
    nodes_.push_back({id, kind, debug_strings_.intern(std::move(label)), offset, code});
    outgoing_edge_indices_.emplace_back();
    return id;
}

std::vector<NodeId> DataDependencyGraph::append(const DataDependencyGraph& fragment) {
    std::vector<NodeId> remap(fragment.nodes().size() + 1, 0);
    for (const auto& node : fragment.nodes())
        remap[node.id] = addNode(node.kind,
            std::string(fragment.debugString(node.label)), node.bytecode_offset, node.code);
    for (const auto& edge : fragment.edges())
        addEdge(remap.at(edge.source), remap.at(edge.target), edge.kind);
    return remap;
}

void DataDependencyGraph::addEdge(NodeId source, NodeId target, EdgeKind kind) {
    node(source);
    node(target);
    if (hasEdge(source, target, kind)) return;
    outgoing_edge_indices_[source - 1U].push_back(edges_.size());
    edges_.push_back({source, target, kind});
}

const Node& DataDependencyGraph::node(NodeId id) const {
    if (id == 0 || id > nodes_.size()) throw std::out_of_range("invalid DDG node id");
    return nodes_[id - 1];
}

bool DataDependencyGraph::hasEdge(NodeId source, NodeId target, EdgeKind kind) const noexcept {
    if (source == 0 || source > outgoing_edge_indices_.size()) return false;
    return std::any_of(outgoing_edge_indices_[source - 1U].begin(),
        outgoing_edge_indices_[source - 1U].end(), [&](std::size_t index) {
            const auto& edge = edges_[index];
            return edge.target == target && edge.kind == kind;
        });
}

}  // namespace cpygraph::ddg
