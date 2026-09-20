#!/usr/bin/env python3
"""Validate PYGBench sources and their source-adjacent semantic oracles."""

from __future__ import annotations

import json
from pathlib import Path
from collections import Counter


ROOT = Path(__file__).resolve().parents[1]
GROUND_TRUTHS = ROOT / "groundtruths"
MARKERS = {
    "PYGBENCH": "id",
    "CATEGORY": "category",
    "TAGS": "tags",
    "EXPECT": "expect",
    "FORBID": "forbid",
    "EXPECT_PATH": "expect_paths",
    "FORBID_PATH": "forbid_paths",
}


def parse_oracle(source: str) -> dict[str, object] | None:
    oracle: dict[str, object] = {
        "expect": [], "forbid": [],
        "expect_paths": [], "forbid_paths": [],
    }
    for line in source.splitlines():
        if not line.startswith("# ") or ":" not in line:
            continue
        marker, value = line[2:].split(":", 1)
        field = MARKERS.get(marker)
        if field is None:
            continue
        value = value.strip()
        if field in {"expect", "forbid", "expect_paths", "forbid_paths"}:
            assert isinstance(oracle[field], list)
            oracle[field].append(value)
        elif field == "tags":
            oracle[field] = [tag.strip() for tag in value.split(",") if tag.strip()]
        else:
            oracle[field] = value
    return oracle if "id" in oracle else None


def main() -> None:
    manifest = json.loads(
        (GROUND_TRUTHS / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 2:
        raise AssertionError("unsupported PYGBench manifest schema")

    valid_categories = set(manifest["categories"])
    seen: dict[str, Path] = {}
    counts: Counter[str] = Counter()

    for relative_root in manifest["case_roots"]:
        case_root = ROOT / relative_root
        if not case_root.is_dir():
            raise AssertionError(f"missing case root: {case_root}")
        for path in sorted(case_root.rglob("*.py")):
            source = path.read_text(encoding="utf-8")
            compile(source, str(path), "exec", dont_inherit=True)
            oracle = parse_oracle(source)
            if oracle is None:
                continue

            case_id = str(oracle["id"])
            if not case_id or case_id in seen:
                raise AssertionError(f"duplicate or empty case id {case_id!r}: {path}")
            seen[case_id] = path

            category = oracle.get("category")
            if category not in valid_categories:
                raise AssertionError(f"{case_id} has invalid category: {category!r}")
            tags = oracle.get("tags")
            if not isinstance(tags, list) or not tags:
                raise AssertionError(f"{case_id} must declare at least one tag")

            expected = oracle["expect"]
            forbidden = oracle["forbid"]
            if not isinstance(expected, list) or "" in expected:
                raise AssertionError(f"{case_id} has invalid expected facts")
            if not isinstance(forbidden, list) or not forbidden or "" in forbidden:
                raise AssertionError(f"{case_id} has no explicit forbidden facts")
            if not expected and not forbidden:
                raise AssertionError(f"{case_id} has no explicit facts")
            overlap = set(expected) & set(forbidden)
            if overlap:
                raise AssertionError(f"{case_id} expects and forbids {sorted(overlap)}")
            expected_paths = oracle["expect_paths"]
            forbidden_paths = oracle["forbid_paths"]
            if not isinstance(expected_paths, list) or \
                    not isinstance(forbidden_paths, list):
                raise AssertionError(f"{case_id} has invalid semantic paths")
            path_overlap = set(expected_paths) & set(forbidden_paths)
            if path_overlap:
                raise AssertionError(
                    f"{case_id} expects and forbids paths {sorted(path_overlap)}")
            counts[str(category)] += 1

    if not seen:
        raise AssertionError("PYGBench has no marked cases")
    missing_categories = valid_categories - counts.keys()
    if missing_categories:
        raise AssertionError(f"categories without cases: {sorted(missing_categories)}")
    minimum = int(manifest["minimum_cases_per_category"])
    undersized = {category: counts[category] for category in valid_categories
                  if counts[category] < minimum}
    if undersized:
        raise AssertionError(f"categories below minimum size {minimum}: {undersized}")

    print(json.dumps({"suite": manifest["suite"], "cases": len(seen),
                      "categories": dict(sorted(counts.items()))}, sort_keys=True))


if __name__ == "__main__":
    main()
