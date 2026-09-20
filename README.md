# CPyGraph

CPyGraph is a C++ foundation for bytecode-level analysis of CPython programs.
It loads native code objects, normalizes version-specific bytecode into a
shared semantic IR, and constructs control-flow, control-dependence, call,
points-to, and data-dependence graphs.

## Highlights

- CPython 3.10–3.14 bytecode adapters;
- static and shared C++ libraries;
- control-flow graph (CFG) construction with exception edges;
- block-level postdominators and control-dependence graphs (CDGs) with typed
  branch outcomes and bidirectional CFG block/instruction mapping;
- inclusion-based Andersen points-to analysis (PTA) with selective
  function-level flow, context, and path sensitivity;
- on-the-fly PTA and call-graph (CG) construction;
- data-dependence graph (DDG) construction;
- package compilation, loading, and analysis;
- default per-phase wall-time, CPU-time, and process-RSS profiling;
- DOT generation with rectangular graph nodes;
- optional `pygCG`, `pygCFG`, `pygCDG`, and `pygDDG` command-line tools;
- component unit tests and the standalone PYGBench semantic suite.

## Analysis model

Version adapters perform only CPython-specific decoding and stack
normalization. Shared analysis code owns common semantics, which minimizes the
work required to support another CPython version.

Call-graph construction and PTA run together to a fixed point. When a new
callable target is discovered, its body is activated, argument and return
constraints are connected, and points-to propagation resumes.

Python operations that hide calls at source level retain their protocol
identity in the semantic IR. Visible user-defined methods such as `__init__`,
`__enter__`, `__exit__`, and `__add__` receive concrete call edges. Remaining
dynamic alternatives use named unresolved callee groups rather than an
undifferentiated unknown edge. Graph nodes and edges store compact numeric
IDs, enums, and method bit masks—not strings.

Package results also carry a `SoundnessCoverage` lattice value. PTA and CG use
the constant-size `ConservativeTop` summary for relations they cannot yet
materialize. CFG, CDG, and DDG use the narrower `TypedUnresolved` value, limiting
uncertainty to the relevant semantic feature instead of widening every
candidate. Consumers union concrete facts with the indicated summary;
avoiding dense fallback edges keeps memory use bounded.

## Current limitations

CPyGraph does not support asynchronous programs in the current version. The
loader recognizes native suspension and resume instructions, but the analysis
does not model event-loop
scheduling, task interleavings, happens-before relations, or control and data
flow between concurrently executing coroutine instances. Analyses that require
those relations need a dedicated async concurrency model above the current
package graphs; synchronous-graph results alone must not be interpreted as a
complete model of async execution.

PYGBench cases containing async constructs carry the
`async-requires-dedicated-model` limitation marker and are excluded from scored
evaluations so they do not broaden CPyGraph's claimed scope.

## Build

Requirements:

- CMake 3.16 or newer;
- a C++17 compiler;
- CPython development headers and library.

```bash
cmake -S . -B build -DCPYGRAPH_BUILD_TOOLS=ON
cmake --build build -j
```

The build produces both `libcpygraph.a` and `libcpygraph.so`. CMake consumers
can link against `CPyGraph::Static` or `CPyGraph::Shared`.

CPyGraph analyzes bytecode for the CPython minor version selected by CMake.
Native `.pyc` files from a different minor version are intentionally rejected.
The package tools also accept one matching `.pyc` file directly. This path
loads the bundled code object without discovering or recompiling adjacent
source files.

## Add a CPython version

Production integration is directory-driven. To add CPython 3.15, create
`cpygraph/bytecode/adapters/python315/` containing:

```text
adapter.h               declares Python315Adapter
adapter.cpp             opcode semantics and version-specific features
opcodes.h               generated CPython 3.15 opcode macros
opcode_overrides.json   optional compatibility-only opcode definitions
```

`Python315Adapter::version()` must return `3.15`. Start its semantic table from
the closest supported release, retain only valid opcodes, and express every raw
opcode through a generated `CPYGRAPH_PY315_*` macro rather than a literal.
Generate the opcode header with the target interpreter:

