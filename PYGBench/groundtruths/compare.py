#!/usr/bin/env python3
"""Compare canonical analyzer results directly with saved PYGBench ground truth."""

from __future__ import annotations

import argparse
from collections import defaultdict
import json
from pathlib import Path
import sys


GROUND_TRUTHS = Path(__file__).resolve().parent
CASES = GROUND_TRUTHS / "cases"
GRAPH_COMPONENTS = ("cg", "cfg", "cdg", "ddg")
EXCLUDED_LIMITATIONS = {"async-requires-dedicated-model"}


def supported_case(case: dict[str, object]) -> bool:
    limitations = case.get("limitations", [])
    if not isinstance(limitations, list):
        raise ValueError(f"{case.get('case', '<unknown>')} has invalid limitations")
    return not (set(str(value) for value in limitations) & EXCLUDED_LIMITATIONS)


def empty_counts() -> dict[str, int]:
    return {
        "expected_candidates": 0,
        "negative_candidates": 0,
        "total_candidates": 0,
        "true_positives": 0,
        "true_negatives": 0,
        "false_positives": 0,
        "false_negatives": 0,
    }


def add_counts(target: dict[str, int], source: dict[str, int]) -> None:
    for name in target:
        target[name] += source[name]


def finish(counts: dict[str, int]) -> dict[str, object]:
    predicted = counts["true_positives"] + counts["false_positives"]
    expected = counts["true_positives"] + counts["false_negatives"]
    return {
        **counts,
        "precision": (counts["true_positives"] / predicted
                      if predicted else 1.0),
        "recall": (counts["true_positives"] / expected
                   if expected else 1.0),
    }


def missing_result_counts(expected: dict[str, object]) -> dict[str, int]:
    counts = empty_counts()
    pta_expected = sum(len(value["points_to"])
                       for value in expected["points_to_sets"])
    pta_negative = sum(len(value["must_not_point_to"])
                       for value in expected["points_to_sets"])
    counts["expected_candidates"] += pta_expected
    counts["negative_candidates"] += pta_negative
    counts["false_negatives"] += pta_expected
    for component in GRAPH_COMPONENTS:
        graph = expected[component]
        graph_expected = (len(graph["required_edges"]) +
                          len(graph["required_paths"]))
        graph_negative = (len(graph["forbidden_edges"]) +
                          len(graph["forbidden_paths"]))
        counts["expected_candidates"] += graph_expected
        counts["negative_candidates"] += graph_negative
        counts["false_negatives"] += graph_expected
    counts["total_candidates"] = (
        counts["expected_candidates"] + counts["negative_candidates"])
    return counts


def labeled_candidate_universe(
        cases: list[dict[str, object]]) -> dict[str, object]:
    pta_expected = pta_forbidden = 0
    graph_counts = {
        component: {
            "direct_expected": 0,
            "direct_negative": 0,
            "multi_hop_expected": 0,
            "multi_hop_negative": 0,
        }
        for component in GRAPH_COMPONENTS
    }
    for case in cases:
        for points_to in case["points_to_sets"]:
            pta_expected += len(points_to["points_to"])
            pta_forbidden += len(points_to["must_not_point_to"])
        for component in GRAPH_COMPONENTS:
            graph = case[component]
            counts = graph_counts[component]
            counts["direct_expected"] += len(graph["required_edges"])
            counts["direct_negative"] += len(graph["forbidden_edges"])
            counts["multi_hop_expected"] += len(graph["required_paths"])
            counts["multi_hop_negative"] += len(graph["forbidden_paths"])

    def population(expected: int, negative: int) -> dict[str, int]:
        return {"expected": expected, "negative": negative,
                "total": expected + negative}

    def component_population(counts: dict[str, int]) -> dict[str, object]:
        direct = population(counts["direct_expected"],
                            counts["direct_negative"])
        multi_hop = population(counts["multi_hop_expected"],
                               counts["multi_hop_negative"])
        return {
            "direct": direct,
            "multi_hop": multi_hop,
            "all": population(direct["expected"] + multi_hop["expected"],
                              direct["negative"] + multi_hop["negative"]),
        }

    by_component = {
        component: component_population(counts)
        for component, counts in graph_counts.items()
    }
    direct_expected = sum(value["direct"]["expected"]
                          for value in by_component.values())
    direct_negative = sum(value["direct"]["negative"]
                          for value in by_component.values())
    multi_hop_expected = sum(value["multi_hop"]["expected"]
                             for value in by_component.values())
    multi_hop_negative = sum(value["multi_hop"]["negative"]
                             for value in by_component.values())
    graph_expected = direct_expected + multi_hop_expected
    graph_negative = direct_negative + multi_hop_negative
    all_expected = pta_expected + graph_expected
    all_negative = pta_forbidden + graph_negative
    return {
        "pta_memberships": population(pta_expected, pta_forbidden),
        "graph_paths": {
            "by_component": by_component,
            "direct": population(direct_expected, direct_negative),
            "multi_hop": population(multi_hop_expected, multi_hop_negative),
            "all": population(graph_expected, graph_negative),
        },
        "all_scored_candidates": population(all_expected, all_negative),
    }


