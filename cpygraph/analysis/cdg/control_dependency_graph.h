#pragma once

#include "analysis/cfg/control_flow_graph.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cpygraph::cdg {

enum class BranchOutcome : std::uint8_t {
    Normal,
    True,
    False,
    Exception,
    Resume,
};

struct Edge {
    cfg::BlockId controller{};
    cfg::BlockId dependent{};
    BranchOutcome outcome{BranchOutcome::Normal};
};

class ControlDependencyGraph {
public:
    ControlDependencyGraph() = default;
    ControlDependencyGraph(std::size_t block_count, std::vector<Edge> edges);

    std::size_t blockCount() const noexcept { return block_count_; }
    const std::vector<Edge>& edges() const noexcept { return edges_; }
    bool hasEdge(cfg::BlockId controller, cfg::BlockId dependent,
                 BranchOutcome outcome) const noexcept;
    std::vector<cfg::BlockId> controllers(cfg::BlockId dependent) const;
    std::vector<cfg::BlockId> dependents(cfg::BlockId controller) const;
    std::vector<Edge> outgoingEdges(cfg::BlockId controller) const;

private:
    std::size_t block_count_{};
    std::vector<Edge> edges_;
};

}  // namespace cpygraph::cdg