```bash
python3 tools/generate_opcode_macros.py --only 315 \
    --python315 /path/to/python3.15
```

CMake discovers `python*/adapter.cpp` and generates the factory registry, while
the opcode generator discovers the same directories. Consequently, adding a
production adapter requires no edits to CMake, the factory, the public API, or
the generator. Add version conformance tests under `tests/unit/adapters/`, then
run the adapter and complete suites:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build -L adapters --output-on-failure
ctest --test-dir build --output-on-failure
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

Run a single component suite with a CTest label:

```bash
ctest --test-dir build -L pta --output-on-failure
```

Available labels include `adapters`, `cfg`, `cdg`, `pta`, `cg`, `ddg`, `package`,
`python`, `visualization`, and `pygbench`.

## Evaluation artifacts

Controlled semantic evaluation lives in `PYGBench/`. The independent
real-package corpus is frozen in `benchmarks/package-list.json`; it contains
500 hash-pinned subjects for each CPython version from 3.10 through 3.14 and
can be validated or materialized with `benchmarks/manage.py`.

Create the reusable CPython environments and version-specific builds with:

```bash
./scripts/setup_cpython_venvs.sh
./scripts/build_cpython_matrix.sh
```

Use `experiments/run.py` to launch the complete corpus campaign. The default
RQ4 campaign contains one full-pipeline CPyGraph run for each package. With an
explicit `--tool`, the same entry point runs one package and writes analyzer
output plus an end-to-end wall-time, CPU-time, and aggregate process-tree peak
RSS record.

After materializing `benchmarks/package-list.json`, launch the deterministic
single-run corpus experiment with `python3 experiments/run.py`. It selects
the matching CPython 3.10--3.14 CPyGraph build automatically and schedules
exactly 2,500 CPyGraph runs.

For the shared call-graph edge relation, CPyGraph also runs on PyCG's pinned
112-case published micro-benchmark. PyCG and CPyGraph each reach 111 complete
cases and 103 sound cases under the benchmark's criteria. Setup and scoring
commands are documented in `baselines/pycg_micro/README.md`.

No baseline is attributed to the PTA, CFG, CDG, or DDG products because the
evaluated tools do not expose the same native-bytecode contracts.

## Analyze a package

Each optional tool accepts a Python package directory. It compiles the package
with the configured interpreter, loads the resulting code objects, completes
the PTA/CG prerequisite, and prints a JSON summary.

```bash
./build/tools/pygCG path/to/package
./build/tools/pygCFG path/to/package
./build/tools/pygCDG path/to/package
./build/tools/pygDDG path/to/package
```

The corresponding C++ package pipeline is:

```cpp
#include "api/package.h"

cpygraph::package::PackageCompiler compiler;
auto compiled = compiler.compile("path/to/package");
auto loaded = cpygraph::package::PackageLoader().load(compiled);
auto package = cpygraph::package::PackageAnalyzer().analyze(loaded);

auto result = cpygraph::package::GraphBuilder().analyze(package);
```

`GraphBuilder::analyze` is the multi-product API: it solves the on-the-fly
PTA/CG fixed point once, then constructs CFG, CDG, and DDG products from that
same result. The individual component methods remain available when a client
needs only one product. `pygDDG` uses the coupled API and reports modules, code
objects, native instructions, PTA values/objects/constraints/facts/iterations,
CG targets and unresolved sites, and CFG/CDG/DDG cardinalities in one JSON
record. `pygCDG` remains the focused CDG inspection tool.

## Selective PTA sensitivity

Complete-package sensitivity can improve precision but needlessly multiplies the
cost of analyzing functions that are not relevant to a query. CPyGraph can
instead apply sensitivity at function granularity: keep most of the package
inexpensive and request stronger PTA only for selected numeric code-object IDs.

Three modes use the same graph-building API:

| Mode | Behavior |
| --- | --- |
| `Insensitive` | Default. Every function is flow-, context-, and path-insensitive. |
| `Selective` | Only explicitly configured functions receive their requested sensitivity attributes. |
| `Complete` | Every function receives flow, context, and path sensitivity. |

