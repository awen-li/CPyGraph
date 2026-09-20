# CPyGraph call-graph baseline

PyCG supplies the direct quantitative comparison for call-graph construction.
The experiment uses PyCG's published 112-case micro-benchmark and its
complete/sound case criteria. No baseline is attributed to CPyGraph's PTA,
CFG, CDG, or DDG products because the evaluated tools do not expose the same
native-bytecode contracts.

Create the reusable CPython environments and materialize the pinned PyCG
checkout:

```bash
./scripts/setup_cpython_venvs.sh
./baselines/pycg_micro/setup.sh
```

The default build and baseline locations are under `builds/` and can be
overridden with `CPYGRAPH_CPYTHON_ROOT` and `CPYGRAPH_BASELINE_ROOT`.

Build CPyGraph and run the comparison:

```bash
./scripts/build_cpython_matrix.sh 3.10
python3 baselines/pycg_micro/run.py \
  --tool builds/cpython-3.10/tools/pygCG \
  --output experiments/results/pycg-micro.json
```

The result retains every generated, false-positive, and false-negative edge.
Both tools reach 111/112 complete cases and 103/112 sound cases on the pinned
published suite. See `pycg_micro/README.md` for the exact scoring contract.
