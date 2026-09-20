#pragma once

// CPython's PyCmp_* argument values used by COMPARE_OP. Keeping these
// symbolic prevents version adapters and tests from embedding literals.
#define CPYGRAPH_COMPARE_LESS 0
#define CPYGRAPH_COMPARE_LESS_EQUAL 1
#define CPYGRAPH_COMPARE_EQUAL 2
#define CPYGRAPH_COMPARE_NOT_EQUAL 3
#define CPYGRAPH_COMPARE_GREATER 4
#define CPYGRAPH_COMPARE_GREATER_EQUAL 5
