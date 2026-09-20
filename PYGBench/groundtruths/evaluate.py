#!/usr/bin/env python3
"""Evaluate every PYGBench query under each supported PTA configuration."""

from __future__ import annotations

import argparse
from contextlib import nullcontext
import json
from pathlib import Path
import subprocess
import sys
import tempfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
if str(REPOSITORY_ROOT) not in sys.path:
    sys.path.insert(0, str(REPOSITORY_ROOT))

from experiments.resource_monitor import run_monitored


GROUND_TRUTHS = Path(__file__).resolve().parent
ADAPTERS = GROUND_TRUTHS.parent / "adapters"
DEFAULT_TIMEOUT_SECONDS = 1_800
DEFAULT_MEMORY_GIB = 32
GIBIBYTE_BYTES = 1_024**3
POLICIES = (
    "insensitive",
    "selective-flow",
    "selective-context",
    "selective-path",
    "selective-all",
    "selective-entry",
    "complete",
)
SENSITIVITY_CHALLENGES = {
    "flow": "flow-sensitive",
    "context": "context-sensitive",
    "path": "path-sensitive",
}


def run(arguments: list[str]) -> None:
    completed = subprocess.run(
        arguments, check=False, capture_output=True, text=True)
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or completed.stdout.strip())


def run_measured(arguments: list[str], timeout_seconds: int,
                 memory_limit_bytes: int) -> dict[str, object]:
    """Run one policy and measure the same invocation that emits its facts."""

    with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
        monitored = run_monitored(
            arguments,
            cwd=REPOSITORY_ROOT,
            stdout=stdout,
            stderr=stderr,
            timeout_seconds=timeout_seconds,
            memory_limit_bytes=memory_limit_bytes,
        )
        stdout.seek(0)
        stderr.seek(0)
        stdout_text = stdout.read().decode(errors="replace").strip()
        stderr_text = stderr.read().decode(errors="replace").strip()
    if monitored.exit_code != 0:
        detail = stderr_text or stdout_text or "measured analysis failed"
        raise RuntimeError(detail)
    return monitored.resources.to_json()


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument(
        "--output-dir", type=Path,
        help="retain observations and reports here; otherwise use temporary files")
    parser.add_argument("--timeout-seconds", type=int,
                        default=DEFAULT_TIMEOUT_SECONDS)
    parser.add_argument("--memory-gib", type=int, default=DEFAULT_MEMORY_GIB)
    parser.add_argument(
        "--allow-unsound", action="store_true",
        help="retain ablation results with false negatives instead of failing")
    parser.add_argument(
        "--policy", action="append", choices=POLICIES,
        help="evaluate only the selected policy; repeat for multiple policies")
    parser.add_argument(
        "--retain-artifacts", action="store_true",
        help="retain per-case DOT and raw analysis artifacts")
    return parser.parse_args()


