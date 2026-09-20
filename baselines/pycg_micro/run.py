#!/usr/bin/env python3
"""Score CPyGraph on the 112-case PyCG ICSE 2021 micro-benchmark."""

from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import subprocess


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BASELINE_ROOT = REPOSITORY_ROOT / "builds" / "baselines"
DEFAULT_BENCHMARK_ROOT = (
    DEFAULT_BASELINE_ROOT / "sources" / "pycg" /
    "micro-benchmark" / "snippets"
)
PUBLISHED_CATEGORIES = (
    "args",
    "assignments",
    "builtins",
    "classes",
    "decorators",
    "dicts",
    "direct_calls",
    "exceptions",
    "functions",
    "generators",
    "imports",
    "kwargs",
    "lambdas",
    "lists",
    "mro",
    "returns",
)
EXPECTED_CASE_COUNT = 112
PYCG_ARTIFACT_COMMIT = "3b37b54f0ba86eb272c5fb70b0d0a3cfccc85986"
PYCG_PUBLISHED_COMPLETE_CASES = 111
PYCG_PUBLISHED_SOUND_CASES = 103
DEFAULT_TIMEOUT_SECONDS = 300
RESULT_SCHEMA_VERSION = 1


Edge = tuple[str, str]


def edge_set(adjacency: dict[str, object]) -> set[Edge]:
    result: set[Edge] = set()
    for caller, targets in adjacency.items():
        if not isinstance(targets, list) or not all(
                isinstance(target, str) for target in targets):
            raise ValueError(f"invalid adjacency list for {caller}")
        result.update((caller, target) for target in targets)
    return result


def generated_edges(document: dict[str, object]) -> set[Edge]:
    raw_edges = document.get("edges")
    if not isinstance(raw_edges, list):
        raise ValueError("pygCG output does not contain an edges list")
    result: set[Edge] = set()
    for edge in raw_edges:
        if (not isinstance(edge, list) or len(edge) != 2 or
                not all(isinstance(name, str) for name in edge)):
            raise ValueError("pygCG emitted an invalid edge")
        result.add((edge[0], edge[1]))
    return result


def case_directories(benchmark_root: Path) -> list[tuple[str, Path]]:
    cases: list[tuple[str, Path]] = []
    for category in PUBLISHED_CATEGORIES:
        category_root = benchmark_root / category
        if not category_root.is_dir():
            raise ValueError(f"missing published PyCG category: {category}")
        for case in sorted(category_root.iterdir()):
            if case.is_dir() and (case / "callgraph.json").is_file():
                cases.append((category, case))
    if len(cases) != EXPECTED_CASE_COUNT:
        raise ValueError(
            f"expected {EXPECTED_CASE_COUNT} published cases, found {len(cases)}")
    return cases


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def score_case(tool: Path, category: str, case: Path,
               timeout_seconds: int) -> dict[str, object]:
    expected_document = json.loads(
        (case / "callgraph.json").read_text(encoding="utf-8"))
    if not isinstance(expected_document, dict):
        raise ValueError(f"invalid ground truth: {case}")
    expected = edge_set(expected_document)
    command = [str(tool), str(case), "--no-profile"]
    try:
        completed = subprocess.run(
            command, check=False, capture_output=True, text=True,
            timeout=timeout_seconds,
        )
    except subprocess.TimeoutExpired as error:
        return {
            "category": category,
            "case": case.name,
            "status": "TIMEOUT",
            "complete": False,
            "sound": False,
            "expected_edges": len(expected),
            "generated_edges": None,
            "false_positive_edges": [],
            "false_negative_edges": [list(edge) for edge in sorted(expected)],
            "error": str(error),
        }

    try:
        if completed.returncode != 0:
            raise ValueError(
                f"pygCG exited with status {completed.returncode}: "
                f"{completed.stderr.strip()}")
        output = json.loads(completed.stdout)
        if not isinstance(output, dict):
            raise ValueError("pygCG output is not a JSON object")
        generated = generated_edges(output)
    except (json.JSONDecodeError, ValueError) as error:
        return {
            "category": category,
            "case": case.name,
            "status": "ANALYSIS_FAILURE",
            "complete": False,
            "sound": False,
            "expected_edges": len(expected),
            "generated_edges": None,
            "false_positive_edges": [],
            "false_negative_edges": [list(edge) for edge in sorted(expected)],
            "error": str(error),
        }

    false_positives = generated - expected
    false_negatives = expected - generated
    return {
        "category": category,
        "case": case.name,
        "status": "SUCCESS",
        # These names follow the PyCG paper: complete means no false positives;
        # sound means no false negatives.
        "complete": not false_positives,
        "sound": not false_negatives,
        "expected_edges": len(expected),
        "generated_edges": len(generated),
        "false_positive_edges": [list(edge) for edge in sorted(false_positives)],
        "false_negative_edges": [list(edge) for edge in sorted(false_negatives)],
    }


def summarize(cases: list[dict[str, object]]) -> dict[str, object]:
    grouped: dict[str, list[dict[str, object]]] = defaultdict(list)
    for case in cases:
        grouped[str(case["category"])].append(case)

    def counts(group: list[dict[str, object]]) -> dict[str, int]:
        return {
            "cases": len(group),
            "successful_cases": sum(case["status"] == "SUCCESS" for case in group),
            "complete_cases": sum(bool(case["complete"]) for case in group),
            "sound_cases": sum(bool(case["sound"]) for case in group),
        }

    return {
        **counts(cases),
        "categories": [
            {"category": category, **counts(grouped[category])}
            for category in PUBLISHED_CATEGORIES
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, required=True,
                        help="version-matched pygCG executable")
    parser.add_argument("--benchmark-root", type=Path,
                        default=DEFAULT_BENCHMARK_ROOT)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout-seconds", type=int,
                        default=DEFAULT_TIMEOUT_SECONDS)
    args = parser.parse_args()
    if args.timeout_seconds <= 0:
        parser.error("--timeout-seconds must be positive")

    tool = args.tool.resolve()
    benchmark_root = args.benchmark_root.resolve()
    if not tool.is_file():
        parser.error(f"pygCG executable does not exist: {tool}")
    try:
        selected = case_directories(benchmark_root)
    except ValueError as error:
        parser.error(str(error))

    cases = [
        score_case(tool, category, case, args.timeout_seconds)
        for category, case in selected
    ]
    document = {
        "schema_version": RESULT_SCHEMA_VERSION,
        "benchmark": "PyCG ICSE 2021 micro-benchmark",
        "benchmark_commit": PYCG_ARTIFACT_COMMIT,
        "tool": str(tool),
        "tool_sha256": sha256(tool),
        "criteria": {
            "complete": "generated edge set is a subset of ground truth",
            "sound": "ground-truth edge set is a subset of generated edges",
        },
        "published_pycg": {
            "cases": EXPECTED_CASE_COUNT,
            "complete_cases": PYCG_PUBLISHED_COMPLETE_CASES,
            "sound_cases": PYCG_PUBLISHED_SOUND_CASES,
        },
        "cpygraph": summarize(cases),
        "cases": cases,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(document["cpygraph"], indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
