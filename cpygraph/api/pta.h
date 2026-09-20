#pragma once

// Public points-to API. PointerAnalysis is the implementation-independent
// contract; AndersenPointerAnalysis is the default inclusion-based solver.
// Constraint builders and stitchers connect bytecode analysis across calls.

#include "analysis/argument_binding.h"
#include "analysis/pta/andersen.h"
#include "analysis/pta/import_alias_model.h"
#include "analysis/pta/interprocedural_pta.h"
#include "analysis/pta/pointer_analysis.h"
#include "analysis/pta/sensitivity.h"
#include "analysis/pta/semantic_pta.h"
