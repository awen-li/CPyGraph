#pragma once

// Public control-flow API. CFGBuilder owns graph construction; the returned
// ControlFlowGraph provides immutable blocks, typed edges, and traversal
// queries while retaining instruction offsets.

#include "analysis/cfg/cfg_builder.h"
#include "analysis/cfg/cfg_refiner.h"
#include "analysis/cfg/control_flow_graph.h"
