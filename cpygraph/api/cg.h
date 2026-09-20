#pragma once

// Public call-graph API. OnTheFlyCallGraphBuilder couples incremental PTA
// propagation with reachable-body activation and call-edge construction.
// CallGraphBuilder remains available for already-solved inputs. Both retain
// explicit unresolved edges when resolution is incomplete.

#include "analysis/cg/call_graph.h"
#include "analysis/cg/on_the_fly.h"
