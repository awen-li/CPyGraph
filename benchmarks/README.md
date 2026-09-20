# Frozen real-package benchmark

`package-list.json` is the authoritative frozen experiment manifest (SHA-256
`569ff5a58ef3e46978560d96aabf2af46d1b05175bce67e9dc9c79ca77f95008`).
The subjects and selection rules were fixed before CPyGraph's real-package
runs, avoiding selection in response to analyzer results.

The corpus contains 2,500 unique subjects: 500 for each CPython version from
3.10 through 3.14. For each version, the eligible population was sorted by
non-blank, non-comment physical Python source lines, divided into ten
equal-count size portions, and sampled with a fixed seed at 50 subjects per
portion. Projects and repository URLs are unique across all cohorts.

Eligibility was fixed before CPyGraph evaluation. A subject has a stable
source release uploaded during the frozen three-year window, an explicit
`Requires-Python` constraint compatible with its cohort, a public repository
active in the final 90 days, and Python source that compiles with the assigned
CPython runtime. Each record pins the source-distribution URL and SHA-256
digest and records source files, bytes, and lines. Failed syntax checks were
replaced deterministically from the same size portion.

Materialize or verify all pinned source trees with:

```bash
python3 benchmarks/manage.py \
  --output benchmarks/package-list.json \
  --package-dir benchmarks/packages
```

Validate the manifest structure without downloading packages:

```bash
python3 benchmarks/manage.py \
  --output benchmarks/package-list.json \
  --validate-only
```

When the manifest exists, this command validates it, downloads only missing
archives, verifies every digest, and safely extracts the corresponding source
trees. It does not sample again. `--refresh` intentionally constructs a new
corpus and must not be used for reproducing the paper.

Materialized packages and download caches are ignored by Git. The manifest is
the durable benchmark artifact. Baseline and CPyGraph runners consume each
record's `source_directory`; adding or removing subjects therefore changes
only the manifest and package materialization, not analyzer code.

Run a package through CPyGraph with end-to-end wall time, CPU time, and
aggregate process-tree peak RSS collected together:

```bash
python3 experiments/run.py \
  --tool build/tools/pygDDG \
  --input benchmarks/packages/PACKAGE/source \
  --output experiments/results/cpygraph/PACKAGE
```

The analyzer's per-phase profile remains in `analysis.json`; the externally
measured end-to-end record is written to `run.json`.

For the complete corpus, first create the five version-specific builds and
then invoke the campaign driver:

```bash
./scripts/build_cpython_matrix.sh
python3 experiments/run.py
```

The driver reads every `source_directory` directly from the frozen manifest;
no package list is duplicated in experiment code.
