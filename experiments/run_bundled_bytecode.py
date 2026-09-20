#!/usr/bin/env python3
"""Run RQ5 on bundled native bytecode from the frozen package corpus."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
import csv
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Iterable


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
if str(REPOSITORY_ROOT) not in sys.path:
    sys.path.insert(0, str(REPOSITORY_ROOT))

from experiments.campaign import (
    DEFAULT_BUILD_ROOT,
    DEFAULT_CPYTHON_ROOT,
    SUPPORTED_VERSIONS,
    git_revision,
    metric_summary,
    sha256,
    utc_now,
    validate_build,
    write_json,
)
from experiments.run import DEFAULT_MEMORY_GIB, DEFAULT_TIMEOUT_SECONDS


DEFAULT_PACKAGE_ROOT = REPOSITORY_ROOT / "benchmarks" / "packages"
DEFAULT_OUTPUT_ROOT = REPOSITORY_ROOT / "experiments" / "results"
DEFAULT_WORKERS = 4
PYC_HEADER_BYTES = 16
MAGIC_BYTES = 4
RUN_SCHEMA_VERSION = 2
RQ5_SCHEMA_VERSION = 1
SUCCESS = "SUCCESS"
CLASS_SUPPORTED = "SUPPORTED"
CLASS_INVALID_HEADER = "INVALID_HEADER"
CLASS_UNSUPPORTED_CPYTHON = "UNSUPPORTED_CPYTHON"
CLASS_UNSUPPORTED_PYPY = "UNSUPPORTED_PYPY"
CLASS_UNKNOWN = "UNKNOWN_BYTECODE"
CPYTHON_TAG = re.compile(r"\.cpython-(\d+)")
PYPY_TAG = re.compile(r"\.pypy")


def runtime_magic(python_executable: Path) -> str:
    completed = subprocess.run(
        [str(python_executable), "-c",
         "import importlib.util; print(importlib.util.MAGIC_NUMBER.hex())"],
        check=True, capture_output=True, text=True,
    )
    return completed.stdout.strip()


def supported_magics(cpython_root: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for version in SUPPORTED_VERSIONS:
        executable = cpython_root / f"cpython-{version}" / "bin" / "python"
        if not executable.is_file():
            raise ValueError(f"missing CPython {version} executable: {executable}")
        magic = runtime_magic(executable)
        if magic in result:
            raise ValueError(f"duplicate bytecode magic for CPython {version}")
        result[magic] = version
    return result


def tagged_version(path: Path) -> str | None:
    match = CPYTHON_TAG.search(path.name)
    if match is None:
        return None
    digits = match.group(1)
    if len(digits) < 2:
        return None
    return f"{digits[0]}.{digits[1:]}"


def classify_artifact(path: Path, magic_versions: dict[str, str]) -> dict[str, object]:
    with path.open("rb") as stream:
        data = stream.read(PYC_HEADER_BYTES)
    tag = tagged_version(path)
    if len(data) < PYC_HEADER_BYTES:
        return {
            "classification": CLASS_INVALID_HEADER,
            "magic": data[:MAGIC_BYTES].hex(),
            "python_version": None,
            "tagged_version": tag,
            "tag_matches_magic": None,
        }
    magic = data[:MAGIC_BYTES].hex()
    version = magic_versions.get(magic)
    if version is not None:
        return {
            "classification": CLASS_SUPPORTED,
            "magic": magic,
            "python_version": version,
            "tagged_version": tag,
            "tag_matches_magic": None if tag is None else tag == version,
        }
    if PYPY_TAG.search(path.name):
        classification = CLASS_UNSUPPORTED_PYPY
    elif tag is not None:
        classification = CLASS_UNSUPPORTED_CPYTHON
    else:
        classification = CLASS_UNKNOWN
    return {
        "classification": classification,
        "magic": magic,
        "python_version": None,
        "tagged_version": tag,
        "tag_matches_magic": None,
    }


def package_identity(relative: Path) -> str:
    parts = relative.parts
    package_parts = parts[:3] if len(parts) >= 3 else parts
    return "/".join(package_parts)


def inventory(package_root: Path, magic_versions: dict[str, str]) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for index, path in enumerate(sorted(package_root.rglob("*.pyc")), start=1):
        relative = path.relative_to(package_root)
        classification = classify_artifact(path, magic_versions)
        records.append({
            "index": index,
            "path": str(path.resolve()),
            "relative_path": relative.as_posix(),
            "package": package_identity(relative),
            "size_bytes": path.stat().st_size,
            "sha256": sha256(path),
            **classification,
        })
    return records


def result_directory(output_root: Path, record: dict[str, object]) -> Path:
    key = str(record["sha256"])[:16]
    return output_root / "files" / f"{int(record['index']):04d}-{key}"


def existing_run(path: Path, source: Path) -> dict[str, object] | None:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError, json.JSONDecodeError):
        return None
    if (not isinstance(value, dict) or
            value.get("schema_version") != RUN_SCHEMA_VERSION or
            value.get("input") != str(source.resolve())):
        return None
    return value


def analyze_artifact(record: dict[str, object], output_root: Path,
                     tools: dict[str, Path], timeout_seconds: int,
                     memory_gib: int) -> dict[str, object]:
    source = Path(str(record["path"]))
    version = str(record["python_version"])
    output = result_directory(output_root, record)
    output.mkdir(parents=True, exist_ok=True)
    run_path = output / "run.json"
    run = existing_run(run_path, source)
    action = "RESUME"
    if run is None:
        command = [
            sys.executable, str(REPOSITORY_ROOT / "experiments" / "run.py"),
            "--tool", str(tools[version]), "--input", str(source),
            "--output", str(output), "--timeout-seconds", str(timeout_seconds),
            "--memory-gib", str(memory_gib),
        ]
        completed = subprocess.run(
            command, cwd=REPOSITORY_ROOT, check=False,
            capture_output=True, text=True,
        )
        (output / "driver.stdout.log").write_text(
            completed.stdout, encoding="utf-8")
        (output / "driver.stderr.log").write_text(
            completed.stderr, encoding="utf-8")
        run = existing_run(run_path, source)
        action = "RUN"
    if run is None:
        run = {
            "schema_version": RUN_SCHEMA_VERSION,
            "input": str(source.resolve()),
            "status": "INVALID_OUTPUT",
            "error": "runner did not emit a valid run.json",
        }
        write_json(run_path, run)
    return {
        **record,
        "action": action,
        "result_directory": str(output),
        "status": run.get("status"),
        "wall_seconds": run.get("wall_seconds"),
        "cpu_seconds": run.get("cpu_seconds"),
        "peak_rss_bytes": run.get("peak_rss_bytes"),
        "analysis_summary": run.get("analysis_summary"),
    }


def component_completion(entries: Iterable[dict[str, object]]) -> dict[str, object]:
    required = {
        "bytecode": "code_objects",
        "pta": "pta",
        "cg": "cg",
        "cfg": "cfg",
        "cdg": "cdg",
        "ddg": "ddg",
    }
    entries = list(entries)
    result: dict[str, object] = {}
    for component, field in required.items():
        complete = sum(
            isinstance(entry.get("analysis_summary"), dict) and
            field in entry["analysis_summary"]
            for entry in entries
        )
        result[component] = {
            "complete": complete,
            "total": len(entries),
            "rate": complete / len(entries) if entries else None,
        }
    return result


def summarize(records: list[dict[str, object]],
              runs: list[dict[str, object]]) -> dict[str, object]:
    supported = [record for record in records
                 if record["classification"] == CLASS_SUPPORTED]
    successful = [run for run in runs if run["status"] == SUCCESS]
    versions: dict[str, object] = {}
    for version in SUPPORTED_VERSIONS:
        group = [run for run in runs if run["python_version"] == version]
        good = [run for run in group if run["status"] == SUCCESS]
        versions[version] = {
            "files": len(group),
            "statuses": dict(sorted(Counter(
                str(run["status"]) for run in group).items())),
            "wall_seconds": metric_summary([
                float(run["wall_seconds"]) for run in good]),
            "peak_rss_bytes": metric_summary([
                float(run["peak_rss_bytes"]) for run in good]),
        }
    code_objects = [
        float(run["analysis_summary"]["code_objects"])
        for run in successful
        if isinstance(run.get("analysis_summary"), dict)
    ]
    return {
        "schema_version": RQ5_SCHEMA_VERSION,
        "updated_at": utc_now(),
        "packages_with_bytecode": len({str(record["package"])
                                        for record in records}),
        "total_bytecode_files": len(records),
        "classifications": dict(sorted(Counter(
            str(record["classification"]) for record in records).items())),
        "supported_files": len(supported),
        "tag_mismatches": sum(
            record["classification"] == CLASS_SUPPORTED and
            record["tag_matches_magic"] is False
            for record in records),
        "analysis_statuses": dict(sorted(Counter(
            str(run["status"]) for run in runs).items())),
        "component_completion": component_completion(runs),
        "recovered_code_objects": {
            "total": int(sum(code_objects)),
            **metric_summary(code_objects),
        },
        "wall_seconds": metric_summary([
            float(run["wall_seconds"]) for run in successful]),
        "cpu_seconds": metric_summary([
            float(run["cpu_seconds"]) for run in successful]),
        "peak_rss_bytes": metric_summary([
            float(run["peak_rss_bytes"]) for run in successful]),
        "versions": versions,
    }


def write_jsonl(path: Path, values: Iterable[object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8") as stream:
        for value in values:
            stream.write(json.dumps(value, sort_keys=True) + "\n")
    temporary.replace(path)


def write_summary_csv(path: Path, summary: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(("cpython", "files", "successful", "median_wall_seconds",
                         "p95_wall_seconds", "median_peak_rss_bytes",
                         "p95_peak_rss_bytes"))
        versions = summary["versions"]
        assert isinstance(versions, dict)
        for version in SUPPORTED_VERSIONS:
            row = versions[version]
            assert isinstance(row, dict)
            statuses = row["statuses"]
            wall = row["wall_seconds"]
            rss = row["peak_rss_bytes"]
            assert isinstance(statuses, dict)
            assert isinstance(wall, dict)
            assert isinstance(rss, dict)
            writer.writerow((version, row["files"], statuses.get(SUCCESS, 0),
                             wall["median"], wall["p95"], rss["median"],
                             rss["p95"]))


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-root", type=Path, default=DEFAULT_PACKAGE_ROOT)
    parser.add_argument("--build-root", type=Path, default=DEFAULT_BUILD_ROOT)
    parser.add_argument("--cpython-root", type=Path, default=DEFAULT_CPYTHON_ROOT)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--workers", type=int, default=DEFAULT_WORKERS)
    parser.add_argument("--timeout-seconds", type=int,
                        default=DEFAULT_TIMEOUT_SECONDS)
    parser.add_argument("--memory-gib", type=int, default=DEFAULT_MEMORY_GIB)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(argv)
    if args.workers <= 0 or args.timeout_seconds <= 0 or args.memory_gib <= 0:
        raise SystemExit("workers, timeout, and memory limit must be positive")
    revision = git_revision()
    output = (args.output or
              (DEFAULT_OUTPUT_ROOT / f"rq5-{revision[:7]}"))
    output = output.resolve()
    magics = supported_magics(args.cpython_root.resolve())
    records = inventory(args.package_root.resolve(), magics)
    tools: dict[str, Path] = {}
    for version in SUPPORTED_VERSIONS:
        tools[version] = validate_build(
            args.build_root.resolve(), args.cpython_root.resolve(), version,
            revision)
    write_json(output / "campaign.json", {
        "schema_version": RQ5_SCHEMA_VERSION,
        "created_at": utc_now(),
        "source_revision": revision,
        "package_root": str(args.package_root.resolve()),
        "build_root": str(args.build_root.resolve()),
        "cpython_root": str(args.cpython_root.resolve()),
        "workers": args.workers,
        "timeout_seconds": args.timeout_seconds,
        "memory_limit_bytes": args.memory_gib * 1_024**3,
        "supported_magics": magics,
    })
    write_jsonl(output / "inventory.jsonl", records)
    supported = [record for record in records
                 if record["classification"] == CLASS_SUPPORTED]

    def execute(record: dict[str, object]) -> dict[str, object]:
        return analyze_artifact(record, output, tools,
                                args.timeout_seconds, args.memory_gib)

    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        runs = list(executor.map(execute, supported))
    write_jsonl(output / "runs.jsonl", runs)
    summary = summarize(records, runs)
    write_json(output / "summary.json", summary)
    write_summary_csv(output / "summary.csv", summary)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
