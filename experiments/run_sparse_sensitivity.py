#!/usr/bin/env python3
"""Repeat the frozen sparse-entry PYGBench sensitivity experiment."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import statistics
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
if str(REPOSITORY_ROOT) not in sys.path:
    sys.path.insert(0, str(REPOSITORY_ROOT))

from PYGBench.groundtruths.evaluate import evaluate


DEFAULT_EXECUTABLE = REPOSITORY_ROOT / "build/pygbench_case_analysis"
DEFAULT_OUTPUT = (
    REPOSITORY_ROOT / "experiments/results/rq3-sparse-sensitivity")
CONFIGURATION = REPOSITORY_ROOT / "experiments/configurations/sparse-entry.json"
POLICIES = ("insensitive", "selective-entry", "complete")
REPETITIONS = 5
TIMEOUT_SECONDS = 1_800
MEMORY_LIMIT_BYTES = 32 * 1_024**3


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def distribution(values: list[float]) -> dict[str, float | int]:
    quartiles = statistics.quantiles(values, n=4, method="inclusive")
    mean = statistics.fmean(values)
    deviation = statistics.pstdev(values)
    return {
        "count": len(values),
        "minimum": min(values),
        "q1": quartiles[0],
        "median": statistics.median(values),
        "q3": quartiles[2],
        "maximum": max(values),
        "iqr": quartiles[2] - quartiles[0],
        "coefficient_of_variation": deviation / mean if mean else 0.0,
    }


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")


def semantic_view(policy: dict[str, object]) -> dict[str, object]:
    return {name: value for name, value in policy.items()
            if name not in {"resources"}}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=DEFAULT_EXECUTABLE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--repetitions", type=int, default=REPETITIONS)
    args = parser.parse_args()
    if args.repetitions < 2:
        parser.error("--repetitions must be at least two")
    executable = args.executable.resolve()
    output = args.output.resolve()
    if not executable.is_file():
        parser.error(f"missing benchmark executable: {executable}")
    configuration = json.loads(CONFIGURATION.read_text(encoding="utf-8"))
    manifest = {
        "schema_version": 1,
        "purpose": "genuinely sparse selective-sensitivity evaluation",
        "executable": str(executable),
        "executable_sha256": sha256(executable),
        "configuration": configuration,
        "configuration_sha256": sha256(CONFIGURATION),
        "policies": list(POLICIES),
        "repetitions": args.repetitions,
        "execution_order": "repetition then policy",
        "timeout_seconds": TIMEOUT_SECONDS,
        "memory_limit_bytes": MEMORY_LIMIT_BYTES,
    }
    write_json(output / "manifest.json", manifest)

    reports = []
    for repetition in range(1, args.repetitions + 1):
        result = evaluate(
            executable, output / f"repeat-{repetition}", TIMEOUT_SECONDS,
            MEMORY_LIMIT_BYTES, policies=POLICIES)
        reports.append(result)
        write_json(output / f"repeat-{repetition}/summary.json", result)
        print(f"[{repetition}/{args.repetitions}] complete", flush=True)

    summaries: dict[str, object] = {}
    for policy in POLICIES:
        values = [report["policies"][policy] for report in reports]
        semantic_hashes = {
            hashlib.sha256(json.dumps(
                semantic_view(value), sort_keys=True,
                separators=(",", ":")).encode()).hexdigest()
            for value in values
        }
        first = values[0]
        summaries[policy] = {
            "semantic_output_stable": len(semantic_hashes) == 1,
            "true_positives": first["true_positives"],
            "false_positives": first["false_positives"],
            "false_negatives": first["false_negatives"],
            "precision": first["precision"],
            "recall": first["recall"],
            "sensitivity_selection": first["sensitivity_selection"],
            "wall_seconds": distribution([
                float(value["resources"]["wall_seconds"])
                for value in values]),
            "peak_rss_bytes": distribution([
                float(value["resources"]["peak_rss_bytes"])
                for value in values]),
        }
    summary = {
        "schema_version": 1,
        "repetitions": args.repetitions,
        "policies": summaries,
        "all_semantic_outputs_stable": all(
            bool(value["semantic_output_stable"])
            for value in summaries.values()),
    }
    write_json(output / "summary.json", summary)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if summary["all_semantic_outputs_stable"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
