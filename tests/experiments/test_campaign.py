#!/usr/bin/env python3
"""Validate the frozen-corpus campaign plan without running analyzers."""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
if str(REPOSITORY_ROOT) not in sys.path:
    sys.path.insert(0, str(REPOSITORY_ROOT))

from experiments.campaign import (
    DEFAULT_ANALYZERS,
    SUPPORTED_VERSIONS,
    analyzers_for,
    cpygraph_build_identity,
    load_manifest,
    metric_summary,
    records,
    sha256,
    summarize,
    validate_build,
)


EXPECTED_PACKAGES_PER_VERSION = 500
EXPECTED_TOTAL_PACKAGES = 2_500
EXPECTED_REAL_PACKAGE_RUNS = 2_500


def main() -> int:
    manifest = load_manifest(
        REPOSITORY_ROOT / "benchmarks" / "package-list.json")
    selected = list(records(manifest, set(SUPPORTED_VERSIONS), set()))
    assert len(selected) == EXPECTED_TOTAL_PACKAGES
    for version in SUPPORTED_VERSIONS:
        assert sum(record["cpython"] == version for record in selected) == \
            EXPECTED_PACKAGES_PER_VERSION
    planned = sum(len(analyzers_for(str(record["cpython"]),
                                    DEFAULT_ANALYZERS))
                  for record in selected)
    assert planned == EXPECTED_REAL_PACKAGE_RUNS
    assert analyzers_for("3.10", DEFAULT_ANALYZERS) == ("cpygraph",)
    assert analyzers_for("3.11", DEFAULT_ANALYZERS) == ("cpygraph",)
    summary = metric_summary([1.0, 2.0, 3.0])
    assert summary["median"] == 2.0
    assert summary["minimum"] == 1.0
    assert summary["maximum"] == 3.0
    campaign_summary = summarize([{
        "analyzer": "cpygraph",
        "cpython": "3.10",
        "status": "SUCCESS",
        "wall_seconds": 1.0,
        "cpu_seconds": 0.5,
        "peak_rss_bytes": 1_024,
        "analysis_summary": {
            "modules": 2,
            "pta": {"objects": 7},
            "ddg": {"edges": 11},
        },
        "internal_profiling": {"phases": [{
            "phase": "points_to_call_graph",
            "wall_time_ns": 10,
            "cpu_time_ns": 8,
            "peak_rss_bytes": 512,
        }]},
    }], 1, 0)
    group = campaign_summary["groups"][0]
    assert group["analysis_scale"]["pta.objects"]["median"] == 7.0
    assert group["analysis_scale"]["ddg.edges"]["median"] == 11.0
    assert group["phase_costs"]["points_to_call_graph"][
        "wall_time_ns"]["median"] == 10.0

    with tempfile.TemporaryDirectory(prefix="cpygraph-campaign-test-") as root:
        temporary = Path(root)
        build_root = temporary / "builds"
        cpython_root = temporary / "envs"
        build = build_root / "cpython-3.10"
        tool = build / "tools" / "pygDDG"
        tool.parent.mkdir(parents=True)
        tool.write_bytes(b"test executable identity")
        metadata = {
            "schema_version": 1,
            "cpython": "3.10",
            "python_executable": str(
                cpython_root / "cpython-3.10" / "bin" / "python"),
            "source_revision": "test-revision",
            "pygddg": str(tool),
            "pygddg_sha256": sha256(tool),
        }
        (build / "cpygraph-build.json").write_text(
            json.dumps(metadata), encoding="utf-8")
        assert validate_build(
            build_root, cpython_root, "3.10", "test-revision") == tool
        assert cpygraph_build_identity(build_root, "3.10") == {
            "pygddg": str(tool),
            "pygddg_sha256": sha256(tool),
        }
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
