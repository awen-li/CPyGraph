#!/usr/bin/env python3
"""Repeat RQ4 measurements on a fixed version-and-size-stratified subset."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BASELINE_RUNS = (
    REPOSITORY_ROOT / "experiments/results/rq4/runs.jsonl")
DEFAULT_PACKAGE_ROOT = REPOSITORY_ROOT / "benchmarks/packages"
DEFAULT_BUILD_ROOT = REPOSITORY_ROOT / "builds"
DEFAULT_OUTPUT = (
    REPOSITORY_ROOT / "experiments/results/rq4-repeated-timing")
SUPPORTED_VERSIONS = ("3.10", "3.11", "3.12", "3.13", "3.14")
STRATA = 3
REPETITIONS = 5
TIMEOUT_SECONDS = 1_800
MEMORY_GIB = 32
SUCCESS = "SUCCESS"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1_048_576), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")


def load_successes(path: Path) -> dict[str, list[dict[str, object]]]:
    result = {version: [] for version in SUPPORTED_VERSIONS}
    for line in path.read_text(encoding="utf-8").splitlines():
        record = json.loads(line)
        summary = record.get("analysis_summary")
        instructions = (summary.get("native_instructions")
                        if isinstance(summary, dict) else None)
        version = record.get("cpython")
        if (record.get("status") == SUCCESS and version in result and
                isinstance(instructions, int)):
            result[str(version)].append(record)
    return result


def select_subset(path: Path) -> list[dict[str, object]]:
    selected: list[dict[str, object]] = []
    for version, records in load_successes(path).items():
        records.sort(key=lambda record: (
            int(record["analysis_summary"]["native_instructions"]),
            str(record["package"]), str(record["package_version"])))
        if len(records) < STRATA:
            raise ValueError(f"CPython {version} has too few successful runs")
        for stratum in range(STRATA):
            lower = len(records) * stratum // STRATA
            upper = len(records) * (stratum + 1) // STRATA
            group = records[lower:upper]
            chosen = group[len(group) // 2]
            selected.append({
                "cpython": version,
                "stratum": stratum + 1,
                "stratum_population": len(group),
                "package": chosen["package"],
                "package_version": chosen["package_version"],
                "source_directory": chosen["source_directory"],
                "native_instructions": chosen["analysis_summary"][
                    "native_instructions"],
                "original_wall_seconds": chosen["wall_seconds"],
                "original_peak_rss_bytes": chosen["peak_rss_bytes"],
            })
    return selected


def distribution(values: list[float]) -> dict[str, float]:
    if not values:
        return {}
    quartiles = statistics.quantiles(values, n=4, method="inclusive")
    mean = statistics.fmean(values)
    deviation = statistics.pstdev(values)
    return {
        "count": float(len(values)),
        "minimum": min(values),
        "q1": quartiles[0],
        "median": statistics.median(values),
        "q3": quartiles[2],
        "maximum": max(values),
        "iqr": quartiles[2] - quartiles[0],
        "mean": mean,
        "population_standard_deviation": deviation,
        "coefficient_of_variation": deviation / mean if mean else 0.0,
    }


def semantic_digest(summary: object) -> str:
    encoded = json.dumps(
        summary, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-runs", type=Path,
                        default=DEFAULT_BASELINE_RUNS)
    parser.add_argument("--package-root", type=Path,
                        default=DEFAULT_PACKAGE_ROOT)
    parser.add_argument("--build-root", type=Path, default=DEFAULT_BUILD_ROOT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--repetitions", type=int, default=REPETITIONS)
    args = parser.parse_args()
    if args.repetitions < 2:
        parser.error("--repetitions must be at least two")

    baseline_runs = args.baseline_runs.resolve()
    package_root = args.package_root.resolve()
    build_root = args.build_root.resolve()
    output = args.output.resolve()
    selection = select_subset(baseline_runs)
    tools = {
        version: build_root / f"cpython-{version}" / "tools" / "pygDDG"
        for version in SUPPORTED_VERSIONS
    }
    missing = [str(tool) for tool in tools.values() if not tool.is_file()]
    if missing:
        parser.error("missing version-specific tools: " + ", ".join(missing))
    missing_sources = [
        str(package_root / str(record["source_directory"]))
        for record in selection
        if not (package_root / str(record["source_directory"])).is_dir()
    ]
    if missing_sources:
        parser.error("missing selected sources: " + ", ".join(missing_sources))

    manifest = {
        "schema_version": 1,
        "purpose": "RQ4 timing-variability audit",
        "source_revision": subprocess.run(
            ["git", "-C", str(REPOSITORY_ROOT), "rev-parse", "HEAD"],
            check=True, capture_output=True, text=True,
        ).stdout.strip(),
        "build_configuration": (
            "CMake default with CMAKE_BUILD_TYPE unset, matching "
            "scripts/build_cpython_matrix.sh at the recorded revision"),
        "baseline_runs": str(baseline_runs),
        "baseline_runs_sha256": sha256(baseline_runs),
        "selection_rule": (
            "For each CPython version, sort successful RQ4 runs by native "
            "instruction count, split them into three equal-count strata, "
            "and select the median record in each stratum."),
        "execution_order": "repetition, CPython version, size stratum",
        "execution_mode": "serial",
        "repetitions": args.repetitions,
        "timeout_seconds": TIMEOUT_SECONDS,
        "memory_limit_bytes": MEMORY_GIB * 1_024**3,
        "tools": {
            version: {"path": str(tool), "sha256": sha256(tool)}
            for version, tool in tools.items()
        },
        "selection": selection,
    }
    output.mkdir(parents=True, exist_ok=True)
    manifest_path = output / "manifest.json"
    if manifest_path.exists():
        prior = json.loads(manifest_path.read_text(encoding="utf-8"))
        comparable = dict(prior)
        comparable.pop("completed_runs", None)
        if comparable != manifest:
            parser.error("output contains a different timing protocol")
    else:
        write_json(manifest_path, manifest)

    runs: list[dict[str, object]] = []
    total = args.repetitions * len(selection)
    for repetition in range(1, args.repetitions + 1):
        for record in selection:
            version = str(record["cpython"])
            run_output = (
                output / "raw" / f"repeat-{repetition}" /
                f"cpython-{version}" /
                f"stratum-{record['stratum']}")
            run_path = run_output / "run.json"
            if run_path.is_file():
                run = json.loads(run_path.read_text(encoding="utf-8"))
                action = "RESUME"
            else:
                command = [
                    sys.executable, str(REPOSITORY_ROOT / "experiments/run.py"),
                    "--tool", str(tools[version]),
                    "--input", str(package_root / str(record["source_directory"])),
                    "--output", str(run_output),
                    "--timeout-seconds", str(TIMEOUT_SECONDS),
                    "--memory-gib", str(MEMORY_GIB),
                ]
                completed = subprocess.run(
                    command, cwd=REPOSITORY_ROOT, check=False)
                if not run_path.is_file():
                    raise RuntimeError(
                        f"measurement runner produced no record: {command}")
                run = json.loads(run_path.read_text(encoding="utf-8"))
                action = "RUN"
                if completed.returncode != 0 and run.get("status") == SUCCESS:
                    raise RuntimeError("runner status contradicts exit code")
            entry = {
                **record,
                "repetition": repetition,
                "status": run.get("status"),
                "wall_seconds": run.get("wall_seconds"),
                "cpu_seconds": run.get("cpu_seconds"),
                "peak_rss_bytes": run.get("peak_rss_bytes"),
                "analysis_summary_sha256": semantic_digest(
                    run.get("analysis_summary")),
                "result_directory": str(run_output.relative_to(output)),
            }
            runs.append(entry)
            with (output / "runs.jsonl").open("w", encoding="utf-8") as stream:
                for item in runs:
                    stream.write(json.dumps(item, sort_keys=True) + "\n")
            print(
                f"[{len(runs)}/{total}] {action} CPython {version} "
                f"stratum {record['stratum']} {record['package']} "
                f"{entry['status']}", flush=True)

    successes = [record for record in runs if record["status"] == SUCCESS]
    groups = []
    for selected in selection:
        matching = [record for record in successes if
                    record["cpython"] == selected["cpython"] and
                    record["stratum"] == selected["stratum"]]
        digests = sorted({str(record["analysis_summary_sha256"])
                          for record in matching})
        groups.append({
            **selected,
            "successful_repetitions": len(matching),
            "semantic_output_stable": len(digests) == 1,
            "analysis_summary_sha256": digests,
            "wall_seconds": distribution([
                float(record["wall_seconds"]) for record in matching]),
            "peak_rss_bytes": distribution([
                float(record["peak_rss_bytes"]) for record in matching]),
        })
    summary = {
        "schema_version": 1,
        "planned_runs": total,
        "successful_runs": len(successes),
        "failed_runs": total - len(successes),
        "all_semantic_outputs_stable": all(
            bool(group["semantic_output_stable"]) for group in groups),
        "groups": groups,
        "all_successful_runs": {
            "wall_seconds": distribution([
                float(record["wall_seconds"]) for record in successes]),
            "peak_rss_bytes": distribution([
                float(record["peak_rss_bytes"]) for record in successes]),
        },
    }
    write_json(output / "summary.json", summary)
    write_json(manifest_path, {**manifest, "completed_runs": len(runs)})
    if len(successes) != total or not summary["all_semantic_outputs_stable"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
