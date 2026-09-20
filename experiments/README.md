# Experiment resource collection

Every measured analyzer invocation records wall time, CPU time, and aggregate
peak RSS in the same `run.json`. Peak RSS is sampled across the analyzer
process and its live descendants, so package-compilation subprocesses are part
of the experiment boundary. The default sampling interval is 10 ms.

Run CPyGraph on one materialized package with:

```bash
python3 experiments/run.py \
  --tool build/tools/pygDDG \
  --input benchmarks/packages/PACKAGE/source \
  --output results/cpygraph/PACKAGE
```

`analysis.json` preserves the analyzer result and its built-in phase profile;
`run.json` preserves the external end-to-end resource record and a normalized
copy of the analysis summary. Use `pygDDG` for the complete
PTA/CG/CFG/CDG/DDG pipeline, or select another diagnostic tool for a
component-specific run. The full pipeline solves PTA/CG once.

PYGBench's `evaluate` command also records a resource file for every
sensitivity policy. Therefore its precision/recall and cost values come from
the same analyzer invocation rather than from separate runs.

## Complete experiment campaign

Build CPyGraph against every reusable CPython environment:

```bash
./scripts/build_cpython_matrix.sh
```

The script creates `builds/cpython-VERSION`, verifies every
expected tool, and records the exact CPython executable, repository revision,
and `pygDDG` digest. The campaign refuses a missing, stale, or mismatched build.

After materializing the frozen corpus, run the complete package experiment:

```bash
python3 experiments/run.py
```

Use `--workers N` to analyze independent packages concurrently. The default is
one worker; `--workers 4` runs four package analyses at a time while retaining
the fixed manifest order in campaign checkpoints.

The campaign follows one fixed manifest order and executes each package/tool
configuration once. CPyGraph runs on all 2,500 packages using the matching
3.10--3.14 build. The campaign therefore contains 2,500 measured invocations.

Every package retains raw output and `run.json`. Campaign-level artifacts are:

- `campaign.json`: immutable selection, limits, and build roots;
- `environment.json`: repository, hardware, OS, and driver runtime;
- `runs.jsonl`: one normalized index entry per run;
- `summary.json` and `summary.csv`: status, success rate, time, and memory.

Restarting the same command validates and reuses existing `run.json` files.
This resume behavior protects a long campaign from interruption; it does not
add another measurement. Use `--retry-failures` only to retry failed runs.
`--dry-run`, `--limit`, `--package`, and `--python-version` support inspection
and smoke tests.

The immutable campaign identity includes the CPyGraph source revision and the
SHA-256 digest of every version-specific `pygDDG` executable. Resume refuses a
different revision or binary, preventing measurements from multiple analyzer
builds from being mixed in one result set.

Supplying `--tool` switches the same entry point to its single-package mode,
which the campaign uses for each measured CPyGraph invocation.

Run the complete semantic suite with every version-matched build using:

```bash
python3 experiments/run_cross_version.py
```

The driver writes `semantic-consistency.json`, comparing canonical query
results on the oracle-invariant query intersection, and records adapter lines
and generated opcode-table entries in `summary.json`.

Build and execute the RQ3 ablation matrix with:

```bash
./scripts/build_ablation_matrix.sh
python3 experiments/run_ablations.py
```

Each ablation build changes exactly one mechanism. The runner preserves every
policy report and resource record, then writes component-level and overall
TP/FP/FN, precision, recall, runtime, and peak RSS to `summary.json`.

Run the bundled-bytecode applicability study (RQ5) with:

```bash
python3 experiments/run_bundled_bytecode.py --workers 4
```

The runner inventories every bundled `.pyc` file, identifies its interpreter
from the native magic number, and analyzes supported CPython 3.10--3.14 files
with the matching build. It passes each artifact directly to `pygDDG`; adjacent
source files are never compiled or substituted. The result directory contains
the frozen inventory, one measured run per supported artifact, component
completion, recovered code-object counts, time, memory, and explicit categories
for unsupported bytecode.

## Focused reproducibility audits

Run the frozen sparse-entry policy five times with:

```bash
python3 experiments/run_sparse_sensitivity.py
```

Run the prespecified RQ4 timing subset with binaries built from the recorded
RQ4 revision and the original build configuration:

```bash
python3 experiments/run_repeated_timing.py
```

Classify adjacent source evidence, runtime-compatible package groups, and the
independent code-object recovery sample for the archived RQ5 run with:

```bash
python3 experiments/audit_bundled_bytecode.py
```
