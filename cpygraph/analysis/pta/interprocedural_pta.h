#pragma once

#include "analysis/pta/pointer_analysis.h"
#include "analysis/argument_binding.h"

#include <vector>

namespace cpygraph {

struct ResolvedPTACallFlow {
    // Each positional argument can have multiple reaching definitions.
    std::vector<std::vector<NodeId>> actual_arguments;
    std::vector<NodeId> formal_parameters;
    std::vector<NodeId> return_values;
    NodeId call_result{};
    std::vector<ArgumentBinding> argument_bindings;
};

class InterproceduralPTAStitcher {
public:
    explicit InterproceduralPTAStitcher(PointerAnalysis& analysis) noexcept
        : analysis_(analysis) {}
    void stitch(const std::vector<ResolvedPTACallFlow>& calls);

private:
    PointerAnalysis& analysis_;
};

}  // namespace cpygraph
