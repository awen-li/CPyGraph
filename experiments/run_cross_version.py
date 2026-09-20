#!/usr/bin/env python3
"""Evaluate PYGBench with every version-specific CPyGraph build."""

from __future__ import annotations

import argparse
from collections import Counter
import json
import os
from pathlib import Path
import re
import subprocess
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
if str(REPOSITORY_ROOT) not in sys.path:
    sys.path.insert(0, str(REPOSITORY_ROOT))

from experiments.campaign import git_revision, validate_build


DEFAULT_BUILD_ROOT = REPOSITORY_ROOT / "builds"
DEFAULT_CPYTHON_ROOT = REPOSITORY_ROOT / "builds" / "cpython-venvs"
DEFAULT_OUTPUT_ROOT = REPOSITORY_ROOT / "experiments" / "results" / "pygbench"
PYTHON_VERSIONS = ("3.10", "3.11", "3.12", "3.13", "3.14")
POLICIES = (
    "insensitive", "selective-flow", "selective-context",
    "selective-path", "selective-all", "complete",
)
CROSS_VERSION_SCHEMA_VERSION = 2


def source_line_count(path: Path) -> int:
    """Count nonblank, non-comment physical lines in one adapter file."""

    return sum(
        bool(line.strip()) and not line.lstrip().startswith("//")
        for line in path.read_text(encoding="utf-8").splitlines()
    )


def adapter_metrics() -> list[dict[str, object]]:
    adapter_root = REPOSITORY_ROOT / "cpygraph" / "bytecode" / "adapters"
    metrics = []
    previous_opcodes: set[str] | None = None
    for version in PYTHON_VERSIONS:
        compact = version.replace(".", "")
        directory = adapter_root / f"python{compact}"
        files = sorted(directory.glob("*.[ch]*"))
        opcode_path = directory / "opcodes.h"
        opcode_text = opcode_path.read_text(encoding="utf-8")
        opcode_names = {
            match.group(1)
            for match in re.finditer(
                rf"^#define CPYGRAPH_PY{compact}_(\w+)",
                opcode_text, flags=re.MULTILINE)
        }
        implementation = directory / "adapter.cpp"
        interface = directory / "adapter.h"
        implementation_text = implementation.read_text(encoding="utf-8")
        metrics.append({
            "cpython": version,
            "files": len(files),
            "nonblank_noncomment_lines": sum(
                source_line_count(path) for path in files),
            "handwritten_lines": (
                source_line_count(implementation) + source_line_count(interface)),
            "generated_opcode_entries": len(opcode_names),
            "opcode_entries_added_from_previous": (
                len(opcode_names - previous_opcodes)
                if previous_opcodes is not None else None),
            "opcode_entries_removed_from_previous": (
                len(previous_opcodes - opcode_names)
                if previous_opcodes is not None else None),
            "semantic_table_entries": len(re.findall(
                r"\{CPYGRAPH_PY\d+_", implementation_text)),
            "feature_assignments": len(re.findall(
                r"\bresult\.\w+\s*=", implementation_text)),
            "version_specific_overrides": interface.read_text(
                encoding="utf-8").count("override"),
            "implementation_lines": source_line_count(implementation),
            "interface_lines": source_line_count(interface),
        })
        previous_opcodes = opcode_names
    return metrics


