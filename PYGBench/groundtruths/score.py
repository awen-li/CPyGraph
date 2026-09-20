#!/usr/bin/env python3
"""Score complete PYGBench query observations and aggregate every dimension."""

from __future__ import annotations

import argparse
from collections import defaultdict
import json
from pathlib import Path
import sys
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
GROUND_TRUTHS = ROOT / "groundtruths"
MEASURED = "measured"
UNSUPPORTED = "unsupported"
ANALYSIS_ERROR = "analysis-error"
VALID_STATUSES = {MEASURED, UNSUPPORTED, ANALYSIS_ERROR}
COVERAGE_CONCRETE_ONLY = "concrete-only"
COVERAGE_CONSERVATIVE_TOP = "conservative-top"
COVERAGE_TYPED_UNRESOLVED = "typed-unresolved"
VALID_COVERAGE = {
    COVERAGE_CONCRETE_ONLY,
    COVERAGE_CONSERVATIVE_TOP,
    COVERAGE_TYPED_UNRESOLVED,
}
ANALYSIS_POLICIES = (
    "insensitive",
    "selective-flow",
    "selective-context",
    "selective-path",
    "selective-all",
    "selective-entry",
    "complete",
)
EXCLUDED_LIMITATIONS = {"async-requires-dedicated-model"}


def supported_case(case: dict[str, object]) -> bool:
    limitations = case.get("limitations", [])
    if not isinstance(limitations, list):
        raise ValueError(f"{case.get('id', '<unknown>')} has invalid limitations")
    return not (set(str(value) for value in limitations) & EXCLUDED_LIMITATIONS)


def query_objects(query: dict[str, object], python_version: str,
                  polarity: str) -> list[object]:
    by_version = query.get("objects_by_python_version")
    if isinstance(by_version, dict):
        selected = by_version.get(python_version)
        if isinstance(selected, dict):
            values = selected.get(f"{polarity}_objects")
            if isinstance(values, list):
                return values
    values = query[f"{polarity}_objects"]
    assert isinstance(values, list)
    return values


def labeled_candidate_universe(
        cases: list[dict[str, object]], python_version: str) -> dict[str, object]:
    pta_expected = pta_forbidden = 0
    graph_counts = {
        component: {
            "direct_expected": 0,
            "direct_negative": 0,
            "multi_hop_expected": 0,
            "multi_hop_negative": 0,
        }
        for component in ("cg", "cfg", "cdg", "ddg")
    }
    for case in cases:
        components = case["components"]
        for query in components["pta"].get("queries", []):
            pta_expected += len(query_objects(
                query, python_version, "expected"))
            pta_forbidden += len(query_objects(
                query, python_version, "forbidden"))
        for component in ("cg", "cfg", "cdg", "ddg"):
            counts = graph_counts[component]
            for query in components[component].get("queries", []):
                prefix = ("multi_hop" if query["relation"] ==
                          "may-follow-path" else "direct")
                counts[f"{prefix}_expected"] += len(query_objects(
                    query, python_version, "expected"))
                counts[f"{prefix}_negative"] += len(query_objects(
                    query, python_version, "forbidden"))

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
    return {
        "pta_memberships": population(pta_expected, pta_forbidden),
        "graph_paths": {
            "by_component": by_component,
            "direct": population(direct_expected, direct_negative),
            "multi_hop": population(multi_hop_expected, multi_hop_negative),
            "all": population(graph_expected, graph_negative),
        },
        "all_scored_candidates": population(
            pta_expected + graph_expected,
            pta_forbidden + graph_negative),
    }


def ratio(numerator: int, denominator: int) -> float:
    if denominator == 0:
        return 1.0
    return numerator / denominator