def evaluate(executable: Path, output_root: Path, timeout_seconds: int,
             memory_limit_bytes: int,
             require_sound_recall: bool = True,
             policies: tuple[str, ...] = POLICIES,
             retain_artifacts: bool = False) -> dict[str, object]:
    output_root.mkdir(parents=True, exist_ok=True)
    reports: dict[str, dict[str, object]] = {}
    resources: dict[str, dict[str, object]] = {}
    for policy in policies:
        observations = output_root / f"{policy}-observations.json"
        report_path = output_root / f"{policy}-report.json"
        adapter_command = [
            sys.executable, str(ADAPTERS / "cpygraph.py"),
            "--executable", str(executable), "--quiet",
            "--policy", policy, "--output", str(observations),
        ]
        if retain_artifacts:
            adapter_command.extend([
                "--artifacts-dir", str(output_root / "artifacts" / policy)])
        resources[policy] = run_measured(
            adapter_command, timeout_seconds, memory_limit_bytes)
        (output_root / f"{policy}-resources.json").write_text(
            json.dumps(resources[policy], indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        score_command = [
            sys.executable, str(GROUND_TRUTHS / "score.py"),
            "--observations", str(observations), "--require-complete",
            "--output", str(report_path),
        ]
        if require_sound_recall:
            score_command.append("--require-sound-recall")
        run(score_command)
        reports[policy] = json.loads(report_path.read_text(encoding="utf-8"))

    versions = {str(report["python_version"]) for report in reports.values()}
    if len(versions) != 1:
        raise ValueError("policy reports use different Python versions")
    if "selective-all" in reports and "complete" in reports:
        for field in ("overall", "groups", "queries"):
            if reports["selective-all"][field] != reports["complete"][field]:
                raise ValueError(
                    f"selective-all and complete results differ in {field}")
    unsound = [
        f"{policy}/{group}"
        for policy, report in reports.items()
        for group, metrics in report["groups"].items()
        if float(metrics["recall"]) != 1.0
    ]
    if unsound and require_sound_recall:
        raise ValueError("unsound PYGBench groups: " + ", ".join(unsound))

    def metrics(policy: str, group: str = "overall") -> dict[str, object]:
        values = (reports[policy]["overall"] if group == "overall"
                  else reports[policy]["groups"][group])
        return {
            "query_groups": values["queries"],
            "expected_candidates": values["expected_candidates"],
            "negative_candidates": values["negative_candidates"],
            "total_candidates": values["total_candidates"],
            "true_positives": values["true_positives"],
            "true_negatives": values["true_negatives"],
            "false_positives": values["false_positives"],
            "false_negatives": values["false_negatives"],
            "precision": values["precision"],
            "recall": values["recall"],
        }

    challenges = {}
    for attribute, tag in SENSITIVITY_CHALLENGES.items():
        selected_policy = f"selective-{attribute}"
        if "insensitive" not in reports or selected_policy not in reports:
            continue
        group = f"dimension/sensitivity/{tag}"
        challenges[attribute] = {
            "insensitive": metrics("insensitive", group),
            selected_policy: metrics(selected_policy, group),
        }
    representative = reports[policies[0]]["overall"]
    universes = {
        json.dumps(report["evaluation_universe"], sort_keys=True)
        for report in reports.values()
    }
    if len(universes) != 1:
        raise ValueError("policy reports use different evaluation universes")
    return {
        "suite": "PYGBench",
        "python_version": versions.pop(),
        "measured_query_groups": representative["measured_queries"],
        "query_groups": representative["queries"],
        "evaluation_universe": reports[policies[0]]["evaluation_universe"],
        "policies": {
            policy: {
                **metrics(policy),
                "components": {
                    component: metrics(policy, f"component/{component}")
                    for component in ("pta", "cg", "cfg", "cdg", "ddg")
                },
                "resources": resources[policy],
                "sensitivity_selection": reports[policy].get(
                    "sensitivity_selection"),
            }
            for policy in policies
        },
        "sensitivity_challenges": challenges,
        "status": "complete",
    }


def main() -> int:
    arguments = parse_arguments()
    if arguments.timeout_seconds <= 0 or arguments.memory_gib <= 0:
        raise ValueError("timeout and memory limit must be positive")
    executable = arguments.executable.resolve()
    if not executable.is_file():
        raise ValueError(f"case-analysis executable does not exist: {executable}")
    temporary = (tempfile.TemporaryDirectory(prefix="pygbench-results-")
                 if arguments.output_dir is None else nullcontext(None))
    with temporary as path:
        output_root = (arguments.output_dir.resolve()
                       if arguments.output_dir is not None else Path(path))
        print(json.dumps(evaluate(
            executable, output_root, arguments.timeout_seconds,
            arguments.memory_gib * GIBIBYTE_BYTES,
            require_sound_recall=not arguments.allow_unsound,
            policies=tuple(arguments.policy or POLICIES),
            retain_artifacts=arguments.retain_artifacts,
        ), sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(2)
