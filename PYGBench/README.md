# PYGBench

PYGBench is CPyGraph's semantic correctness and precision microbenchmark. Its
public surface is benchmark programs, machine-readable ground truths, a small
adapter contract, and one runner:

```text
PYGBench/
├── adapters/      adapter contract, template, and CPyGraph integration
├── benchmarks/    201 independently identified Python cases
├── groundtruths/
│   ├── cases/     one explicit, tool-neutral JSON ground truth per case
│   └── artifacts/ expected CG/CFG/CDG/DDG DOT and PTA sets per case
├── result.schema.json  canonical analyzer-result format
├── run.py          single public entry point
└── README.md
```

The benchmark executable is built from `tests/pygbench/case_analysis.cpp` and
links directly to CPyGraph. PYGBench does not invoke the diagnostic graph
tools. Generated observations and reports belong in the build tree and are not
committed as benchmark inputs.

## Benchmark design

PYGBench uses small semantic programs instead of large applications. Each case
isolates one primary challenge so that a failure identifies the missing
language or analysis rule. Multi-file packages are used when imports,
re-exports, package initialization, or cross-module flow are the behavior under
test. Every case is nevertheless analyzed as a package and evaluated for all
five components.

Cases are constructed with the following procedure:

1. Select one Python semantic feature or program-analysis challenge.
2. Write the smallest executable package that preserves that behavior.
3. Manually specify required facts and fault-revealing negative facts for the
   primary challenge.
4. Add analyzer-independent structural facts for PTA, CG, CFG, CDG, and DDG
   so the complete pipeline is scored on every case.
5. Run every analysis policy over the same fixed candidate universe. Analyzer
   output is never used to create ground truth.

The 201 cases are organized by their primary challenge. A category is not an
exclusive component assignment; for example, every `cfg` case also has PTA,
CG, CDG, and DDG ground truth.

| Primary category | Cases | Main purpose |
|---|---:|---|
| Python language | 39 | Scope, binding, object model, expressions, and runtime protocols |
| PTA | 32 | Object identity, aliases, heap fields, containers, and selective sensitivity |
| CFG | 29 | Branches, loops, exceptions, cleanup, and suspension |
| CDG | 15 | Postdominators, branch outcomes, decision trees, short-circuiting, pattern dispatch, loops, and exceptional control dependence |
| Protocol | 22 | Bytecode-visible implicit and special-method calls |
| CG | 22 | Direct, indirect, receiver, closure, recursive, and higher-order calls |
| DDG | 19 | Local, heap, closure, interprocedural, and path-correlated data flow |
| Import | 16 | Package initialization, aliases, re-exports, and cross-module flow |
| Negative | 7 | Unreachable, overwritten, infeasible, shadowed, and unused targets |

### Python language coverage

The corpus covers these language-feature families:

- Imports and packages: absolute and relative imports, module and symbol
  aliases, star imports, re-exports, package initializers, dynamic imports,
  and cross-module calls, returns, inheritance, callbacks, fields, and DDG.
- Functions and scope: positional-only, keyword-only, default, variadic, and
  unpacked arguments; lambdas; decorators; annotations; globals; nonlocals;
  nested functions; closure cells; comprehensions; generators; and recursion.
- Object model: construction, inheritance, `super`, bound methods,
  class/static methods, instance and class fields, `__dict__`, `__slots__`,
  properties, descriptors, metaclasses, and dynamic attribute access.
- Runtime protocols and hidden calls: callable objects, context managers,
  iteration, reversed iteration, subscription, containment, arithmetic,
  reflected operators, comparison and `NotImplemented` fallback, truthiness,
  length, hashing, conversion, and formatting.
- Control and exceptions: conditionals, short-circuit expressions, loops,
  `break`/`continue`, pattern matching, `try`/`except`/`else`/`finally`, handler
  selection, explicit raise, re-raise, exception chaining, and cleanup paths.
- Expressions and storage: assignment expressions, augmented assignment,
  unpacking, slicing, f-strings, comprehensions, containers, strong and weak
  field updates, aliased bases, and nested access paths.

