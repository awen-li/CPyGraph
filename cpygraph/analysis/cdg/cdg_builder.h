#pragma once

#include "analysis/cdg/control_dependency_graph.h"
#include "analysis/cdg/post_dominators.h"

namespace cpygraph::cdg {

struct ControlDependenceAnalysis {
    PostDominatorTree postdominators;
    ControlDependencyGraph graph;
};

class CDGBuilder {
public:
    ControlDependenceAnalysis build(
        const cfg::ControlFlowGraph& graph) const;
};

}  // namespace cpygraph::cdg