def empty_counts() -> dict[str, int]:
    return {
        "queries": 0,
        "measured_queries": 0,
        "expected_candidates": 0,
        "negative_candidates": 0,
        "total_candidates": 0,
        "sound_queries": 0,
        "passed_queries": 0,
        "true_positives": 0,
        "true_negatives": 0,
        "false_positives": 0,
        "false_negatives": 0,
        "negative_hits": 0,
        "concrete_true_positives": 0,
        "concrete_false_positives": 0,
        "concrete_false_negatives": 0,
        "abstract_true_positives": 0,
        "abstract_false_positives": 0,
    }


def finish_counts(counts: dict[str, int]) -> dict[str, object]:
    result: dict[str, object] = dict(counts)
    predicted = counts["true_positives"] + counts["false_positives"]
    expected = counts["true_positives"] + counts["false_negatives"]
    result["unmeasured_queries"] = counts["queries"] - counts["measured_queries"]
    result["precision"] = ratio(counts["true_positives"], predicted)
    result["recall"] = ratio(counts["true_positives"], expected)
    result["sound_query_rate"] = ratio(
        counts["sound_queries"], counts["measured_queries"])
    result["query_pass_rate"] = ratio(
        counts["passed_queries"], counts["measured_queries"])
    result["complete"] = counts["queries"] == counts["measured_queries"]
    return result


def add_counts(target: dict[str, int], source: dict[str, int]) -> None:
    for name in target:
        target[name] += source[name]


def query_groups(case: dict[str, object], component: str,
                 python_version: str, relation: str) -> Iterable[str]:
    yield "overall"
    yield f"component/{component}"
    yield f"category/{case['category']}"
    yield f"python_version/{python_version}"
    if component == "pta":
        yield "candidate/pta-membership"
    elif relation == "may-follow-path":
        yield "candidate/graph-multi-hop-path"
    else:
        yield "candidate/graph-direct-path"
    dimensions = case["dimensions"]
    assert isinstance(dimensions, dict)
    for dimension, values in dimensions.items():
        if dimension == "python_versions":
            continue
        if isinstance(values, list):
            for value in values:
                yield f"dimension/{dimension}/{value}"
        else:
            yield f"dimension/{dimension}/{values}"


def load_observations(path: Path) -> tuple[dict[str, dict[str, object]], dict[str, object]]:
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema_version") != 2 or document.get("suite") != "PYGBench":
        raise ValueError("unsupported PYGBench observation document")
    values = document.get("queries")
    if not isinstance(values, list):
        raise ValueError("observation document queries must be an array")
    analysis_policy = document.get("analysis_policy")
    if analysis_policy not in ANALYSIS_POLICIES:
        raise ValueError(f"invalid analysis policy: {analysis_policy!r}")
    result: dict[str, dict[str, object]] = {}
    for value in values:
        if not isinstance(value, dict):
            raise ValueError("query observation must be an object")
        query_id = value.get("id")
        status = value.get("status")
        objects = value.get("actual_objects")
        abstract_objects = value.get("abstract_objects")
        coverage = value.get("coverage")
        if not isinstance(query_id, str) or not query_id or query_id in result:
            raise ValueError(f"duplicate or invalid observation ID: {query_id!r}")
        if status not in VALID_STATUSES:
            raise ValueError(f"{query_id} has invalid observation status: {status!r}")
        if coverage not in VALID_COVERAGE:
            raise ValueError(f"{query_id} has invalid coverage: {coverage!r}")
        if not isinstance(objects, list) or any(
                not isinstance(item, str) or not item for item in objects):
            raise ValueError(f"{query_id} actual_objects must contain nonempty strings")
        if len(objects) != len(set(objects)):
            raise ValueError(f"{query_id} contains duplicate actual objects")
        if not isinstance(abstract_objects, list) or any(
                not isinstance(item, str) or not item for item in abstract_objects):
            raise ValueError(
                f"{query_id} abstract_objects must contain nonempty strings")
        if len(abstract_objects) != len(set(abstract_objects)):
            raise ValueError(f"{query_id} contains duplicate abstract objects")
        if set(objects) & set(abstract_objects):
            raise ValueError(f"{query_id} concrete and abstract objects overlap")
        if coverage == COVERAGE_CONCRETE_ONLY and abstract_objects:
            raise ValueError(f"{query_id} has abstract objects without abstract coverage")
        if status != MEASURED and (objects or abstract_objects):
            raise ValueError(f"{query_id} has results despite status {status}")
        result[query_id] = value
    metadata = {
        "python_version": document.get("python_version", "unknown"),
        "analyzer": document.get("analyzer", "unknown"),
        "analysis_policy": analysis_policy,
        "sensitivity_selection": document.get("sensitivity_selection"),
    }
    return result, metadata


