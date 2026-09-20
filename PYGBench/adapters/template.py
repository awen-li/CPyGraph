#!/usr/bin/env python3
"""Minimal template for a PYGBench analyzer adapter."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--case-id", required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--entry-point", action="append", default=[])
    return parser.parse_args()


def analyze(arguments: argparse.Namespace) -> dict[str, object]:
    """Run the tool and translate only its identifiers and file format.

    Do not read PYGBench ground truth here. Replace the empty values with the
    tool's canonical semantic nodes, edges, optional feasible paths, and PTA
    sets. See PYGBench/result.schema.json.
    """
    del arguments.tool, arguments.source, arguments.entry_point
    empty_graph = {"nodes": [], "edges": []}
    return {
        "schema_version": 1,
        "case": arguments.case_id,
        "points_to_sets": [],
        "cg": dict(empty_graph),
        "cfg": dict(empty_graph),
        "cdg": dict(empty_graph),
        "ddg": dict(empty_graph),
    }


def main() -> None:
    arguments = parse_arguments()
    result = analyze(arguments)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")


if __name__ == "__main__":
    main()