def semantic_consistency(output_root: Path,
                         versions: list[str]) -> dict[str, object]:
    """Compare canonical query results, never release-specific opcodes."""

    policies: dict[str, object] = {}
    for policy in POLICIES:
        reports = {
            version: json.loads((
                output_root / f"cpython-{version}" /
                f"{policy}-report.json").read_text(encoding="utf-8"))
            for version in versions
        }
        query_maps = {
            version: {query["id"]: query for query in report["queries"]}
            for version, report in reports.items()
        }
        all_ids = set().union(*(set(values) for values in query_maps.values()))
        common_ids = set.intersection(*(set(values) for values in query_maps.values()))
        invariant_ids = []
        version_dependent_ids = []
        prediction_mismatches = []
        outcome_mismatches = []
        by_component: Counter[str] = Counter()
        mismatches_by_component: Counter[str] = Counter()
        for query_id in sorted(common_ids):
            queries = [query_maps[version][query_id] for version in versions]
            expected = {
                json.dumps(query["expected_objects"], sort_keys=True)
                for query in queries
            }
            if len(expected) != 1:
                version_dependent_ids.append(query_id)
                continue
            invariant_ids.append(query_id)
            component = str(queries[0]["component"])
            by_component[component] += 1
            predictions = {
                json.dumps(query["predicted_objects"], sort_keys=True)
                for query in queries
            }
            if len(predictions) != 1:
                prediction_mismatches.append(query_id)
                mismatches_by_component[component] += 1
            outcomes = {
                (query["status"], query["coverage"],
                 query["sound"], query["passed"])
                for query in queries
            }
            if len(outcomes) != 1:
                outcome_mismatches.append(query_id)
        policies[policy] = {
            "query_ids": len(all_ids),
            "common_query_ids": len(common_ids),
            "invariant_oracle_queries": len(invariant_ids),
            "version_dependent_oracle_queries": len(version_dependent_ids),
            "prediction_consistency": (
                (len(invariant_ids) - len(prediction_mismatches)) /
                len(invariant_ids) if invariant_ids else 1.0),
            "outcome_consistency": (
                (len(invariant_ids) - len(outcome_mismatches)) /
                len(invariant_ids) if invariant_ids else 1.0),
            "queries_by_component": dict(sorted(by_component.items())),
            "prediction_mismatches_by_component": dict(
                sorted(mismatches_by_component.items())),
            "missing_query_ids_by_version": {
                version: sorted(all_ids - set(query_maps[version]))
                for version in versions
                if all_ids - set(query_maps[version])
            },
            "version_dependent_query_ids": version_dependent_ids,
            "prediction_mismatch_query_ids": prediction_mismatches,
            "outcome_mismatch_query_ids": outcome_mismatches,
        }
    return {"versions": versions, "policies": policies}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", type=Path,
                        default=Path(os.environ.get(
                            "CPYGRAPH_BUILD_ROOT", DEFAULT_BUILD_ROOT)))
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--cpython-root", type=Path,
                        default=Path(os.environ.get(
                            "CPYGRAPH_CPYTHON_ROOT", DEFAULT_CPYTHON_ROOT)))
    parser.add_argument("--python-version", action="append",
                        choices=PYTHON_VERSIONS)
    parser.add_argument("--timeout-seconds", type=int, default=1_800)
    parser.add_argument("--memory-gib", type=int, default=32)
    parser.add_argument("--rerun", action="store_true")
    args = parser.parse_args()
    versions = args.python_version or list(PYTHON_VERSIONS)
    output_root = args.output.resolve()
    summaries = []
    build_root = args.build_root.resolve()
    cpython_root = args.cpython_root.resolve()
    revision = git_revision()
    for version in versions:
        validate_build(build_root, cpython_root, version, revision)
        executable = (build_root / f"cpython-{version}" /
                      "pygbench_case_analysis")
        if not executable.is_file():
            parser.error(f"missing CPython {version} benchmark build: {executable}")
        version_output = output_root / f"cpython-{version}"
        version_output.mkdir(parents=True, exist_ok=True)
        result_path = version_output / "result.json"
        if result_path.is_file() and not args.rerun:
            result = json.loads(result_path.read_text(encoding="utf-8"))
            if result.get("python_version") != version:
                raise RuntimeError(
                    f"stored CPython {version} result reports "
                    f"{result.get('python_version')}")
            summaries.append({
                "cpython": version,
                "status": result["status"],
                "policies": result["policies"],
            })
            print(f"CPython {version}: RESUME {result['status']}", flush=True)
            continue
        completed = subprocess.run([
            sys.executable, str(REPOSITORY_ROOT / "PYGBench" / "run.py"),
            "evaluate", "--executable", str(executable),
            "--output-dir", str(version_output),
            "--timeout-seconds", str(args.timeout_seconds),
            "--memory-gib", str(args.memory_gib),
        ], cwd=REPOSITORY_ROOT, check=False, capture_output=True, text=True)
        (version_output / "stdout.log").write_text(
            completed.stdout, encoding="utf-8")
        (version_output / "stderr.log").write_text(
            completed.stderr, encoding="utf-8")
        if completed.returncode != 0:
            raise RuntimeError(
                f"CPython {version} PYGBench failed: " +
                (completed.stderr.strip() or "no diagnostic"))
        result = json.loads(completed.stdout)
        if result.get("python_version") != version:
            raise RuntimeError(
                f"CPython {version} build reported {result.get('python_version')}")
        result_path.write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        summaries.append({
            "cpython": version,
            "status": result["status"],
            "policies": result["policies"],
        })
        print(f"CPython {version}: {result['status']}", flush=True)
    consistency = semantic_consistency(output_root, versions)
    (output_root / "semantic-consistency.json").write_text(
        json.dumps(consistency, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")
    (output_root / "summary.json").write_text(
        json.dumps({"schema_version": CROSS_VERSION_SCHEMA_VERSION,
                    "versions": summaries,
                    "semantic_consistency": consistency,
                    "adapter_extension_metrics": adapter_metrics()},
                   indent=2, sort_keys=True) + "\n",
        encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