The selectable attributes are combinable. `Flow` distinguishes local values
by program point, `Context` uses a bounded two-call-site call string, and
`Path` partitions bounded acyclic branch alternatives and implies `Flow`.
Unselected functions remain insensitive and conservatively exchange facts
with sensitive callers and callees.

A selective configuration associates numeric code-object IDs with attributes:

```cpp
cpygraph::PTASensitivityConfiguration sensitivity;
sensitivity.level = cpygraph::PTASensitivityLevel::Selective;
sensitivity.functions = {
    {hot_function_id,
     cpygraph::PTASensitivity::Flow |
         cpygraph::PTASensitivity::Context},
    {branch_heavy_function_id, cpygraph::PTASensitivity::Path},
};

auto precise_cg = cpygraph::package::GraphBuilder().callGraph(
    package, {}, sensitivity);
```

`CallGraph::nodes()` exposes compact numeric node records containing
`code`, `in_degree`, and `out_degree`; `CallGraph::node(code)` provides direct
lookup without adding strings to graph storage. These values are graph facts,
not automatic sensitivity-selection inputs. CPyGraph changes sensitivity only
through the explicit configuration passed to the analysis API.

Complete is selected directly by setting `level` to `Complete`; it has no
per-function configuration. Setting `Selective` with every function configured
for all attributes is required to produce the same result as complete mode.
`maximum_path_variants` bounds path partitioning; remaining branches are
conservatively merged.

Sensitivity changes PTA and therefore the call targets discovered during
on-the-fly CG construction. CFG, CDG, and DDG construction use the same PTA
prerequisite but retain their existing public APIs.

## Profiling

Profiling is enabled by default for both the library and command-line tools.
The package pipeline records compilation, loading, bytecode analysis, PTA/CG,
CFG, CDG, and DDG phases. Reports contain wall and process CPU time plus starting,
ending, peak, and delta process RSS. Memory is sampled every 5 ms by a lazy
background sampler that starts only when the first region begins.

```cpp
#include "api/profile.h"

auto snapshot = cpygraph::profile::report();
cpygraph::profile::disable();
cpygraph::profile::enable();  // Re-enable and reset collected records.
```

The diagnostic tools append a `profiling` object to their JSON output. Pass
`--no-profile` to disable collection for an individual invocation:

```bash
./build/tools/pygCG path/to/package --no-profile
```

Phase and status identities remain compact enums internally; names are
introduced only when a report is rendered. On Linux, RSS covers the CPyGraph
process, while compilation wall time includes waiting for the compiler child
process. The external experiment runner additionally measures aggregate RSS
for the complete analyzer process tree, including compiler subprocesses.

Include `cpygraph/api.h` for the complete public API or an individual
`cpygraph/api/*.h` component header.

## Evaluation drivers

RQ1 is the complete PYGBench evaluation. RQ2 repeats it with every CPython
adapter and additionally writes canonical per-query semantic-consistency and
adapter-size measurements. RQ3 uses controlled compile-time ablations, and RQ4
uses the frozen package campaign:

```bash
python3 PYGBench/run.py evaluate \
  --executable build/pygbench_case_analysis \
  --output-dir experiments/results/pygbench/cpython-3.10
python3 experiments/run_cross_version.py
./scripts/build_ablation_matrix.sh
python3 experiments/run_ablations.py
python3 experiments/run.py
```

The RQ3 matrix changes one mechanism per build: field modeling, bound-method
binding, exception-flow construction, or interprocedural DDG stitching. PTA
sensitivity policies are measured on the unablated build. Version-specific
normalization is evaluated directly in RQ2 because removing the matching
adapter makes native bytecode undecodable rather than yielding a meaningful
analysis ablation.

## Repository layout

```text
cpygraph/          library implementation and public API
cpygraph/analysis/profile/  low-overhead time and process-memory profiling
tests/unit/        component unit tests
PYGBench/          standalone precision/recall and correctness benchmarks
experiments/       unified end-to-end time and process-tree memory collection
baselines/         pinned comparison tools and raw-output runners
benchmarks/        frozen real-package manifest and materialization utility
tests/fixtures/    package-level analysis fixtures
tools/             optional package analysis tools
```
