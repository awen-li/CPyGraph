#!/usr/bin/env python3
"""Validate PyCG micro-benchmark edge scoring semantics."""

from __future__ import annotations

from pathlib import Path
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
if str(REPOSITORY_ROOT) not in sys.path:
    sys.path.insert(0, str(REPOSITORY_ROOT))

from baselines.pycg_micro.run import edge_set, generated_edges, summarize


def main() -> int:
    expected = edge_set({"main": ["main.first", "main.second"]})
    generated = generated_edges({
        "edges": [["main", "main.first"], ["main", "main.extra"]]
    })
    assert expected - generated == {("main", "main.second")}
    assert generated - expected == {("main", "main.extra")}

    summary = summarize([
        {"category": "args", "status": "SUCCESS",
         "complete": True, "sound": False},
        {"category": "assignments", "status": "SUCCESS",
         "complete": True, "sound": True},
    ])
    assert summary["cases"] == 2
    assert summary["successful_cases"] == 2
    assert summary["complete_cases"] == 2
    assert summary["sound_cases"] == 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
