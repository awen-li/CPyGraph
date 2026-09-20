#pragma once

#include "common/model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cpygraph::ddg {

using NodeId = std::uint64_t;

enum class NodeKind : std::uint8_t {
    Input,
    Constant,
    Load,
    Store,
    Operation,
    Return,
    Unknown,
};
enum class EdgeKind : std::uint8_t {
    DefUse,
    Operand,
    Result,
    ReturnValue,
    Address,
    Callee,
};

struct Node {
    NodeId id{};
    NodeKind kind{NodeKind::Unknown};
    SymbolId label{};
    std::uint32_t bytecode_offset{};
    CodeObjectId code{};
};

struct Edge {
    NodeId source{};
    NodeId target{};
    EdgeKind kind{EdgeKind::DefUse};
};

class DataDependencyGraph {
public:
    NodeId addNode(NodeKind kind, std::string label, std::uint32_t offset,
                   CodeObjectId code = 0);
    void addEdge(NodeId source, NodeId target, EdgeKind kind);
    std::vector<NodeId> append(const DataDependencyGraph& fragment);
    const Node& node(NodeId id) const;
    const std::vector<Node>& nodes() const noexcept { return nodes_; }
    const std::vector<Edge>& edges() const noexcept { return edges_; }
    bool hasEdge(NodeId source, NodeId target, EdgeKind kind) const noexcept;
    std::string_view debugString(SymbolId id) const noexcept { return debug_strings_.lookup(id); }

private:
    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
    std::vector<std::vector<std::size_t>> outgoing_edge_indices_;
    DebugStringTable debug_strings_;
};

}  // namespace cpygraph::ddg