def graph_edges(graph: dict[str, object]) -> set[tuple[str, str, str | None]]:
    edges = graph.get("edges")
    if not isinstance(edges, list):
        raise ValueError("result graph must contain an edges array")
    result: set[tuple[str, str, str | None]] = set()
    for edge in edges:
        if not isinstance(edge, dict):
            raise ValueError("result edge must be an object")
        source, target = edge.get("source"), edge.get("target")
        if not isinstance(source, str) or not source or \
                not isinstance(target, str) or not target:
            raise ValueError("result edge must have canonical source and target")
        outcome = edge.get("outcome")
        if outcome is not None and outcome not in {
                "normal", "true", "false", "exception", "resume"}:
            raise ValueError("result edge has an invalid branch outcome")
        result.add((source, target, outcome))
    return result


def declared_paths(graph: dict[str, object]) -> set[tuple[str, ...]] | None:
    values = graph.get("paths")
    if values is None:
        return None
    if not isinstance(values, list):
        raise ValueError("result graph paths must be an array")
    paths: set[tuple[str, ...]] = set()
    for value in values:
        if not isinstance(value, list) or len(value) < 2 or any(
                not isinstance(node, str) or not node for node in value):
            raise ValueError("result paths must contain canonical node arrays")
        paths.add(tuple(value))
    return paths


def path_present(path: tuple[str, ...], edges: set[tuple[str, str]],
                 paths: set[tuple[str, ...]] | None) -> bool:
    if paths is not None:
        return path in paths
    return all(edge in edges for edge in zip(path, path[1:]))


def compare_graph(expected: dict[str, object], actual: dict[str, object]
                  ) -> tuple[dict[str, int], dict[str, object]]:
    actual_nodes = actual.get("nodes")
    if not isinstance(actual_nodes, list) or any(
            not isinstance(node, str) or not node for node in actual_nodes):
        raise ValueError("result graph must contain canonical string nodes")
    node_set = set(actual_nodes)
    expected_nodes = set(str(node) for node in expected["nodes"])
    missing_nodes = sorted(expected_nodes - node_set)

    typed_edges = graph_edges(actual)
    edges = {(source, target) for source, target, _ in typed_edges}
    required_edges = {
        (str(edge["source"]), str(edge["target"]), edge.get("outcome"))
        for edge in expected["required_edges"]
    }
    forbidden_edges = {
        (str(edge["source"]), str(edge["target"]), edge.get("outcome"))
        for edge in expected["forbidden_edges"]
    }
    counts = empty_counts()
    counts["expected_candidates"] = (
        len(required_edges) + len(expected["required_paths"]))
    counts["negative_candidates"] = (
        len(forbidden_edges) + len(expected["forbidden_paths"]))
    counts["total_candidates"] = (
        counts["expected_candidates"] + counts["negative_candidates"])
    counts["true_positives"] += len(required_edges & typed_edges)
    counts["true_negatives"] += len(forbidden_edges - typed_edges)
    counts["false_negatives"] += len(required_edges - typed_edges)
    counts["false_positives"] += len(forbidden_edges & typed_edges)

    def edge_record(edge: tuple[str, str, str | None]) -> dict[str, str]:
        source, target, outcome = edge
        result = {"source": source, "target": target}
        if outcome is not None:
            result["outcome"] = outcome
        return result

    paths = declared_paths(actual)
    missing_paths: list[list[str]] = []
    forbidden_paths: list[list[str]] = []
    for value in expected["required_paths"]:
        path = tuple(str(node) for node in value)
        if path_present(path, edges, paths):
            counts["true_positives"] += 1
        else:
            counts["false_negatives"] += 1
            missing_paths.append(list(path))
    for value in expected["forbidden_paths"]:
        path = tuple(str(node) for node in value)
        if path_present(path, edges, paths):
            counts["false_positives"] += 1
            forbidden_paths.append(list(path))
        else:
            counts["true_negatives"] += 1
    diagnostics = {
        "missing_nodes": missing_nodes,
        "missing_edges": [
            edge_record(edge)
            for edge in sorted(required_edges - typed_edges,
                               key=lambda value: tuple(str(item) for item in value))
        ],
        "negative_edges_present": [
            edge_record(edge)
            for edge in sorted(forbidden_edges & typed_edges,
                               key=lambda value: tuple(str(item) for item in value))
        ],
        "missing_paths": missing_paths,
        "negative_paths_present": forbidden_paths,
    }
    return counts, diagnostics


