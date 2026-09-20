#pragma once

#include "analysis/cfg/control_flow_graph.h"

#include <cstddef>
#include <vector>

namespace cpygraph::cdg {

class PostDominatorTree {
public:
    PostDominatorTree() = default;
    explicit PostDominatorTree(
        std::vector<cfg::BlockId> immediate_postdominators);

    std::size_t blockCount() const noexcept {
        return immediate_postdominators_.size();
    }
    bool postdominates(cfg::BlockId candidate, cfg::BlockId block) const;
    cfg::BlockId immediatePostDominator(cfg::BlockId block) const;
    std::vector<cfg::BlockId> postdominators(cfg::BlockId block) const;

private:
    std::vector<cfg::BlockId> immediate_postdominators_;
};

class PostDominatorBuilder {
public:
    PostDominatorTree build(const cfg::ControlFlowGraph& graph) const;
};

}  // namespace cpygraph::cdg
