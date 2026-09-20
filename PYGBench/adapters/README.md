# PYGBench adapter contract

An adapter runs one analyzer on one benchmark and writes one canonical
`result.json` conforming to `../result.schema.json`.

PYGBench invokes an adapter as:

```text
adapter --tool TOOL --case-id ID --source FILE --output FILE \
        [--entry-point NAME ...]
```

Copy `template.py` when integrating another analyzer. The adapter may translate
the analyzer's identifiers and file format, but it must not read ground truth,
add missing facts, suppress predicted facts, or contain case-specific logic.

Canonical results contain PTA sets plus CG, CFG, CDG, and DDG graphs.
Result labels must use the semantic identities defined by PYGBench;
they must never expose a tool's transient numeric IDs. Graph paths are
optional: when omitted, the comparator tests a path through its consecutive
edges. A path-sensitive tool should emit `paths` explicitly when its feasible
paths are stricter than graph reachability.

`cpygraph.py` is a compatibility adapter for the existing six-policy CPyGraph
experiment. New integrations should implement the small canonical contract in
`template.py`; they do not need CPyGraph's compatibility machinery.