def compare_pta(expected_sets: list[object], actual_sets: object
                ) -> tuple[dict[str, int], dict[str, object]]:
    if not isinstance(actual_sets, list):
        raise ValueError("result points_to_sets must be an array")
    actual: dict[str, set[str]] = {}
    for value in actual_sets:
        if not isinstance(value, dict) or \
                not isinstance(value.get("subject"), str) or \
                not isinstance(value.get("points_to"), list):
            raise ValueError("invalid result points-to set")
        subject = str(value["subject"])
        points_to = value["points_to"]
        if any(not isinstance(item, str) or not item for item in points_to):
            raise ValueError("points-to members must be canonical strings")
        actual.setdefault(subject, set()).update(points_to)
    counts = empty_counts()
    missing: dict[str, list[str]] = {}
    forbidden: dict[str, list[str]] = {}
    for value in expected_sets:
        assert isinstance(value, dict)
        subject = str(value["subject"])
        predicted = actual.get(subject, set())
        required = set(str(item) for item in value["points_to"])
        rejected = set(str(item) for item in value["must_not_point_to"])
        counts["expected_candidates"] += len(required)
        counts["negative_candidates"] += len(rejected)
        counts["total_candidates"] += len(required) + len(rejected)
        counts["true_positives"] += len(required & predicted)
        counts["true_negatives"] += len(rejected - predicted)
        counts["false_negatives"] += len(required - predicted)
        counts["false_positives"] += len(rejected & predicted)
        if required - predicted:
            missing[subject] = sorted(required - predicted)
        if rejected & predicted:
            forbidden[subject] = sorted(rejected & predicted)
    return counts, {
        "missing_memberships": missing,
        "negative_memberships_present": forbidden,
    }


def load_result(results: Path, case_id: str) -> dict[str, object]:
    candidates = (results / case_id / "result.json",
                  results / f"{case_id}.json")
    path = next((candidate for candidate in candidates if candidate.is_file()), None)
    if path is None:
        raise ValueError(f"missing result for {case_id}")
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema_version") != 1 or document.get("case") != case_id:
        raise ValueError(f"{path}: invalid result header")
    return document


