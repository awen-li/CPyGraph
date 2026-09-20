#!/usr/bin/env python3
"""Audit source presence and package context for an existing RQ5 run."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import subprocess


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = REPOSITORY_ROOT / "experiments/results/rq5"
DEFAULT_OUTPUT = REPOSITORY_ROOT / "experiments/results/rq5-source-audit"
DEFAULT_CPYTHON_ROOT = REPOSITORY_ROOT / "builds" / "cpython-venvs"
SUPPORTED = "SUPPORTED"
SOURCE_ABSENT = "absent"
SOURCE_HEADER_MATCHED = "header-matched"
SOURCE_HEADER_MISMATCH = "header-mismatch"
SOURCE_PRESENT_UNVERIFIED = "present-unverified"
PYC_HEADER_BYTES = 16
TIMESTAMP_FLAGS = 0
INDEPENDENT_STATS_PROGRAM = r"""
import dis
import marshal
from pathlib import Path
import sys
import types

root = marshal.loads(Path(sys.argv[1]).read_bytes()[16:])

def code_objects(code):
    result = [code]
    for value in code.co_consts:
        if isinstance(value, types.CodeType):
            result.extend(code_objects(value))
    return result

def instructions(code):
    try:
        return list(dis.get_instructions(code, show_caches=False))
    except TypeError:
        return list(dis.get_instructions(code))

