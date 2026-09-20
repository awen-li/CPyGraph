#!/usr/bin/env python3
"""Run a canonical PYGBench adapter over selected cases and compare results."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

from compare import (add_counts, compare_case, empty_counts, finish,
                     labeled_candidate_universe, missing_result_counts)


GROUND_TRUTHS = Path(__file__).resolve().parent
ROOT = GROUND_TRUTHS.parent


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adapter", type=Path, required=True)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--case")
    parser.add_argument("--category")
    parser.add_argument("--tag")
    parser.add_argument("--json", action="store_true")
    return parser.parse_args()


def adapter_command(adapter: Path) -> list[str]:
    return ([sys.executable, str(adapter)] if adapter.suffix == ".py"
            else [str(adapter)])


def main() -> int:
    arguments = parse_arguments()
    adapter = arguments.adapter.resolve()
    tool = arguments.tool.resolve()
    if not adapter.is_file():
        raise SystemExit(f"adapter does not exist: {adapter}")
    if not tool.is_file():
        raise SystemExit(f"analyzer does not exist: {tool}")
    catalog = json.loads(
        (GROUND_TRUTHS / "oracle.json").read_text(encoding="utf-8"))
    selected = [
        case for case in catalog["cases"]
        if (arguments.case is None or case["id"] == arguments.case)
        and (arguments.category is None or case["category"] == arguments.category)
        and (arguments.tag is None or arguments.tag in case["tags"])
    ]
    if not selected:
        raise SystemExit("no PYGBench cases match the requested filters")
    output_root = arguments.output_dir.resolve()
    results: list[dict[str, object]] = []
    totals = empty_counts()
    for index, metadata in enumerate(selected, 1):
        case_id = str(metadata["id"])
        expected = json.loads(
            (GROUND_TRUTHS / "cases" / f"{case_id}.json")
            .read_text(encoding="utf-8"))
        result_path = output_root / case_id / "result.json"
        command = [
            *adapter_command(adapter),
            "--tool", str(tool),
            "--case-id", case_id,
            "--source", str(ROOT / str(metadata["source"])),
            "--output", str(result_path),
        ]
        for entry_point in metadata["entry_points"]:
            command.extend(("--entry-point", str(entry_point)))
        completed = subprocess.run(
            command, check=False, capture_output=True, text=True)
        if completed.returncode != 0:
            counts = missing_result_counts(expected)
            result = {
                "case": case_id,
                "passed": False,
                "error": completed.stderr.strip() or "adapter failed",
                "overall": finish(counts),
            }
        else:
            try:
                actual = json.loads(result_path.read_text(encoding="utf-8"))
                result = compare_case(expected, actual)
            except (OSError, ValueError, json.JSONDecodeError) as error:
                counts = missing_result_counts(expected)
                result = {"case": case_id, "passed": False,
                          "error": str(error), "overall": finish(counts)}
        add_counts(totals, result["overall"])
        results.append(result)
        if not arguments.json:
            print(f"[{index}/{len(selected)}] "
                  f"{'PASS' if result['passed'] else 'FAIL'} {case_id}")
    failed = sum(not result["passed"] for result in results)
    report = {
        "schema_version": 1,
        "suite": "PYGBench",
        "adapter": str(adapter),
        "tool": str(tool),
        "cases": len(results),
        "passed": len(results) - failed,
        "failed": failed,
        "evaluation_universe": labeled_candidate_universe([
            json.loads(
                (GROUND_TRUTHS / "cases" / f"{metadata['id']}.json")
                .read_text(encoding="utf-8"))
            for metadata in selected
        ]),
        "overall": finish(totals),
        "results": results,
    }
    report_path = output_root / "report.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")
    if arguments.json:
        print(json.dumps(report, sort_keys=True))
    else:
        metrics = report["overall"]
        universe = report["evaluation_universe"]
        candidates = universe["all_scored_candidates"]
        paths = universe["graph_paths"]["all"]
        print(f"PYGBench: {report['passed']}/{report['cases']} passed; "
              f"candidates={candidates['total']} "
              f"(expected={candidates['expected']}, "
              f"negative={candidates['negative']}); "
              f"graph-paths={paths['total']}; "
              f"precision={metrics['precision']:.3f}, "
              f"recall={metrics['recall']:.3f}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