def compare_case(expected: dict[str, object], actual: dict[str, object]
                 ) -> dict[str, object]:
    if actual.get("schema_version") != 1 or \
            actual.get("case") != expected.get("case"):
        raise ValueError("result header does not match the ground-truth case")
    by_component: dict[str, dict[str, object]] = {}
    total = empty_counts()
    pta_counts, pta_diagnostics = compare_pta(
        expected["points_to_sets"], actual.get("points_to_sets"))
    add_counts(total, pta_counts)
    by_component["pta"] = {
        **finish(pta_counts), "diagnostics": pta_diagnostics}
    missing_nodes = 0
    for component in GRAPH_COMPONENTS:
        actual_graph = actual.get(component)
        if not isinstance(actual_graph, dict):
            raise ValueError(f"result is missing {component} graph")
        counts, diagnostics = compare_graph(expected[component], actual_graph)
        add_counts(total, counts)
        missing_nodes += len(diagnostics["missing_nodes"])
        by_component[component] = {
            **finish(counts), "diagnostics": diagnostics}
    metrics = finish(total)
    passed = (metrics["false_positives"] == 0 and
              metrics["false_negatives"] == 0 and missing_nodes == 0)
    return {
        "case": expected["case"], "passed": passed,
        "overall": metrics, "components": by_component,
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path)
    parser.add_argument("--case")
    parser.add_argument("--category")
    parser.add_argument("--tag")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def self_test() -> None:
    empty_graph = {
        "nodes": [], "required_edges": [], "forbidden_edges": [],
        "required_paths": [], "forbidden_paths": [],
    }
    expected = {
        "case": "sample",
        "points_to_sets": [{
            "subject": "value", "points_to": ["left"],
            "must_not_point_to": ["right"],
        }],
        "cg": {
            "nodes": ["entry", "target", "exit"],
            "required_edges": [
                {"source": "entry", "target": "target"},
                {"source": "target", "target": "exit"},
            ],
            "forbidden_edges": [
                {"source": "entry", "target": "exit"}],
            "required_paths": [["entry", "target", "exit"]],
            "forbidden_paths": [["entry", "exit"]],
        },
        "cfg": dict(empty_graph), "cdg": dict(empty_graph),
        "ddg": dict(empty_graph),
    }
    actual = {
        "schema_version": 1, "case": "sample",
        "points_to_sets": [{"subject": "value", "points_to": ["left"]}],
        "cg": {
            "nodes": ["entry", "target", "exit"],
            "edges": [
                {"source": "entry", "target": "target"},
                {"source": "target", "target": "exit"},
            ],
        },
        "cfg": {"nodes": [], "edges": []},
        "cdg": {"nodes": [], "edges": []},
        "ddg": {"nodes": [], "edges": []},
    }
    report = compare_case(expected, actual)
    assert report["passed"]
    assert report["overall"]["expected_candidates"] == 4
    assert report["overall"]["negative_candidates"] == 3
    assert report["overall"]["total_candidates"] == 7
    assert report["overall"]["true_positives"] == 4
    assert report["overall"]["true_negatives"] == 3
    assert report["overall"]["false_positives"] == 0
    assert report["overall"]["false_negatives"] == 0
    actual["cg"]["edges"].append({"source": "entry", "target": "exit"})
    report = compare_case(expected, actual)
    assert not report["passed"]
    assert report["overall"]["false_positives"] == 2
    assert report["overall"]["true_negatives"] == 1


def main() -> int:
    arguments = parse_arguments()
    if arguments.self_test:
        self_test()
        print('{"canonical_comparator":"PASS"}')
        return 0
    if arguments.results is None:
        raise SystemExit("--results is required unless --self-test is used")
    case_paths = sorted(CASES.glob("*.json"))
    expected_cases = [
        json.loads(path.read_text(encoding="utf-8")) for path in case_paths
    ]
    expected_cases = [
        case for case in expected_cases
        if (arguments.case is not None or supported_case(case))
        and (arguments.case is None or case["case"] == arguments.case)
        and (arguments.category is None or
             str(case["case"]).split(".", 1)[0] == arguments.category)
        and (arguments.tag is None or arguments.tag in case["tags"])
    ]
    if not expected_cases:
        raise SystemExit("no matching PYGBench ground-truth cases")
    results: list[dict[str, object]] = []
    totals = empty_counts()
    failed = 0
    for expected in expected_cases:
        try:
            result = compare_case(
                expected, load_result(arguments.results, str(expected["case"])))
        except (OSError, ValueError, json.JSONDecodeError) as error:
            counts = missing_result_counts(expected)
            result = {"case": expected["case"], "passed": False,
                      "error": str(error), "overall": finish(counts)}
        failed += int(not result["passed"])
        add_counts(totals, result["overall"])
        results.append(result)
        if not arguments.json:
            print(f"[{'PASS' if result['passed'] else 'FAIL'}] {result['case']}")
    report = {
        "schema_version": 1,
        "suite": "PYGBench",
        "cases": len(results),
        "passed": len(results) - failed,
        "failed": failed,
        "evaluation_universe": labeled_candidate_universe(expected_cases),
        "overall": finish(totals),
        "results": results,
    }
    if arguments.output is not None:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
    if arguments.json:
        print(json.dumps(report, sort_keys=True))
    else:
        metrics = report["overall"]
        universe = report["evaluation_universe"]
        candidates = universe["all_scored_candidates"]
        paths = universe["graph_paths"]["all"]
        print(f"PYGBench: {report['passed']}/{report['cases']} passed; "
              f"candidates={candidates['total']} "
              f"(expected={candidates['expected']}, "
              f"negative={candidates['negative']}); "
              f"graph-paths={paths['total']}; "
              f"precision={metrics['precision']:.3f}, "
              f"recall={metrics['recall']:.3f}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
