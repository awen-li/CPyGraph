#pragma once

// Public package-input API. PackageCompiler recursively compiles Python
// sources with CPyGraph's matching interpreter. PackageLoader decodes those
// code-object trees or loads one matching native .pyc artifact directly.

#include "package/package.h"
#include "package/analysis.h"
#include "package/call_graph.h"
#include "package/graph_builder.h"