Cases containing async constructs carry the `async-requires-dedicated-model`
marker defined by CPyGraph's
[project-level limitations](../README.md#current-limitations). The six marked
cases are retained as unscored limitation probes; the scored suite contains 195
synchronous cases.

### Program-analysis facts

Each component has a separate semantic contract:

| Component | Ground-truth fact | What it checks |
|---|---|---|
| PTA | subject → abstract object membership | Aliasing, allocations, parameters, returns, fields, containers, captures, and heap updates |
| CG | caller → package-defined callee path | Direct and indirect dispatch, methods, callbacks, recursion, closures, imports, and typed hidden-call groups |
| CFG | semantic block → block path | Normal branches, loops, exceptional successors, handlers, cleanup, returns, and suspension/resume flow |
| CDG | controlling block → dependent block path | True/false, normal/exception, nested branch, loop, and postdominating-join behavior |
| DDG | value producer → consumer path | Local definitions, arguments and returns, globals, closure cells, heap fields, containers, and interprocedural flow |

Required facts measure recall. Negative facts describe plausible but
semantically invalid results and measure precision. CG, CFG, CDG, and DDG use a
unified finite-path representation; PTA uses points-to-set membership. Flow,
context, and path sensitivity are PTA configuration attributes, while field
sensitivity and exception modeling are semantic capabilities tested directly.

Multi-hop coverage is constructed from independently declared semantic edges,
not by enumerating analyzer output. For each graph, the builder composes finite
simple paths containing at most eight nodes; positive and negative paths are
composed separately so one fault cannot create a combinatorial family of mixed
paths. Explicit long positive witnesses also contribute their contiguous
multi-hop subpaths. Dedicated deep direct-call, callback, nested-control,
exception-cleanup, local-data, and interprocedural-field cases exercise these
rules. Direct and multi-hop paths are combined into one graph metric; they are
not presented as separate benchmark scores.

## Ground truth

Every source case has a matching JSON file under `groundtruths/cases/`. Each
case contains a scored candidate set for all five components: PTA, CG, CFG,
CDG, and DDG. It states the required and negative graph nodes/paths and the
required and negative members of each PTA points-to set. The labels describe
program semantics instead of tool-specific node IDs or bytecode offsets, so
another analyzer can reuse the same oracle.

Feature-specific facts are manually curated. When a case has no manual fact
for one of the other components, the ground-truth builder derives a small,
analyzer-independent structural fact from the Python source AST. It never
reads CPyGraph output. This keeps each case focused on one primary challenge
while still testing the complete PTA/CG/CFG/CDG/DDG pipeline.

Every case also has directly consumable artifacts at
`groundtruths/artifacts/<case-id>/`:

```text
cg.dot       expected call graph
cfg.dot      expected control-flow graph
cdg.dot      expected control-dependence graph
ddg.dot      expected data-dependency graph
pta.json     expected and negative members of each points-to set
```

DOT files contain the required semantic nodes and edges with rectangular
nodes. Negative edges use the internal `// forbidden:` assertion marker and
remain in the case JSON; they are not inserted into the expected graph.
CDG edges additionally carry one of `true`, `false`, `normal`, `exception`, or
`resume` as an outcome label, so branch polarity is part of the oracle rather
than inferred from node names.
These compact standalone artifacts use the CPython 3.10 semantics expected by
the baseline comparison environment. The full oracle resolves explicitly
versioned candidates when `run_cross_version.py` evaluates CPython 3.11--3.14;
this accounts for compiler transformations such as PEP 709 comprehension
inlining without duplicating benchmark cases.

The public shape is deliberately small:

```json
{
  "case": "pta.same_object_fields",
  "points_to_sets": [
    {
      "subject": "box.left",
      "points_to": ["first"],
      "must_not_point_to": ["second"]
    }
  ],
  "cg": {
    "nodes": [],
    "required_edges": [],
    "forbidden_edges": [],
    "required_paths": [],
    "forbidden_paths": []
  },
  "cfg": {
    "nodes": [],
    "required_edges": [],
    "forbidden_edges": [],
    "required_paths": [],
    "forbidden_paths": []
  },
  "cdg": {
    "nodes": [],
    "required_edges": [],
    "forbidden_edges": [],
    "required_paths": [],
    "forbidden_paths": []
  },
  "ddg": {
    "nodes": [],
    "required_edges": [],
    "forbidden_edges": [],
    "required_paths": [],
    "forbidden_paths": []
  }
}
```

To extend PYGBench, add the `.py` case and its corresponding ground truth.
Case discovery is data-driven; the runner and scorer require no case-specific
code changes. `python3 PYGBench/run.py ground-truth` regenerates and checks the
normalized files from the curated semantic facts.

The scored universe must be reported independently of internal query grouping:

| Scored population | Expected | Negative | Total |
|---|---:|---:|---:|
| PTA points-to memberships | 131 | 136 | 267 |
| CG paths | 177 | 224 | 401 |
| CFG paths | 277 | 59 | 336 |
| CDG paths | 153 | 212 | 365 |
| DDG paths | 260 | 63 | 323 |
| **All graph paths** | **867** | **558** | **1,425** |
| **All scored candidates** | **998** | **694** | **1,692** |

Here, a negative is a fault-revealing candidate: semantics says it must not be
reported, and its presence is a false positive. It does not mean that the
benchmark program contains a fault. `forbidden_*` is retained only as the
internal ground-truth assertion syntax.

The implementation groups these candidates into 1,305 query groups for
efficient analysis. Query-group count is not a precision or recall denominator.
Papers and result summaries must report the 1,692-candidate synchronous
universe, including the 1,425 graph paths. Every JSON result report also
records these counts in `evaluation_universe`. The 195 scored cases provide 975
targeted component sections: one PTA, CG, CFG, CDG, and DDG section per case.

The scorer uses may-set semantics over each declared candidate domain:

```text
precision = TP / (TP + FP)
recall    = TP / (TP + FN)
```

For a complete run, `expected = TP + FN`, `negative = TN + FP`, and
`total = TP + TN + FP + FN`. Thus the population counts and every reported
precision/recall value can be independently reconciled.

Runtime traces are not used as complete ground truth. Unresolved summaries are
projected only onto compatible, declared package candidates. External/runtime
targets remain outside the benchmark domain.

CG, CFG, CDG, and DDG use the same finite-path semantics. Edges and independently
curated multi-hop paths belong to the same candidate population. A path is present only
when every consecutive semantic edge is present. Expected paths contribute TP
or FN; negative paths contribute FP when present. Paths are explicitly
curated, simple, and finite, so loops never trigger unbounded enumeration. The
same precision/recall equations cover points-to memberships and graph paths.

## Using another analyzer

PYGBench never requires another analyzer to reproduce CPyGraph's internal
node IDs. A tool adapter translates its output once into the canonical format
defined by `result.schema.json`:

```json
{
  "schema_version": 1,
  "case": "cg.higher_order",
  "points_to_sets": [
    {"subject": "value", "points_to": ["allocation"]}
  ],
  "cg": {
    "nodes": ["run", "apply", "increment"],
    "edges": [
      {"source": "run", "target": "apply"},
      {"source": "apply", "target": "increment"}
    ],
    "paths": [["run", "apply", "increment"]]
  },
  "cfg": {"nodes": [], "edges": []},
  "cdg": {
    "nodes": ["condition", "body"],
    "edges": [
      {"source": "condition", "target": "body", "outcome": "true"}
    ]
  },
  "ddg": {"nodes": [], "edges": []}
}
```

`paths` is optional. When absent, the comparator checks consecutive graph
edges; a path-sensitive analyzer can emit explicit feasible paths. Adapters
only run the tool and translate its identifiers and serialization. They must
not inspect expected/forbidden facts or contain case-specific corrections.

Copy `adapters/template.py` to integrate a tool. One adapter is reused for all
cases, including cases added later. Run it and compare in one command:

```bash
python3 PYGBench/run.py evaluate-tool \
    --adapter /path/to/my_adapter.py \
    --tool /path/to/my_analyzer \
    --output-dir results
```

The adapter is invoked independently for each case and receives `--case-id`,
`--source`, repeated `--entry-point`, and `--output`. Existing canonical
results can be compared without running a tool:

```bash
python3 PYGBench/run.py compare --results results
```

Both commands support `--case`, `--category`, and `--tag`. Reports include
per-case diagnostics plus unified PTA/edge/path precision and recall. A missing
or invalid result contributes false negatives instead of disappearing from the
aggregate metric.

## Sensitivity experiment

CPyGraph exposes exactly three configurable PTA sensitivity attributes:
`flow`, `context`, and `path`. Field modeling, receiver dispatch, object
separation, and exception flow are language/analysis features—not additional
sensitivity modes.

The experiment uses the same 1,692 labeled candidates, grouped into 1,305 query
groups, for every policy. The three isolated runs select every function for one
attribute, allowing the effect of each supported attribute to be measured
without relabeling a difficult subset as a policy result.

Only selective mode has a configuration. A per-case JSON configuration has
this format:

```json
{
  "level": "selective",
  "functions": [
    {
      "selector": "*",
      "attributes": ["flow", "context", "path"]
    }
  ]
}
```

`selector` identifies the functions to refine; `*` selects every function in
the case. `attributes` may contain only `flow`, `context`, and `path`.
Insensitive is the default mode and complete sensitivity is selected directly;
neither uses a configuration file.

Results reproduced with CPython 3.10 on 2026-09-19:

Every row below evaluates the same 1,692 candidates (998 expected and 694
negative).

| Analysis policy | TP | TN | FP | FN | Precision | Recall |
|---|---:|---:|---:|---:|---:|---:|
| Insensitive | 998 | 599 | 95 | 0 | 0.913 | 1.000 |
| Selective flow only | 998 | 604 | 90 | 0 | 0.917 | 1.000 |
| Selective context only | 998 | 615 | 79 | 0 | 0.927 | 1.000 |
| Selective path only | 998 | 611 | 83 | 0 | 0.923 | 1.000 |
| Selective declared entries | 998 | 630 | 64 | 0 | 0.940 | 1.000 |
| Selective flow + context + path | 998 | 641 | 53 | 0 | 0.950 | 1.000 |
| Complete sensitivity | 998 | 641 | 53 | 0 | 0.950 | 1.000 |

Complete-sensitivity component results are shown with their full scored
populations. CG, CFG, CDG, and DDG populations are graph paths; PTA's
population is points-to memberships.

| Component | Expected | Negative | Total | TP | TN | FP | FN | Precision | Recall |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| PTA memberships | 131 | 136 | 267 | 131 | 107 | 29 | 0 | 0.819 | 1.000 |
| CG paths | 177 | 224 | 401 | 177 | 222 | 2 | 0 | 0.989 | 1.000 |
| CFG paths | 277 | 59 | 336 | 277 | 43 | 16 | 0 | 0.945 | 1.000 |
| CDG paths | 153 | 212 | 365 | 153 | 212 | 0 | 0 | 1.000 | 1.000 |
| DDG paths | 260 | 63 | 323 | 260 | 57 | 6 | 0 | 0.977 | 1.000 |

All policy rows use the complete benchmark population; no sensitivity-specific
subset is reported. `selective-all` and `complete` must remain identical, and
the runner enforces this invariant. Sensitivity is configured only in PTA, but
every policy is evaluated over the same complete PTA, CG, CFG, CDG, and DDG
ground truth.

## Run

Build the linked benchmark entry and validate the suite:

```bash
cmake -S . -B build -DCPYGRAPH_BUILD_BENCHMARKS=ON
cmake --build build
python3 PYGBench/run.py validate
python3 PYGBench/run.py ground-truth
python3 PYGBench/run.py list --category cfg
```

Run all cases or select one case/category/tag:

```bash
python3 PYGBench/run.py cases \
    --executable build/pygbench_case_analysis
python3 PYGBench/run.py cases \
    --executable build/pygbench_case_analysis \
    --case pta.same_object_fields
```

Run all seven analysis policies and retain raw observations and reports outside
the source directory. This command uses the CPyGraph compatibility adapter;
generic analyzers should use `evaluate-tool` above:

```bash
python3 PYGBench/run.py evaluate \
  --executable build/pygbench_case_analysis \
  --output-dir build/pygbench-results
```

This evaluation measures wall time, CPU time, and aggregate process-tree peak
RSS during the same analyzer invocation used to compute each sensitivity
policy's precision and recall. Per-policy measurements are retained as
`POLICY-resources.json` and included in the final JSON report.

Without `--output-dir`, evaluation uses temporary output. The normal CTest
entry remains:

```bash
ctest --test-dir build -L pygbench --output-on-failure
```

Ground-truth artifacts above are committed benchmark inputs. Add
`--retain-artifacts` to write measured CPyGraph outputs under
`artifacts/<policy>/<case-id>/`: `cg.dot`, `cfg-<code-object>.dot`,
`cdg-<code-object>.dot`, `ddg.dot`, and `pta.json`. Selective result
directories additionally contain the exact
`sensitivity.json` used for that run; insensitive and complete directories do
not.

The measured sparse policy uses
`experiments/configurations/sparse-entry.json`: it assigns all three
attributes to each case's declared non-module entry functions. This rule
selects 218 of 833 loaded function instances and is fixed independently of the
candidate labels.
