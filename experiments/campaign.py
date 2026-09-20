#!/usr/bin/env python3
"""Run the frozen package corpus with version-matched CPyGraph builds."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys
from typing import Iterable


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = REPOSITORY_ROOT / "benchmarks" / "package-list.json"
DEFAULT_PACKAGE_ROOT = REPOSITORY_ROOT / "benchmarks" / "packages"
DEFAULT_OUTPUT_ROOT = REPOSITORY_ROOT / "experiments" / "results"
DEFAULT_BUILD_ROOT = REPOSITORY_ROOT / "builds"
DEFAULT_CPYTHON_ROOT = REPOSITORY_ROOT / "builds" / "cpython-venvs"
SUPPORTED_VERSIONS = ("3.10", "3.11", "3.12", "3.13", "3.14")
DEFAULT_ANALYZERS = ("cpygraph",)
SUPPORTED_ANALYZERS = frozenset({"cpygraph"})
SUCCESS = "SUCCESS"
RUN_SCHEMA_VERSION = 2
CAMPAIGN_SCHEMA_VERSION = 3
BUILD_SCHEMA_VERSION = 1
CHECKPOINT_INTERVAL = 25
DEFAULT_WORKERS = 1


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def cpu_model() -> str:
    try:
        for line in Path("/proc/cpuinfo").read_text(
                encoding="utf-8").splitlines():
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unknown"


def total_memory_bytes() -> int | None:
    try:
        for line in Path("/proc/meminfo").read_text(
                encoding="utf-8").splitlines():
            if line.startswith("MemTotal:"):
                return int(line.split()[1]) * 1_024
    except (OSError, ValueError, IndexError):
        pass
    return None


def git_revision() -> str:
    return subprocess.run(
        ["git", "-C", str(REPOSITORY_ROOT), "rev-parse", "HEAD"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()


def environment_metadata() -> dict[str, object]:
    compiler = subprocess.run(
        [os.environ.get("CXX", "c++"), "--version"],
        check=False, capture_output=True, text=True,
    )
    cmake = subprocess.run(
        ["cmake", "--version"], check=False, capture_output=True, text=True)
    disk = shutil.disk_usage(REPOSITORY_ROOT)
    return {
        "captured_at": utc_now(),
        "platform": platform.platform(),
        "kernel": platform.release(),
        "machine": platform.machine(),
        "cpu_model": cpu_model(),
        "logical_cpu_count": os.cpu_count(),
        "total_memory_bytes": total_memory_bytes(),
        "driver_python": platform.python_version(),
        "cpygraph_revision": git_revision(),
        "compiler": compiler.stdout.splitlines()[0] if compiler.stdout else None,
        "cmake": cmake.stdout.splitlines()[0] if cmake.stdout else None,
        "storage": {
            "filesystem": str(REPOSITORY_ROOT),
            "total_bytes": disk.total,
            "free_bytes": disk.free,
        },
    }


def load_manifest(path: Path) -> dict[str, object]:
    document = json.loads(path.read_text(encoding="utf-8"))
    benchmarks = document.get("benchmarks")
    if document.get("schema_version") != 5 or not isinstance(benchmarks, dict):
        raise ValueError("unsupported package manifest")
    if tuple(sorted(benchmarks)) != SUPPORTED_VERSIONS:
        raise ValueError("package manifest does not contain CPython 3.10--3.14")
    return document


def records(document: dict[str, object], versions: set[str],
            package_filter: set[str]) -> Iterable[dict[str, object]]:
    benchmarks = document["benchmarks"]
    assert isinstance(benchmarks, dict)
    for version in SUPPORTED_VERSIONS:
        if version not in versions:
            continue
        portions = benchmarks[version]
        if not isinstance(portions, dict):
            raise ValueError(f"invalid CPython {version} cohort")
        for portion in sorted(portions):
            entries = portions[portion]
            if not isinstance(entries, list):
                raise ValueError(f"invalid CPython {version}/{portion} portion")
            for record in sorted(entries, key=lambda item: (
                    str(item["package"]), str(item["version"]))):
                if package_filter and str(record["package"]) not in package_filter:
                    continue
                yield {**record, "cpython": version, "portion": portion}


def analyzers_for(version: str, requested: tuple[str, ...]) -> tuple[str, ...]:
    """Return the CPyGraph analyzers selected for one interpreter version."""

    del version
    return requested


def cpygraph_tool(build_root: Path, version: str) -> Path:
    return build_root / f"cpython-{version}" / "tools" / "pygDDG"


def cpygraph_build_identity(build_root: Path, version: str) -> dict[str, object]:
    executable = cpygraph_tool(build_root, version)
    return {
        "pygddg": str(executable),
        "pygddg_sha256": sha256(executable) if executable.is_file() else None,
    }


def validate_build(build_root: Path, cpython_root: Path, version: str,
                   revision: str) -> Path:
    build = build_root / f"cpython-{version}"
    metadata_path = build / "cpygraph-build.json"
    try:
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise ValueError(
            f"invalid CPython {version} build metadata; rerun "
            f"scripts/build_cpython_matrix.sh: {error}") from error
    executable = cpygraph_tool(build_root, version)
    expected_python = cpython_root / f"cpython-{version}" / "bin" / "python"
    expected = {
        "schema_version": BUILD_SCHEMA_VERSION,
        "cpython": version,
        "python_executable": str(expected_python),
        "source_revision": revision,
        "pygddg": str(executable),
        "pygddg_sha256": sha256(executable) if executable.is_file() else None,
    }
    mismatches = [
        field for field, value in expected.items()
        if metadata.get(field) != value
    ]
    if mismatches:
        raise ValueError(
            f"CPython {version} build does not match this campaign "
            f"({', '.join(mismatches)}); rerun scripts/build_cpython_matrix.sh")
    return executable


def runner_command(analyzer: str, source: Path, output: Path,
                   version: str, build_root: Path,
                   timeout_seconds: int, memory_gib: int) -> list[str]:
    if analyzer != "cpygraph":
        raise ValueError(f"unsupported analyzer: {analyzer}")
    return [
        sys.executable, str(REPOSITORY_ROOT / "experiments" / "run.py"),
        "--tool", str(cpygraph_tool(build_root, version)),
        "--input", str(source), "--output", str(output),
        "--timeout-seconds", str(timeout_seconds),
        "--memory-gib", str(memory_gib),
    ]


def execute_run(record: dict[str, object], analyzer: str,
                package_root: Path, output_root: Path, build_root: Path,
                timeout_seconds: int, memory_gib: int,
                retry_failures: bool) -> tuple[dict[str, object], str]:
    """Execute or resume one independent package/analyzer measurement."""
    source = package_root / str(record["source_directory"])
    run_output = result_directory(output_root, record, analyzer)
    run_path = run_output / "run.json"
    prior = existing_run(run_path, source)
    if prior is not None and not (
            retry_failures and prior.get("status") != SUCCESS):
        return prior, "RESUME"

    run_output.mkdir(parents=True, exist_ok=True)
    command = runner_command(
        analyzer, source, run_output, str(record["cpython"]),
        build_root, timeout_seconds, memory_gib,
    )
    completed = subprocess.run(
        command, cwd=REPOSITORY_ROOT, check=False,
        capture_output=True, text=True,
    )
    (run_output / "driver.stdout.log").write_text(
        completed.stdout, encoding="utf-8")
    (run_output / "driver.stderr.log").write_text(
        completed.stderr, encoding="utf-8")
    run = existing_run(run_path, source)
    if run is None:
        run = {
            "schema_version": RUN_SCHEMA_VERSION,
            "input": str(source),
            "status": "INVALID_OUTPUT",
            "exit_code": completed.returncode,
            "error": "runner did not emit a valid run.json",
        }
        write_json(run_path, run)
    return run, "RUN"


def existing_run(path: Path, source: Path) -> dict[str, object] | None:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError, json.JSONDecodeError):
        return None
    if (not isinstance(value, dict) or
            value.get("schema_version") != RUN_SCHEMA_VERSION or
            value.get("input") != str(source)):
        return None
    return value


def result_directory(output_root: Path, record: dict[str, object],
                     analyzer: str) -> Path:
    return (output_root / f"cpython-{record['cpython']}" /
            str(record["package"]) / str(record["version"]) / analyzer)


def percentile(values: list[float], proportion: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = round((len(ordered) - 1) * proportion)
    return ordered[index]


def metric_summary(values: list[float]) -> dict[str, float | int | None]:
    return {
        "count": len(values),
        "minimum": min(values) if values else None,
        "median": statistics.median(values) if values else None,
        "p95": percentile(values, 0.95),
        "maximum": max(values) if values else None,
    }


def summarize(entries: list[dict[str, object]], planned: int,
              skipped: int) -> dict[str, object]:
    groups: dict[tuple[str, str], list[dict[str, object]]] = defaultdict(list)
    for entry in entries:
        groups[(str(entry["analyzer"]), str(entry["cpython"]))].append(entry)
    summaries: list[dict[str, object]] = []
    for (analyzer, version), group in sorted(groups.items()):
        statuses = Counter(str(entry["status"]) for entry in group)
        successful = [entry for entry in group if entry["status"] == SUCCESS]
        scale_names = (
            "modules", "code_objects", "native_instructions",
            "pta.values", "pta.objects", "pta.constraints",
            "pta.points_to_facts", "pta.solver_iterations",
            "cg.nodes", "cg.edges", "cg.call_sites",
            "cg.resolved_targets", "cg.unresolved_call_sites",
            "cfg.blocks", "cfg.edges", "cdg.blocks", "cdg.edges",
            "ddg.nodes", "ddg.edges",
        )

        def scale_value(entry: dict[str, object], name: str) -> float | None:
            value: object = entry.get("analysis_summary", {})
            for part in name.split("."):
                if not isinstance(value, dict) or part not in value:
                    return None
                value = value[part]
            return float(value) if isinstance(value, (int, float)) else None

        phase_names = sorted({
            str(phase["phase"])
            for entry in successful
            for phase in (
                entry.get("internal_profiling", {}).get("phases", [])
                if isinstance(entry.get("internal_profiling"), dict) else [])
            if isinstance(phase, dict) and "phase" in phase
        })

        def phase_values(phase_name: str, metric: str) -> list[float]:
            values = []
            for entry in successful:
                profiling = entry.get("internal_profiling")
                if not isinstance(profiling, dict):
                    continue
                for phase in profiling.get("phases", []):
                    if (isinstance(phase, dict) and
                            phase.get("phase") == phase_name and
                            isinstance(phase.get(metric), (int, float))):
                        values.append(float(phase[metric]))
                        break
            return values

        summaries.append({
            "analyzer": analyzer,
            "cpython": version,
            "runs": len(group),
            "successes": len(successful),
            "success_rate": len(successful) / len(group) if group else None,
            "statuses": dict(sorted(statuses.items())),
            "wall_seconds": metric_summary([
                float(entry["wall_seconds"]) for entry in successful
            ]),
            "cpu_seconds": metric_summary([
                float(entry["cpu_seconds"]) for entry in successful
            ]),
            "peak_rss_bytes": metric_summary([
                float(entry["peak_rss_bytes"]) for entry in successful
            ]),
            "analysis_scale": {
                name: metric_summary([
                    value for entry in successful
                    if (value := scale_value(entry, name)) is not None
                ])
                for name in scale_names
            },
            "phase_costs": {
                phase: {
                    metric: metric_summary(phase_values(phase, metric))
                    for metric in (
                        "wall_time_ns", "cpu_time_ns", "peak_rss_bytes")
                }
                for phase in phase_names
            },
        })

    def resource_group(group: list[dict[str, object]]) -> dict[str, object]:
        statuses = Counter(str(entry["status"]) for entry in group)
        successful = [entry for entry in group if entry["status"] == SUCCESS]
        return {
            "runs": len(group),
            "successes": len(successful),
            "success_rate": len(successful) / len(group) if group else None,
            "statuses": dict(sorted(statuses.items())),
            "wall_seconds": metric_summary([
                float(entry["wall_seconds"]) for entry in successful]),
            "cpu_seconds": metric_summary([
                float(entry["cpu_seconds"]) for entry in successful]),
            "peak_rss_bytes": metric_summary([
                float(entry["peak_rss_bytes"]) for entry in successful]),
        }

    analyzer_groups: dict[str, list[dict[str, object]]] = defaultdict(list)
    portion_groups: dict[
        tuple[str, str, str], list[dict[str, object]]] = defaultdict(list)
    for entry in entries:
        analyzer = str(entry["analyzer"])
        analyzer_groups[analyzer].append(entry)
        if "portion" in entry:
            portion_groups[(analyzer, str(entry["cpython"]),
                            str(entry["portion"]))].append(entry)
    return {
        "schema_version": CAMPAIGN_SCHEMA_VERSION,
        "updated_at": utc_now(),
        "planned_runs": planned,
        "recorded_runs": len(entries),
        "resumed_runs": skipped,
        "groups": summaries,
        "analyzers": [
            {"analyzer": analyzer, **resource_group(group)}
            for analyzer, group in sorted(analyzer_groups.items())
        ],
        "portions": [
            {"analyzer": analyzer, "cpython": version, "portion": portion,
             **resource_group(group)}
            for (analyzer, version, portion), group in sorted(
                portion_groups.items())
        ],
    }


def write_summary_csv(path: Path, summary: dict[str, object]) -> None:
    fields = (
        "analyzer", "cpython", "runs", "successes", "success_rate",
        "median_wall_seconds", "p95_wall_seconds",
        "median_cpu_seconds", "median_peak_rss_bytes",
    )
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        groups = summary["groups"]
        assert isinstance(groups, list)
        for group in groups:
            assert isinstance(group, dict)
            wall = group["wall_seconds"]
            cpu = group["cpu_seconds"]
            rss = group["peak_rss_bytes"]
            assert isinstance(wall, dict) and isinstance(cpu, dict)
            assert isinstance(rss, dict)
            writer.writerow({
                "analyzer": group["analyzer"],
                "cpython": group["cpython"],
                "runs": group["runs"],
                "successes": group["successes"],
                "success_rate": group["success_rate"],
                "median_wall_seconds": wall["median"],
                "p95_wall_seconds": wall["p95"],
                "median_cpu_seconds": cpu["median"],
                "median_peak_rss_bytes": rss["median"],
            })
    temporary.replace(path)


def parse_analyzers(value: str) -> tuple[str, ...]:
    analyzers = tuple(dict.fromkeys(
        item.strip().lower() for item in value.split(",") if item.strip()))
    invalid = set(analyzers) - SUPPORTED_ANALYZERS
    if invalid or not analyzers:
        raise argparse.ArgumentTypeError(
            "analyzers must be a comma-separated subset of " +
            ",".join(sorted(SUPPORTED_ANALYZERS)))
    return analyzers


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--package-root", type=Path, default=DEFAULT_PACKAGE_ROOT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--build-root", type=Path,
                        default=Path(os.environ.get(
                            "CPYGRAPH_BUILD_ROOT", DEFAULT_BUILD_ROOT)))
    parser.add_argument("--cpython-root", type=Path,
                        default=Path(os.environ.get(
                            "CPYGRAPH_CPYTHON_ROOT", DEFAULT_CPYTHON_ROOT)))
    parser.add_argument("--analyzers", type=parse_analyzers,
                        default=DEFAULT_ANALYZERS)
    parser.add_argument("--python-version", action="append",
                        choices=SUPPORTED_VERSIONS)
    parser.add_argument("--package", action="append", default=[])
    parser.add_argument("--limit", type=int)
    parser.add_argument("--timeout-seconds", type=int, default=1_800)
    parser.add_argument("--memory-gib", type=int, default=32)
    parser.add_argument("--workers", type=int, default=DEFAULT_WORKERS,
                        help="number of package analyses to execute concurrently")
    parser.add_argument(
        "--analysis-revision",
        help="analyzer source revision recorded by an existing campaign")
    parser.add_argument("--retry-failures", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)

    if args.limit is not None and args.limit <= 0:
        parser.error("--limit must be positive")
    if args.timeout_seconds <= 0 or args.memory_gib <= 0 or args.workers <= 0:
        parser.error("timeout, memory limit, and worker count must be positive")
    manifest_path = args.manifest.resolve()
    package_root = args.package_root.resolve()
    output_root = args.output.resolve()
    build_root = args.build_root.resolve()
    cpython_root = args.cpython_root.resolve()
    versions = set(args.python_version or SUPPORTED_VERSIONS)
    document = load_manifest(manifest_path)
    selected = list(records(document, versions, set(args.package)))
    if args.limit is not None:
        selected = selected[:args.limit]
    if not selected:
        parser.error("no packages match the requested selection")

    planned = [
        (record, analyzer)
        for record in selected
        for analyzer in analyzers_for(str(record["cpython"]), args.analyzers)
    ]
    revision = args.analysis_revision or git_revision()
    selection_identity = [
        [record["cpython"], record["package"], record["version"], analyzer]
        for record, analyzer in planned
    ]
    selection_sha256 = hashlib.sha256(
        json.dumps(selection_identity, separators=(",", ":")).encode()
    ).hexdigest()
    required_versions = sorted({
        str(record["cpython"]) for record, analyzer in planned
        if analyzer == "cpygraph"
    })
    build_identities = {
        version: cpygraph_build_identity(build_root, version)
        for version in required_versions
    }

    protocol = {
        "schema_version": CAMPAIGN_SCHEMA_VERSION,
        "created_at": utc_now(),
        "source_revision": revision,
        "manifest": str(manifest_path),
        "manifest_sha256": sha256(manifest_path),
        "package_root": str(package_root),
        "build_root": str(build_root),
        "cpython_root": str(cpython_root),
        "analyzers": list(args.analyzers),
        "python_versions": sorted(versions),
        "selected_packages": len(selected),
        "planned_runs": len(planned),
        "selection_sha256": selection_sha256,
        "execution_order": "manifest-version-portion-package-analyzer",
        "workers": args.workers,
        "runs_per_package_configuration": 1,
        "cpygraph_builds": build_identities,
        "timeout_seconds": args.timeout_seconds,
        "memory_limit_bytes": args.memory_gib * 1_024**3,
    }
    if args.dry_run:
        print(json.dumps(protocol, indent=2, sort_keys=True))
        return 0

    try:
        for version in required_versions:
            validate_build(build_root, cpython_root, version, revision)
    except ValueError as error:
        parser.error(str(error))
    missing_sources = sorted(
        package_root / str(record["source_directory"])
        for record in selected
        if not (package_root / str(record["source_directory"])).is_dir()
    )
    if missing_sources:
        parser.error(
            f"{len(missing_sources)} package sources are missing; run "
            "benchmarks/manage.py to materialize the corpus")

    output_root.mkdir(parents=True, exist_ok=True)
    campaign_path = output_root / "campaign.json"
    if campaign_path.exists():
        prior = json.loads(campaign_path.read_text(encoding="utf-8"))
        stable_fields = (
            "source_revision", "cpygraph_builds", "manifest_sha256",
            "analyzers", "python_versions",
            "selected_packages", "planned_runs", "timeout_seconds",
            "memory_limit_bytes", "selection_sha256", "build_root",
            "cpython_root",
        )
        if any(prior.get(field) != protocol[field] for field in stable_fields):
            parser.error("output contains a different campaign configuration")
    else:
        write_json(campaign_path, protocol)
        write_json(output_root / "environment.json", environment_metadata())

    entries: list[dict[str, object]] = []
    skipped = 0

    def run_planned(item: tuple[dict[str, object], str]) -> \
            tuple[dict[str, object], str]:
        record, analyzer = item
        return execute_run(
            record, analyzer, package_root, output_root, build_root,
            args.timeout_seconds, args.memory_gib,
            args.retry_failures,
        )

    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        outcomes = executor.map(run_planned, planned)
        for index, ((record, analyzer), (run, action)) in enumerate(
                zip(planned, outcomes), 1):
            run_output = result_directory(output_root, record, analyzer)
            if action == "RESUME":
                skipped += 1
            entry = {
                "index": index,
                "analyzer": analyzer,
                "cpython": record["cpython"],
                "portion": record["portion"],
                "package": record["package"],
                "package_version": record["version"],
                "source_directory": record["source_directory"],
                "result_directory": str(run_output.relative_to(output_root)),
                "status": run.get("status", "INVALID_OUTPUT"),
                "wall_seconds": run.get("wall_seconds"),
                "cpu_seconds": run.get("cpu_seconds"),
                "peak_rss_bytes": run.get("peak_rss_bytes"),
                "analysis_summary": run.get("analysis_summary"),
                "internal_profiling": run.get("internal_profiling"),
            }
            entries.append(entry)
            if index % CHECKPOINT_INTERVAL == 0 or index == len(planned):
                with (output_root / "runs.jsonl").open(
                        "w", encoding="utf-8") as stream:
                    for recorded in entries:
                        stream.write(json.dumps(recorded, sort_keys=True) + "\n")
                summary = summarize(entries, len(planned), skipped)
                write_json(output_root / "summary.json", summary)
                write_summary_csv(output_root / "summary.csv", summary)
            print(
                f"[{index}/{len(planned)}] {action} {analyzer} "
                f"CPython {record['cpython']} {record['package']}=={record['version']} "
                f"{entry['status']}",
                flush=True,
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
