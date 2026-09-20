#pragma once

#include "analysis/cfg/control_flow_graph.h"

namespace cpygraph::cfg {

class CFGBuilder {
public:
    ControlFlowGraph build(
        bytecode::SemanticProgram program,
        const std::vector<bytecode::ExceptionRegion>& exception_regions = {}) const;
};

}  // namespace cpygraph::cfg
