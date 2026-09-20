#!/usr/bin/env python3
"""Run PYGBench cases independently through the CPyGraph library entry."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

from validate import GROUND_TRUTHS, ROOT, parse_oracle


PASS = "PASS"
FAIL = "FAIL"


def discover_cases() -> list[tuple[str, str, list[str], Path]]:
    manifest = json.loads(
        (GROUND_TRUTHS / "manifest.json").read_text(encoding="utf-8"))
    cases: list[tuple[str, str, list[str], Path]] = []
    for relative_root in manifest["case_roots"]:
        for path in sorted((ROOT / relative_root).rglob("*.py")):
            oracle = parse_oracle(path.read_text(encoding="utf-8"))
            if oracle is None:
                continue
            tags = oracle["tags"]
            assert isinstance(tags, list)
            cases.append((str(oracle["id"]), str(oracle["category"]),
                          [str(tag) for tag in tags], path))
    return sorted(cases)


def analyze_case(executable: Path, case_id: str, category: str,
                 path: Path, ground_truth: dict[str, object]) -> dict[str, object]:
    entry_points = ground_truth["entry_points"]
    assert isinstance(entry_points, list)
    completed = subprocess.run(
        [str(executable), str(path), *(str(entry) for entry in entry_points)],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        return {
            "id": case_id,
            "category": category,
            "status": FAIL,
            "error": completed.stderr.strip() or "case analysis failed",
        }
    try:
        result = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        return {
            "id": case_id,
            "category": category,
            "status": FAIL,
            "error": f"invalid case-analysis output: {error}",
        }
    components = ground_truth["components"]
    assert isinstance(components, dict)
    result.update({
        "id": case_id,
        "category": category,
        "ground_truth": {
            name: str(component["applicability"]).upper()
            for name, component in components.items()
        },
    })
    return result


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path,
                        help="path to the linked pygbench_case_analysis entry")
    parser.add_argument("--list", action="store_true",
                        help="list matching cases without running an analyzer")
    parser.add_argument("--case", help="run one exact case ID")
    parser.add_argument("--category", help="run one category")
    parser.add_argument("--tag", help="run cases carrying one exact tag")
    parser.add_argument("--json", action="store_true", help="emit JSON lines")
    parser.add_argument("--quiet", action="store_true", help="emit failures and summary only")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    catalog_path = GROUND_TRUTHS / "oracle.json"
    if not catalog_path.is_file():
        raise SystemExit("groundtruths/oracle.json is missing")
    catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    ground_truth_by_id = {case["id"]: case for case in catalog["cases"]}

    selected = [case for case in discover_cases()
                if (arguments.case is None or case[0] == arguments.case)
                and (arguments.category is None or case[1] == arguments.category)]
    if arguments.tag is not None:
        selected = [case for case in selected if arguments.tag in case[2]]
    if not selected:
        raise SystemExit("no PYGBench cases match the requested filters")
    if arguments.list:
        for case_id, category, tags, path in selected:
            if arguments.json:
                print(json.dumps({
                    "id": case_id, "category": category,
                    "tags": tags, "source": str(path.relative_to(ROOT)),
                }, sort_keys=True))
            else:
                print(f"{case_id:<40} {category:<10} {','.join(tags)}")
        if not arguments.json:
            print(f"{len(selected)} case(s)")
        return 0
    if arguments.executable is None:
        raise SystemExit("--executable is required unless --list is used")
    executable = arguments.executable.resolve()
    if not executable.is_file():
        raise SystemExit(f"case-analysis executable does not exist: {executable}")
    missing_ground_truth = {case[0] for case in selected} - ground_truth_by_id.keys()
    if missing_ground_truth:
        raise SystemExit(f"cases without ground truth: {sorted(missing_ground_truth)}")

    passed = 0
    failed = 0
    for case_id, category, tags, path in selected:
        result = analyze_case(executable, case_id, category, path,
                              ground_truth_by_id[case_id])
        result["tags"] = tags
        success = result.get("status") == PASS
        passed += int(success)
        failed += int(not success)
        if arguments.json:
            print(json.dumps(result, sort_keys=True))
        elif not arguments.quiet or not success:
            detail = ""
            if not success:
                detail = f": {result.get('error', 'unknown failure')}"
            print(f"[{result.get('status', FAIL)}] {case_id} ({category}){detail}")

    summary = {"suite": "PYGBench", "cases": len(selected),
               "passed": passed, "failed": failed}
    if arguments.json:
        print(json.dumps({"summary": summary}, sort_keys=True))
    else:
        print(f"PYGBench: {passed}/{len(selected)} passed, {failed} failed")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