def score(catalog: dict[str, object],
          observations: dict[str, dict[str, object]],
          metadata: dict[str, object]) -> dict[str, object]:
    expected_query_ids: set[str] = set()
    query_results: list[dict[str, object]] = []
    group_counts: dict[str, dict[str, int]] = defaultdict(empty_counts)

    cases = catalog.get("cases")
    if not isinstance(cases, list):
        raise ValueError("ground-truth cases must be an array")
    scored_cases = [case for case in cases if supported_case(case)]
    excluded_cases = [case for case in cases if not supported_case(case)]
    known_query_ids: set[str] = set()
    for case in cases:
        components = case["components"]
        assert isinstance(components, dict)
        for component in components.values():
            assert isinstance(component, dict)
            for query in component["queries"]:
                known_query_ids.add(str(query["id"]))
    for case in scored_cases:
        if not isinstance(case, dict):
            raise ValueError("ground-truth case must be an object")
        components = case["components"]
        assert isinstance(components, dict)
        for component_name, component in components.items():
            assert isinstance(component, dict)
            queries = component["queries"]
            assert isinstance(queries, list)
            for query in queries:
                assert isinstance(query, dict)
                query_id = str(query["id"])
                expected_query_ids.add(query_id)
                observed = observations.get(query_id, {
                    "id": query_id,
                    "status": UNSUPPORTED,
                    "actual_objects": [],
                    "abstract_objects": [],
                    "coverage": COVERAGE_CONCRETE_ONLY,
                    "detail": "missing observation",
                })
                status = str(observed["status"])
                expected = set(str(item) for item in query_objects(
                    query, str(metadata["python_version"]), "expected"))
                forbidden = set(str(item) for item in query_objects(
                    query, str(metadata["python_version"]), "forbidden"))
                actual = set(str(item) for item in observed["actual_objects"])
                abstract = set(str(item) for item in observed["abstract_objects"])
                # Abstract groups are projected only onto declared package
                # candidates. Their residual external targets are diagnostics
                # and never become ground-truth objects.
                coverage = str(observed["coverage"])
                predicted = actual | abstract
                summary_coverage = actual | abstract
                domain = expected | forbidden
                outside_domain = summary_coverage - expected - forbidden
                if outside_domain:
                    raise ValueError(
                        f"{query_id} reports objects outside its candidate domain: "
                        f"{sorted(outside_domain)}")
                if (coverage == COVERAGE_CONSERVATIVE_TOP and
                        summary_coverage != domain):
                    raise ValueError(
                        f"{query_id} conservative-top does not cover its complete "
                        f"candidate domain: {sorted(domain - predicted)}")
                measured = status == MEASURED
                true_positives = len(expected & predicted) if measured else 0
                false_positives = len(predicted - expected) if measured else 0
                false_negatives = len(expected - predicted) if measured else 0
                negative_hits = len(forbidden & predicted) if measured else 0
                concrete_true_positives = len(expected & actual) if measured else 0
                concrete_false_positives = len(actual - expected) if measured else 0
                concrete_false_negatives = len(expected - actual) if measured else 0
                abstract_true_positives = len(expected & abstract) if measured else 0
                abstract_false_positives = len(abstract - expected) if measured else 0
                true_negatives = len(forbidden - predicted) if measured else 0
                sound = measured and false_negatives == 0
                passed = measured and expected == predicted
                counts = {
                    "queries": 1,
                    "measured_queries": int(measured),
                    "expected_candidates": len(expected),
                    "negative_candidates": len(forbidden),
                    "total_candidates": len(domain),
                    "sound_queries": int(sound),
                    "passed_queries": int(passed),
                    "true_positives": true_positives,
                    "true_negatives": true_negatives,
                    "false_positives": false_positives,
                    "false_negatives": false_negatives,
                    "negative_hits": negative_hits,
                    "concrete_true_positives": concrete_true_positives,
                    "concrete_false_positives": concrete_false_positives,
                    "concrete_false_negatives": concrete_false_negatives,
                    "abstract_true_positives": abstract_true_positives,
                    "abstract_false_positives": abstract_false_positives,
                }
                for group in query_groups(
                        case, str(component_name), str(metadata["python_version"]),
                        str(query["relation"])):
                    add_counts(group_counts[group], counts)
                query_results.append({
                    "id": query_id,
                    "case": case["id"],
                    "component": component_name,
                    "status": status,
                    "coverage": coverage,
                    "sound": sound,
                    "passed": passed,
                    "expected_objects": sorted(expected),
                    "actual_objects": sorted(actual),
                    "abstract_objects": sorted(abstract),
                    "predicted_objects": sorted(predicted),
                    "summary_coverage_objects": sorted(summary_coverage),
                    "missing_objects": sorted(expected - predicted) if measured else [],
                    "unexpected_objects": sorted(predicted - expected) if measured else [],
                    "negative_hits": sorted(forbidden & predicted) if measured else [],
                    "detail": observed.get("detail", ""),
                })

    extras = sorted(set(observations) - known_query_ids)
    if extras:
        raise ValueError(f"observations contain unknown query IDs: {extras}")
    groups = {name: finish_counts(counts)
              for name, counts in sorted(group_counts.items())}
    overall = groups["overall"]
    return {
        "schema_version": 2,
        "suite": "PYGBench",
        "analyzer": metadata["analyzer"],
        "python_version": metadata["python_version"],
        "analysis_policy": metadata.get("analysis_policy", "insensitive"),
        "sensitivity_selection": metadata.get("sensitivity_selection"),
        "complete": overall["complete"],
        "scope": {
            "scored_cases": len(scored_cases),
            "excluded_cases": len(excluded_cases),
            "excluded_case_ids": sorted(str(case["id"])
                                        for case in excluded_cases),
            "excluded_limitations": sorted(EXCLUDED_LIMITATIONS),
        },
        "evaluation_universe": labeled_candidate_universe(
            scored_cases, str(metadata["python_version"])),
        "overall": overall,
        "groups": groups,
        "queries": query_results,
    }


