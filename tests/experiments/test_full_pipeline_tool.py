#!/usr/bin/env python3
"""Check the single-run experiment schema emitted by pygDDG."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess


REQUIRED_PRODUCTS = ("pta", "cg", "cfg", "cdg", "ddg")
FULL_ANALYSIS_SCHEMA_VERSION = 2


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--input", type=Path, required=True)
    arguments = parser.parse_args()
    completed = subprocess.run(
        [str(arguments.tool), str(arguments.input)],
        check=True, capture_output=True, text=True)
    result = json.loads(completed.stdout)
    assert result["analysis_schema_version"] == FULL_ANALYSIS_SCHEMA_VERSION
    assert all(product in result for product in REQUIRED_PRODUCTS)
    assert result["modules"] > 0
    assert result["code_objects"] > 0
    assert result["native_instructions"] > 0
    assert result["pta"]["constraints"] > 0
    assert result["pta"]["solver_iterations"] > 0
    phases = result["profiling"]["phases"]
    coupled = [phase for phase in phases
               if phase["phase"] == "points_to_call_graph"]
    assert len(coupled) == 1
    assert coupled[0]["count"] == 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
