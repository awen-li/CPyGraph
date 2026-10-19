#pragma once

#include "analysis/ddg/data_dependency_graph.h"

#include <vector>

namespace cpygraph::ddg {

struct ResolvedCallFlow {
    NodeId call{};
    // Each positional argument may have several reaching definitions. An
    // empty slot is retained when prerequisite coverage marks it unresolved.
    std::vector<std::vector<NodeId>> actual_arguments;
    std::vector<NodeId> formal_arguments;
    std::vector<NodeId> return_values;
};

class InterproceduralStitcher {
public:
    DataDependencyGraph stitch(DataDependencyGraph graph,
                               const std::vector<ResolvedCallFlow>& calls) const;
};

}  // namespace cpygraph::ddg