def self_test() -> None:
    catalog = {
        "cases": [{
            "id": "sample",
            "category": "cg",
            "dimensions": {
                "analysis_component": ["cg"],
                "sensitivity": ["context-sensitive"],
                "scope": ["interprocedural"],
                "dispatch": ["indirect-call"],
                "control": ["straight-line"],
                "oracle_role": ["positive-negative-pair"],
                "language_feature": ["callback"],
                "layer": "micro",
                "python_versions": {"minimum": "3.10", "maximum": "3.14"},
            },
            "components": {
                "cg": {"queries": [{
                    "id": "q1", "expected_objects": ["left", "middle"],
                    "forbidden_objects": ["right"],
                    "relation": "may-call",
                }]},
                "pta": {"queries": []}, "cfg": {"queries": []},
                "cdg": {"queries": []},
                "ddg": {"queries": []},
            },
        }, {
            "id": "async-probe",
            "limitations": ["async-requires-dedicated-model"],
            "category": "cfg",
            "dimensions": {
                "analysis_component": ["cfg"],
                "sensitivity": ["baseline"],
                "scope": ["intraprocedural"],
                "dispatch": ["direct-call"],
                "control": ["suspension"],
                "oracle_role": ["positive-negative-pair"],
                "language_feature": ["async"],
                "layer": "micro",
                "python_versions": {"minimum": "3.10", "maximum": "3.14"},
            },
            "components": {
                "cfg": {"queries": [{
                    "id": "async-q", "expected_objects": ["resume"],
                    "forbidden_objects": ["interleave"],
                    "relation": "may-transfer-control-to",
                }]},
                "pta": {"queries": []}, "cg": {"queries": []},
                "cdg": {"queries": []}, "ddg": {"queries": []},
            },
        }],
    }
    observations = {"q1": {
        "id": "q1", "status": MEASURED,
        "actual_objects": ["left"],
        "abstract_objects": ["middle", "right"],
        "coverage": COVERAGE_CONSERVATIVE_TOP,
    }}
    report = score(catalog, observations, {
        "analyzer": "self-test", "python_version": "test"})
    overall = report["overall"]
    assert overall["queries"] == 1
    assert overall["expected_candidates"] == 2
    assert overall["negative_candidates"] == 1
    assert overall["total_candidates"] == 3
    assert overall["sound_queries"] == 1
    assert overall["true_positives"] == 2
    assert overall["true_negatives"] == 0
    assert overall["false_positives"] == 1
    assert overall["false_negatives"] == 0
    assert overall["negative_hits"] == 1
    assert overall["concrete_true_positives"] == 1
    assert overall["abstract_true_positives"] == 1
    assert overall["abstract_false_positives"] == 1
    assert overall["precision"] == 2 / 3
    assert overall["recall"] == 1.0
    assert report["groups"]["dimension/sensitivity/context-sensitive"] == overall
    assert report["scope"] == {
        "scored_cases": 1,
        "excluded_cases": 1,
        "excluded_case_ids": ["async-probe"],
        "excluded_limitations": ["async-requires-dedicated-model"],
    }
    selective_top = {"q1": {
        "id": "q1", "status": MEASURED,
        "actual_objects": ["left"], "abstract_objects": ["middle"],
        "coverage": COVERAGE_CONSERVATIVE_TOP,
    }}
    try:
        score(catalog, selective_top, {
            "analyzer": "self-test", "python_version": "test"})
        raise AssertionError("selective conservative top was accepted")
    except ValueError as error:
        assert "complete candidate domain" in str(error)
    typed_unresolved = {"q1": {
        "id": "q1", "status": MEASURED,
        "actual_objects": ["left"], "abstract_objects": ["middle"],
        "coverage": COVERAGE_TYPED_UNRESOLVED,
    }}
    typed_report = score(catalog, typed_unresolved, {
        "analyzer": "self-test", "python_version": "test"})
    assert typed_report["overall"]["recall"] == 1.0
    assert typed_report["overall"]["false_positives"] == 0
    assert typed_report["overall"]["true_negatives"] == 1
    assert score(catalog, {}, {
        "analyzer": "self-test", "python_version": "test"})["complete"] is False


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ground-truth", type=Path,
                        default=GROUND_TRUTHS / "oracle.json")
    parser.add_argument("--observations", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--require-complete", action="store_true")
    parser.add_argument("--require-sound-recall", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.self_test:
        self_test()
        print('{"scorer_self_test":"PASS"}')
        return 0
    if arguments.observations is None:
        raise SystemExit("--observations is required unless --self-test is used")
    catalog = json.loads(arguments.ground_truth.read_text(encoding="utf-8"))
    observations, metadata = load_observations(arguments.observations)
    try:
        report = score(catalog, observations, metadata)
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 2
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if arguments.output:
        arguments.output.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    complete = bool(report["complete"])
    groups = report["groups"]
    assert isinstance(groups, dict)
    sound = all(float(metrics["recall"]) == 1.0
                for metrics in groups.values())
    if arguments.require_complete and not complete:
        return 1
    if arguments.require_sound_recall and not sound:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
