#!/usr/bin/env python3
"""Run controlled CPyGraph design ablations on the full PYGBench universe."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD_ROOT = REPOSITORY_ROOT / "builds" / "ablations"
DEFAULT_OUTPUT_ROOT = REPOSITORY_ROOT / "experiments" / "results" / "ablations"
ABLATIONS = (
    "none",
    "field-model",
    "bound-method",
    "exception-flow",
    "interprocedural-ddg",
)
BASELINE_ABLATION = "none"
INSENSITIVE_POLICY = "insensitive"
METADATA_SCHEMA_VERSION = 1
SUMMARY_SCHEMA_VERSION = 1


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def revision() -> str:
    return subprocess.run(
        ["git", "-C", str(REPOSITORY_ROOT), "rev-parse", "HEAD"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()


def validate_build(build_root: Path, ablation: str,
                   source_revision: str) -> Path:
    directory = build_root / ablation
    metadata_path = directory / "cpygraph-ablation-build.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    executable = directory / "pygbench_case_analysis"
    expected = {
        "schema_version": METADATA_SCHEMA_VERSION,
        "ablation": ablation,
        "source_revision": source_revision,
        "executable": str(executable),
        "executable_sha256": sha256(executable),
    }
    mismatches = [name for name, value in expected.items()
                  if metadata.get(name) != value]
    if mismatches:
        raise ValueError(
            f"stale {ablation} build ({', '.join(mismatches)}); "
            "rerun scripts/build_ablation_matrix.sh")
    return executable


def policy_metrics(result: dict[str, object], policy: str,
                   group: str) -> dict[str, object]:
    report_path = Path(str(result["output_directory"])) / f"{policy}-report.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    metrics = report["overall"] if group == "overall" else report["groups"][group]
    return {
        name: metrics[name]
        for name in (
            "queries", "measured_queries", "sound_queries", "passed_queries",
            "true_positives", "true_negatives", "false_positives",
            "false_negatives", "precision", "recall", "sound_query_rate",
            "query_pass_rate", "complete",
        )
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", type=Path, default=DEFAULT_BUILD_ROOT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--timeout-seconds", type=int, default=1_800)
    parser.add_argument("--memory-gib", type=int, default=32)
    parser.add_argument("--rerun", action="store_true")
    args = parser.parse_args()
    if args.timeout_seconds <= 0 or args.memory_gib <= 0:
        parser.error("resource limits must be positive")

    source_revision = revision()
    output_root = args.output.resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    results: dict[str, dict[str, object]] = {}
    for ablation in ABLATIONS:
        executable = validate_build(
            args.build_root.resolve(), ablation, source_revision)
        directory = output_root / ablation
        result_path = directory / "result.json"
        if result_path.is_file() and not args.rerun:
            result = json.loads(result_path.read_text(encoding="utf-8"))
        else:
            directory.mkdir(parents=True, exist_ok=True)
            command = [
                sys.executable, str(REPOSITORY_ROOT / "PYGBench" / "run.py"),
                "evaluate", "--executable", str(executable),
                "--output-dir", str(directory),
                "--timeout-seconds", str(args.timeout_seconds),
                "--memory-gib", str(args.memory_gib),
            ]
            if ablation != BASELINE_ABLATION:
                command.extend((
                    "--allow-unsound", "--policy", INSENSITIVE_POLICY))
            completed = subprocess.run(
                command, cwd=REPOSITORY_ROOT, check=False,
                capture_output=True, text=True)
            (directory / "stdout.log").write_text(
                completed.stdout, encoding="utf-8")
            (directory / "stderr.log").write_text(
                completed.stderr, encoding="utf-8")
            if completed.returncode != 0:
                raise RuntimeError(
                    f"{ablation} failed: " +
                    (completed.stderr.strip() or "no diagnostic"))
            result = json.loads(completed.stdout)
            result_path.write_text(
                json.dumps(result, indent=2, sort_keys=True) + "\n",
                encoding="utf-8")
        result["output_directory"] = str(directory)
        results[ablation] = result

    groups = {
        "field-model": "component/pta",
        "bound-method": "component/cg",
        "exception-flow": "component/cfg",
        "interprocedural-ddg": "component/ddg",
    }
    baseline = policy_metrics(
        results[BASELINE_ABLATION], INSENSITIVE_POLICY, "overall")
    ablation_results = {}
    for ablation, group in groups.items():
        ablation_results[ablation] = {
            "primary_group": group,
            "overall": policy_metrics(
                results[ablation], INSENSITIVE_POLICY, "overall"),
            "component": policy_metrics(
                results[ablation], INSENSITIVE_POLICY, group),
            "resources": results[ablation]["policies"][INSENSITIVE_POLICY][
                "resources"],
        }
    summary = {
        "schema_version": SUMMARY_SCHEMA_VERSION,
        "source_revision": source_revision,
        "baseline": baseline,
        "sensitivity_policies": results[BASELINE_ABLATION]["policies"],
        "ablations": ablation_results,
    }
    (output_root / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
