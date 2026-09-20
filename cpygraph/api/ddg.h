#pragma once

// Public data-dependence API. DDGBuilder derives typed def-use edges from a
// CFG; InterproceduralStitcher adds argument and return flows without mixing
// control dependence into the graph.

#include "analysis/ddg/data_dependency_graph.h"
#include "analysis/ddg/ddg_builder.h"
#include "analysis/ddg/interprocedural.h"
