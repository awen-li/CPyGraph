#pragma once

#include "analysis/cfg/control_flow_graph.h"

#include <string>
#include <vector>

namespace cpygraph::cfg {

// Removes exception transfers that are disproved by version-independent
// bytecode facts. The pass is intentionally separate from CFGBuilder because
// package analysis must complete PTA before publishing a refined CFG.
class CFGRefiner {
public:
    ControlFlowGraph refine(
        const ControlFlowGraph& graph,
        const std::vector<std::string>& initially_assigned = {}) const;
};

}  // namespace cpygraph::cfg
