# Diagnostic tools

The optional tools are shared-library consumers that compile and analyze a
Python package:

- `pygCG <package-path> [--no-profile]`: compiles the package and reports its PTA-guided call graph;
- `pygCFG <package-path> [--no-profile]`: compiles the package and reports its control-flow graph;
- `pygCDG <package-path> [--no-profile]`: reports block-level control dependence after computing postdominators;
- `pygDDG <package-path> [--no-profile]`: compiles the package and reports its data-dependence graph.

Build them with:

```bash
cmake -S . -B build -DCPYGRAPH_BUILD_TOOLS=ON
cmake --build build
```

Each tool recursively discovers `.py` files (including a conventional `src/`
layout), compiles them with the CPython interpreter linked to CPyGraph, loads
the resulting native code objects, runs the on-the-fly PTA/CG prerequisite,
and prints a compact JSON summary for the requested graph. A tool
built against one CPython minor version intentionally rejects a different
compiler because native `.pyc` layouts are not cross-version inputs.

Profiling is enabled by default. Each JSON result includes per-phase wall
time, process CPU time, and process RSS measurements. Pass `--no-profile` when
measurement overhead is undesirable; the graph analysis itself is unchanged.
