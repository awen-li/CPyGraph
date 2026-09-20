# PyCG micro-benchmark comparison

This comparison is limited to the call-graph edge relation shared by PyCG and
CPyGraph. It uses the 112 programs and JSON ground truths from the 16 categories
reported in the PyCG ICSE 2021 paper. The later `dynamic` and `external`
categories in the archived repository are not part of the published suite.

Materialize the pinned upstream benchmark:

```bash
./baselines/pycg_micro/setup.sh
```

The setup command also creates an isolated CPython 3.10 runtime for PyCG.

Build CPyGraph against CPython 3.10 in a separate evaluation build directory,
then run:

```bash
python3 baselines/pycg_micro/run.py \
  --tool builds/cpython-3.10/tools/pygCG \
  --output experiments/results/pycg-micro.json
```

The result retains every generated edge, false-positive edge, and
false-negative edge. Following the PyCG paper, a case is *complete* when it has
no false-positive edge and *sound* when it has no false-negative edge.

On the pinned published artifact, both PyCG and CPyGraph reach 111/112
complete cases and 103/112 sound cases. Use the pinned checkout created by
`setup.sh`: later upstream revisions change several ground-truth files.
