#!/usr/bin/env python3
"""Validate ablation build identity and metric extraction."""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
if str(REPOSITORY_ROOT) not in sys.path:
    sys.path.insert(0, str(REPOSITORY_ROOT))

from experiments.run_ablations import policy_metrics, sha256, validate_build


def main() -> int:
    with tempfile.TemporaryDirectory(
            prefix="cpygraph-ablation-test-") as root:
        build_root = Path(root) / "builds"
        directory = build_root / "none"
        directory.mkdir(parents=True)
        executable = directory / "pygbench_case_analysis"
        executable.write_bytes(b"ablation executable")
        metadata = {
            "schema_version": 1,
            "ablation": "none",
            "source_revision": "revision",
            "python_version": "3.10",
            "executable": str(executable),
            "executable_sha256": sha256(executable),
        }
        (directory / "cpygraph-ablation-build.json").write_text(
            json.dumps(metadata), encoding="utf-8")
        assert validate_build(build_root, "none", "revision") == executable

        result_directory = Path(root) / "result"
        result_directory.mkdir()
        metrics = {
            "queries": 1,
            "measured_queries": 1,
            "sound_queries": 1,
            "passed_queries": 1,
            "true_positives": 1,
            "true_negatives": 1,
            "false_positives": 0,
            "false_negatives": 0,
            "precision": 1.0,
            "recall": 1.0,
            "sound_query_rate": 1.0,
            "query_pass_rate": 1.0,
            "complete": True,
        }
        (result_directory / "insensitive-report.json").write_text(
            json.dumps({"overall": metrics, "groups": {
                "component/cg": metrics,
            }}), encoding="utf-8")
        extracted = policy_metrics(
            {"output_directory": str(result_directory)},
            "insensitive", "component/cg")
        assert extracted == metrics
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
