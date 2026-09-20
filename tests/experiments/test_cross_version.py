#!/usr/bin/env python3
"""Focused tests for canonical cross-version experiment aggregation."""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
if str(REPOSITORY_ROOT) not in sys.path:
    sys.path.insert(0, str(REPOSITORY_ROOT))

from experiments.run_cross_version import (
    POLICIES,
    adapter_metrics,
    semantic_consistency,
)


def query(query_id: str, expected: list[str], predicted: list[str],
          component: str = "cg") -> dict[str, object]:
    return {
        "id": query_id,
        "component": component,
        "expected_objects": expected,
        "predicted_objects": predicted,
        "status": "measured",
        "coverage": "concrete-only",
        "sound": set(expected) <= set(predicted),
        "passed": set(expected) == set(predicted),
    }


def main() -> int:
    metrics = adapter_metrics()
    assert [entry["cpython"] for entry in metrics] == [
        "3.10", "3.11", "3.12", "3.13", "3.14"]
    assert all(entry["generated_opcode_entries"] for entry in metrics)
    assert all(entry["handwritten_lines"] for entry in metrics)
    assert metrics[0]["opcode_entries_added_from_previous"] is None
    assert all(entry["opcode_entries_added_from_previous"] is not None
               for entry in metrics[1:])

    with tempfile.TemporaryDirectory(
            prefix="cpygraph-cross-version-test-") as root:
        output = Path(root)
        for version in ("3.10", "3.11"):
            directory = output / f"cpython-{version}"
            directory.mkdir()
            queries = [
                query("stable", ["target"], ["target"]),
                query("mismatch", ["target"],
                      ["target"] if version == "3.10" else ["target", "other"]),
                query("version-dependent",
                      ["old"] if version == "3.10" else ["new"],
                      ["old"] if version == "3.10" else ["new"]),
            ]
            for policy in POLICIES:
                (directory / f"{policy}-report.json").write_text(
                    json.dumps({"queries": queries}), encoding="utf-8")
        result = semantic_consistency(output, ["3.10", "3.11"])
        insensitive = result["policies"]["insensitive"]
        assert insensitive["query_ids"] == 3
        assert insensitive["invariant_oracle_queries"] == 2
        assert insensitive["version_dependent_oracle_queries"] == 1
        assert insensitive["prediction_consistency"] == 0.5
        assert insensitive["prediction_mismatch_query_ids"] == ["mismatch"]
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