codes = code_objects(root)
print(json.dumps({
    "code_objects": len(codes),
    "dis_instructions": sum(len(instructions(code)) for code in codes),
}))
"""


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")


def write_jsonl(path: Path, values: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as stream:
        for value in values:
            stream.write(json.dumps(value, sort_keys=True) + "\n")


def read_jsonl(path: Path) -> list[dict[str, object]]:
    return [json.loads(line) for line in path.read_text(
        encoding="utf-8").splitlines() if line]


def cache_stem(name: str) -> str | None:
    if not name.endswith(".pyc"):
        return None
    for marker in (".cpython-", ".pypy"):
        if marker in name:
            return name.split(marker, 1)[0]
    return None


def source_candidate(path: Path) -> Path | None:
    if path.parent.name == "__pycache__":
        stem = cache_stem(path.name)
        if stem is None:
            return None
        return path.parent.parent / f"{stem}.py"
    if path.suffix == ".pyc":
        return path.with_suffix(".py")
    return None


def source_evidence(path: Path) -> dict[str, object]:
    source = source_candidate(path)
    if source is None:
        return {
            "source_candidate": None,
            "source_status": SOURCE_PRESENT_UNVERIFIED,
            "timestamp_matches": None,
            "size_matches": None,
        }
    base = {"source_candidate": str(source)}
    if not source.is_file():
        return {
            **base,
            "source_status": SOURCE_ABSENT,
            "timestamp_matches": None,
            "size_matches": None,
        }
    header = path.read_bytes()[:PYC_HEADER_BYTES]
    if len(header) != PYC_HEADER_BYTES:
        return {
            **base,
            "source_status": SOURCE_PRESENT_UNVERIFIED,
            "timestamp_matches": None,
            "size_matches": None,
        }
    flags = int.from_bytes(header[4:8], "little")
    if flags != TIMESTAMP_FLAGS:
        return {
            **base,
            "source_status": SOURCE_PRESENT_UNVERIFIED,
            "pyc_flags": flags,
            "timestamp_matches": None,
            "size_matches": None,
        }
    expected_timestamp = int.from_bytes(header[8:12], "little")
    expected_size = int.from_bytes(header[12:16], "little")
    timestamp_matches = (
        int(source.stat().st_mtime) & 0xFFFFFFFF) == expected_timestamp
    size_matches = (source.stat().st_size & 0xFFFFFFFF) == expected_size
    return {
        **base,
        "source_status": (SOURCE_HEADER_MATCHED
                          if timestamp_matches and size_matches
                          else SOURCE_HEADER_MISMATCH),
        "pyc_flags": flags,
        "timestamp_matches": timestamp_matches,
        "size_matches": size_matches,
    }


def module_identity(relative_path: str) -> str | None:
    parts = list(Path(relative_path).parts)
    try:
        source_index = parts.index("source")
    except ValueError:
        return None
    module_parts = parts[source_index + 1:]
    if not module_parts:
        return None
    if len(module_parts) >= 2 and module_parts[-2] == "__pycache__":
        stem = cache_stem(module_parts[-1])
        if stem is None:
            return None
        module_parts = module_parts[:-2] + [stem]
    else:
        module_parts[-1] = Path(module_parts[-1]).stem
    if module_parts[-1] == "__init__":
        module_parts.pop()
    return ".".join(module_parts)


def independent_stats(path: Path, version: str,
                      cpython_root: Path) -> dict[str, int]:
    executable = cpython_root / f"cpython-{version}" / "bin" / "python"
    completed = subprocess.run(
        [str(executable), "-c", "import json\n" + INDEPENDENT_STATS_PROGRAM,
         str(path)],
        check=True, capture_output=True, text=True)
    result = json.loads(completed.stdout)
    return {name: int(value) for name, value in result.items()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--cpython-root", type=Path,
                        default=DEFAULT_CPYTHON_ROOT)
    args = parser.parse_args()
    input_root = args.input.resolve()
    output = args.output.resolve()
    campaign = json.loads((input_root / "campaign.json").read_text(
        encoding="utf-8"))
    inventory = read_jsonl(input_root / "inventory.jsonl")
    runs = read_jsonl(input_root / "runs.jsonl")
    runs_by_sha = {str(run["sha256"]): run for run in runs}

    audited = []
    for record in inventory:
        path = Path(str(record["path"]))
        audited.append({
            **record,
            **source_evidence(path),
            "module_identity": module_identity(str(record["relative_path"])),
            "analysis_unit": "individual-pyc-file",
        })

    supported = [record for record in audited
                 if record["classification"] == SUPPORTED]
    groups: dict[tuple[str, str], list[dict[str, object]]] = defaultdict(list)
    for record in supported:
        groups[(str(record["package"]),
                str(record["python_version"]))].append(record)
    group_records = []
    for (package, version), records in sorted(groups.items()):
        identities = [record["module_identity"] for record in records]
        group_records.append({
            "package": package,
            "python_version": version,
            "files": len(records),
            "distinct_module_identities": len(set(identities)),
            "module_identity_collisions": len(identities) - len(set(identities)),
            "analysis_mode_in_original_rq5": "per-file",
            "package_group_analysis_performed": False,
        })

    samples = []
    versions = sorted({str(record["python_version"]) for record in supported})
    for version in versions:
        candidates = [record for record in supported
                      if record["python_version"] == version]
        candidates.sort(key=lambda record: (
            int(record["size_bytes"]), str(record["relative_path"])))
        selected = candidates[len(candidates) // 2]
        independent = independent_stats(
            Path(str(selected["path"])), version, args.cpython_root.resolve())
        analysis = runs_by_sha[str(selected["sha256"])]["analysis_summary"]
        samples.append({
            "selection_rule": "median bytecode size within runtime version",
            "python_version": version,
            "relative_path": selected["relative_path"],
            "source_status": selected["source_status"],
            "independent_marshal_code_objects": independent["code_objects"],
            "cpygraph_code_objects": analysis["code_objects"],
            "code_object_count_agrees": (
                independent["code_objects"] == analysis["code_objects"]),
            "independent_dis_instructions": independent["dis_instructions"],
            "cpygraph_native_instructions": analysis["native_instructions"],
            "instruction_count_agrees": (
                independent["dis_instructions"] ==
                analysis["native_instructions"]),
        })

    source_counts = Counter(str(record["source_status"])
                            for record in supported)
    multi_file_groups = [group for group in group_records
                         if int(group["files"]) > 1]
    summary = {
        "schema_version": 1,
        "original_rq5_source_revision": campaign["source_revision"],
        "original_supported_files": len(supported),
        "source_rule": (
            "Map tagged cache files to the adjacent same-stem .py file; "
            "classify timestamp-based bytecode as matched only when both "
            "the stored source timestamp and size agree."),
        "supported_source_statuses": dict(sorted(source_counts.items())),
        "supported_same_named_source_present": sum(
            record["source_status"] != SOURCE_ABSENT for record in supported),
        "supported_source_absent": source_counts[SOURCE_ABSENT],
        "supported_demonstrated_header_match": source_counts[
            SOURCE_HEADER_MATCHED],
        "package_context": {
            "compatible_package_runtime_groups": len(group_records),
            "single_file_groups": len(group_records) - len(multi_file_groups),
            "multi_file_groups": len(multi_file_groups),
            "files_in_multi_file_groups": sum(
                int(group["files"]) for group in multi_file_groups),
            "maximum_files_in_group": max(
                int(group["files"]) for group in group_records),
            "original_analysis_unit": "individual-pyc-file",
            "package_group_analysis_performed": False,
        },
        "representative_loader_checks": {
            "selection": "one median-size supported file per runtime version",
            "samples": len(samples),
            "code_object_count_agreements": sum(
                bool(sample["code_object_count_agrees"]) for sample in samples),
            "instruction_count_agreements": sum(
                bool(sample["instruction_count_agrees"]) for sample in samples),
            "scope": (
                "Independent marshal/dis structural recovery check; not a "
                "graph-semantic accuracy oracle."),
        },
    }
    write_json(output / "manifest.json", {
        "schema_version": 1,
        "input": str(input_root),
        "input_campaign": campaign,
        "inventory_records": len(inventory),
        "supported_run_records": len(runs),
    })
    write_jsonl(output / "records.jsonl", audited)
    write_json(output / "package-groups.json", group_records)
    write_json(output / "representative-checks.json", samples)
    write_json(output / "summary.json", summary)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
