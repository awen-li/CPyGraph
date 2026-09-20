#!/usr/bin/env python3
"""Build or check PYGBench's normalized, source-independent fact catalog."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
from pathlib import Path
import re

from validate import GROUND_TRUTHS, ROOT, parse_oracle


CATALOG_PATH = GROUND_TRUTHS / "oracle.json"
CASE_GROUND_TRUTHS = GROUND_TRUTHS / "cases"
CASE_ARTIFACTS = GROUND_TRUTHS / "artifacts"
COMPONENTS = ("pta", "cg", "cfg", "cdg", "ddg")
FACT_COMPONENT = {
    "points-to": "pta",
    "call": "cg",
    "cfg": "cfg",
    "cdg": "cdg",
    "cdg-true": "cdg",
    "cdg-false": "cdg",
    "cdg-normal": "cdg",
    "cdg-exception": "cdg",
    "cdg-resume": "cdg",
    "ddg": "ddg",
}
FACT_RELATION = {
    "points-to": "may-point-to",
    "call": "may-call",
    "cfg": "may-transfer-control-to",
    "cdg": "may-control",
    "cdg-true": "may-control-true",
    "cdg-false": "may-control-false",
    "cdg-normal": "may-control-normal",
    "cdg-exception": "may-control-exception",
    "cdg-resume": "may-control-resume",
    "ddg": "may-depend-on",
}
SENSITIVITY_ATTRIBUTES = ["flow", "context", "path"]
PATH_COMPONENTS = ("cg", "cfg", "cdg", "ddg")
PATH_RELATION = "may-follow-path"
NON_TARGET_MARKERS = {"group", "unknown", "external", "runtime"}
SUPPORTED_PYTHON_VERSIONS = ("3.10", "3.11", "3.12", "3.13", "3.14")
VERSIONED_FACT = re.compile(
    r"^(3\.(?:10|11|12|13|14))\.\.(3\.(?:10|11|12|13|14))\s*\|\s*(.+)$")


def fact_text(value: str) -> tuple[str, str, str]:
    match = VERSIONED_FACT.fullmatch(value)
    if match is None:
        return SUPPORTED_PYTHON_VERSIONS[0], SUPPORTED_PYTHON_VERSIONS[-1], value
    minimum, maximum, fact = match.groups()
    if SUPPORTED_PYTHON_VERSIONS.index(minimum) > \
            SUPPORTED_PYTHON_VERSIONS.index(maximum):
        raise AssertionError(f"invalid Python version interval: {value!r}")
    return minimum, maximum, fact


def pta_configurations() -> dict[str, object]:
    configurations = {
        "selective-all": {
            "level": "selective",
            "functions": [{
                "selector": "*",
                "attributes": list(SENSITIVITY_ATTRIBUTES),
            }],
        },
    }
    for attribute in SENSITIVITY_ATTRIBUTES:
        configurations[f"selective-{attribute}"] = {
            "level": "selective",
            "functions": [{"selector": "*", "attributes": [attribute]}],
        }
    return configurations


def entry_points(source: str) -> list[str]:
    parsed = ast.parse(source)
    functions = {
        node.name: node for node in parsed.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and not node.name.startswith("_")
    }
    referenced: set[str] = set()
    for function in functions.values():
        for node in ast.walk(function):
            if isinstance(node, ast.Name) and isinstance(node.ctx, ast.Load) \
                    and node.id in functions and node.id != function.name:
                referenced.add(node.id)
    excluded_names = {"decoy", "unrelated", "dead", "unused"}
    roots = [name for name in functions
             if name not in referenced and name not in excluded_names]
    if not roots:
        roots = [name for name in functions if name not in excluded_names]
    return ["<module>", *roots]


def _direct_calls(node: ast.AST) -> set[str]:
    """Return syntactic calls in one body without entering nested bodies."""
    calls: set[str] = set()

    class Visitor(ast.NodeVisitor):
        def visit_Call(self, call: ast.Call) -> None:
            if isinstance(call.func, ast.Name):
                calls.add(call.func.id)
            elif isinstance(call.func, ast.Attribute):
                calls.add(call.func.attr)
            self.generic_visit(call)

        def visit_FunctionDef(self, function: ast.FunctionDef) -> None:
            if function is node:
                self.generic_visit(function)

        def visit_AsyncFunctionDef(
                self, function: ast.AsyncFunctionDef) -> None:
            if function is node:
                self.generic_visit(function)

        def visit_Lambda(self, function: ast.Lambda) -> None:
            if function is node:
                self.generic_visit(function)

        def visit_ClassDef(self, class_: ast.ClassDef) -> None:
            if class_ is node:
                self.generic_visit(class_)

    if isinstance(node, ast.Module):
        for statement in node.body:
            if not isinstance(statement, (ast.FunctionDef,
                                          ast.AsyncFunctionDef,
                                          ast.ClassDef)):
                Visitor().visit(statement)
    else:
        Visitor().visit(node)
    return calls


def _direct_returns(node: ast.AST) -> list[ast.Return]:
    returns: list[ast.Return] = []

    class Visitor(ast.NodeVisitor):
        def visit_Return(self, returned: ast.Return) -> None:
            returns.append(returned)

        def visit_FunctionDef(self, function: ast.FunctionDef) -> None:
            if function is node:
                self.generic_visit(function)

        def visit_AsyncFunctionDef(
                self, function: ast.AsyncFunctionDef) -> None:
            if function is node:
                self.generic_visit(function)

        def visit_Lambda(self, function: ast.Lambda) -> None:
            if function is node:
                self.generic_visit(function)

        def visit_ClassDef(self, class_: ast.ClassDef) -> None:
            if class_ is node:
                self.generic_visit(class_)

    Visitor().visit(node)
    return returns


def structural_ground_truth(source: str) -> dict[str, dict[str, list[str]]]:
    """Build analyzer-independent baseline facts shared by every case.

    Hand-written annotations remain the feature oracle. These structural facts
    ensure that the same program also has a finite scored universe for every
    other analysis component.
    """
    parsed = ast.parse(source)
    top_functions = [
        node for node in parsed.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
    ]
    classes = [node for node in parsed.body if isinstance(node, ast.ClassDef)]
    methods = [
        method for class_ in classes for method in class_.body
        if isinstance(method, (ast.FunctionDef, ast.AsyncFunctionDef))
    ]
    root_names = set(entry_points(source)[1:])
    root_functions = [
        function for function in top_functions
        if function.name in root_names
    ]
    primary = root_functions[0] if root_functions else (
        top_functions[0] if top_functions else (
        methods[0] if methods else None)
    )
    primary_name = primary.name if primary is not None else "module"
    facts = {
        component: {"expected": [], "forbidden": []}
        for component in COMPONENTS
    }

    # Every code object has one entry and an explicit or adapter-synthesized
    # return, providing a shared minimum CFG fact.
    facts["cfg"]["expected"].append(
        f"cfg:{primary_name} entry -> {primary_name} return")
    facts["cdg"]["forbidden"].append(
        "cdg:module entry -> module entry")
    function_names = {node.name for node in top_functions}
    method_names = {node.name for node in methods}
    class_initializers = {
        class_.name: next((
            f"{class_.name}.__init__"
            for method in class_.body
            if isinstance(method, (ast.FunctionDef, ast.AsyncFunctionDef)) and
            method.name == "__init__"), None)
        for class_ in classes
    }
    defined_names = function_names | method_names | {
        initializer for initializer in class_initializers.values()
        if initializer is not None
    }
    callers: list[tuple[str, ast.AST]] = [
        ("module", parsed),
        *((node.name, node) for node in [*top_functions, *methods]),
    ]
    selected_call: tuple[str, str] | None = None
    selected_calls: set[str] = set()
    selected_caller = primary_name
    for caller, body in callers:
        calls = _direct_calls(body)
        targets = sorted(
            (calls & (function_names | method_names)) |
            {initializer for class_name, initializer
             in class_initializers.items()
             if class_name in calls and initializer is not None})
        if targets:
            selected_caller = caller
            selected_calls = calls
            selected_call = (caller, targets[0])
            break
    if selected_call is not None:
        caller, target = selected_call
        facts["cg"]["expected"].append(
            f"call:{caller} -> {target}")
        negative_targets = sorted(
            defined_names - selected_calls - {caller, target})
        if negative_targets:
            facts["cg"]["forbidden"].append(
                f"call:{caller} -> {negative_targets[0]}")
        elif caller != target:
            facts["cg"]["forbidden"].append(
                f"call:{target} -> {caller}")
    else:
        target = sorted(defined_names)[0] if defined_names else "module"
        facts["cg"]["forbidden"].append(
            f"call:{selected_caller} -> {target}")

    parameter_owner: ast.FunctionDef | ast.AsyncFunctionDef | None = None
    parameters: list[str] = []
    for candidate in [*root_functions, *top_functions]:
        arguments = candidate.args
        candidate_parameters = [
            argument.arg for argument in [
                *arguments.posonlyargs, *arguments.args,
                *arguments.kwonlyargs]
            if argument.arg not in {"self", "cls"}
        ]
        loaded_names = {
            name.id for returned in _direct_returns(candidate)
            for name in ast.walk(returned)
            if isinstance(name, ast.Name) and isinstance(name.ctx, ast.Load)
        }
        usable = [name for name in candidate_parameters
                  if name in loaded_names]
        if usable:
            parameter_owner = candidate
            parameters = usable
            break
    if parameters:
        assert parameter_owner is not None
        parameter_owner_name = parameter_owner.name
        parameter = parameters[0]
        facts["pta"]["expected"].append(
            f"points-to:{parameter_owner_name} {parameter} -> "
            f"{parameter_owner_name} parameter {parameter}")
    else:
        target = sorted(function_names)[0] if function_names else "module"
        facts["pta"]["forbidden"].append(
            f"points-to:module return -> {target} function object")

    ddg_source = f"{primary_name} constant"
    if primary is not None:
        returns = [node for node in _direct_returns(primary)
                   if node.value is not None]
        if returns:
            value = returns[0].value
            if isinstance(value, ast.Attribute):
                ddg_source = (
                    f"{primary_name} load attribute {value.attr}")
            elif isinstance(value, ast.Subscript):
                ddg_source = f"{primary_name} load element"
            elif isinstance(value, ast.Call):
                ddg_source = f"{primary_name} call"
            elif isinstance(value, ast.Name):
                ddg_source = f"{primary_name} {value.id}"
            elif not isinstance(value, (ast.Constant, ast.IfExp)):
                ddg_source = f"{primary_name} operation"
    ddg_target = f"{primary_name} return"
    facts["ddg"]["expected"].append(
        f"ddg:{ddg_source} -> {ddg_target}")
    return facts


def typed_facts(values: object, case_id: str,
                polarity: str) -> dict[str, list[dict[str, str]]]:
    result: dict[str, list[dict[str, str]]] = {component: [] for component in COMPONENTS}
    if not isinstance(values, list):
        raise AssertionError(f"{case_id} facts must be a list")
    for index, value in enumerate(values):
        if not isinstance(value, str) or ":" not in value:
            raise AssertionError(f"{case_id} has malformed fact: {value!r}")
        minimum, maximum, value = fact_text(value)
        kind, relation = value.split(":", 1)
        component = FACT_COMPONENT.get(kind)
        if component is None:
            raise AssertionError(f"{case_id} has unknown fact kind: {kind!r}")
        if relation.count("->") != 1:
            raise AssertionError(f"{case_id} fact is not binary: {value!r}")
        subject, object_ = (item.strip() for item in relation.split("->", 1))
        if not subject or not object_:
            raise AssertionError(f"{case_id} fact has an empty endpoint: {value!r}")
        if kind == "call" and NON_TARGET_MARKERS & set(object_.lower().split()):
            raise AssertionError(
                f"{case_id} call ground truth must name a package target: {object_!r}")
        result[component].append({
            "id": f"{case_id}:{component}:{polarity}:{index}",
            "kind": kind,
            "subject": subject,
            "relation": FACT_RELATION[kind],
            "object": object_,
            "minimum_python": minimum,
            "maximum_python": maximum,
        })
    return result


def typed_paths(values: object, case_id: str,
                polarity: str) -> dict[str, list[dict[str, object]]]:
    result: dict[str, list[dict[str, object]]] = {
        component: [] for component in PATH_COMPONENTS
    }
    if not isinstance(values, list):
        raise AssertionError(f"{case_id} paths must be a list")
    for index, value in enumerate(values):
        if not isinstance(value, str) or ":" not in value:
            raise AssertionError(f"{case_id} has malformed path: {value!r}")
        component, path_text = value.split(":", 1)
        if component not in PATH_COMPONENTS:
            raise AssertionError(
                f"{case_id} has unknown path component: {component!r}")
        nodes = [node.strip() for node in path_text.split("->")]
        if len(nodes) < 2 or any(not node for node in nodes):
            raise AssertionError(f"{case_id} has malformed path: {value!r}")
        if len(nodes) != len(set(nodes)):
            raise AssertionError(
                f"{case_id} path must be finite and simple: {value!r}")
        result[component].append({
            "id": f"{case_id}:{component}:path:{polarity}:{index}",
            "nodes": nodes,
        })
    return result


MAX_COMPOSED_PATH_NODES = 8


def composed_paths(
        case_id: str, component: str,
        expected: list[dict[str, str]],
        forbidden: list[dict[str, str]],
        explicit_expected: list[dict[str, object]],
        explicit_forbidden: list[dict[str, object]],
) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    """Derive finite multi-hop candidates from independently curated edges."""
    expected_edges = {
        (fact["subject"], fact["object"]) for fact in expected
    }
    forbidden_edges = {
        (fact["subject"], fact["object"]) for fact in forbidden
    }

    def enumerate_paths(
            edges: set[tuple[str, str]]) -> set[tuple[str, ...]]:
        adjacency: dict[str, set[str]] = {}
        for source, target in edges:
            adjacency.setdefault(source, set()).add(target)
        result: set[tuple[str, ...]] = set()
        for source, target in edges:
            if source == target:
                continue
            pending = [(source, target)]
            while pending:
                nodes = pending.pop()
                if len(nodes) >= 3:
                    result.add(nodes)
                if len(nodes) >= MAX_COMPOSED_PATH_NODES:
                    continue
                for successor in adjacency.get(nodes[-1], set()):
                    if successor in nodes:
                        continue
                    pending.append((*nodes, successor))
        return result

    expected_paths = enumerate_paths(expected_edges)
    negative_paths = enumerate_paths(forbidden_edges)

    # A manually curated long expected path also establishes all of its
    # contiguous multi-hop subpaths, even when intermediate semantic steps are
    # intentionally more detailed than the direct-edge oracle.
    for path in explicit_expected:
        nodes = path["nodes"]
        assert isinstance(nodes, list)
        for begin in range(len(nodes)):
            for end in range(begin + 3, len(nodes) + 1):
                expected_paths.add(tuple(str(node) for node in nodes[begin:end]))

    explicit_expected_nodes = {
        tuple(str(node) for node in path["nodes"])
        for path in explicit_expected
    }
    explicit_forbidden_nodes = {
        tuple(str(node) for node in path["nodes"])
        for path in explicit_forbidden
    }
    expected_paths -= explicit_expected_nodes
    negative_paths -= explicit_forbidden_nodes
    overlap = expected_paths & (negative_paths | explicit_forbidden_nodes)
    if overlap:
        raise AssertionError(
            f"{case_id}:{component} derives contradictory paths: "
            f"{sorted(overlap)}")

    def materialize(
            paths: set[tuple[str, ...]], polarity: str,
    ) -> list[dict[str, object]]:
        return [{
            "id": (
                f"{case_id}:{component}:path:{polarity}:composed:{index}"),
            "nodes": list(nodes),
        } for index, nodes in enumerate(sorted(paths))]

    return (materialize(expected_paths, "expected"),
            materialize(negative_paths, "forbidden"))


def classify_case(oracle: dict[str, object], components: dict[str, object],
                  manifest: dict[str, object]) -> dict[str, object]:
    tags_value = oracle["tags"]
    if not isinstance(tags_value, list):
        raise AssertionError(f"{oracle['id']} tags must be a list")
    tags = {str(tag) for tag in tags_value}
    taxonomy = manifest["dimension_taxonomy"]
    if not isinstance(taxonomy, dict):
        raise AssertionError("dimension taxonomy must be an object")
    classified_tags: set[str] = set()
    dimensions: dict[str, object] = {}
    for dimension, specification in taxonomy.items():
        if not isinstance(specification, dict):
            raise AssertionError(f"dimension {dimension} must be an object")
        vocabulary = set(str(tag) for tag in specification["tags"])
        overlap = classified_tags & vocabulary
        if overlap:
            raise AssertionError(f"dimension tags overlap: {sorted(overlap)}")
        values = sorted(tags & vocabulary)
        dimensions[str(dimension)] = values or [str(specification["default"])]
        classified_tags.update(vocabulary)

    language_features = sorted(tags - classified_tags)
    dimensions["language_feature"] = language_features or [str(oracle["category"])]
    dimensions["analysis_component"] = sorted(
        name for name, component in components.items()
        if isinstance(component, dict) and component["applicability"] == "targeted")
    if not dimensions["analysis_component"]:
        raise AssertionError(f"{oracle['id']} targets no analysis component")
    is_package = bool(tags & {"package", "cross-module"})
    targeted_count = len(dimensions["analysis_component"])
    dimensions["layer"] = (
        "package" if is_package else
        "interaction" if targeted_count > 1 else
        "micro"
    )
    dimensions["python_versions"] = {"minimum": "3.10", "maximum": "3.14"}
    return dimensions


def build_queries(case_id: str, component: str,
                  expected: list[dict[str, str]],
                  forbidden: list[dict[str, str]]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, str], dict[str, list[dict[str, str]]]] = {}
    for polarity, facts in (("expected", expected), ("forbidden", forbidden)):
        for fact in facts:
            key = (fact["subject"], fact["relation"])
            grouped.setdefault(key, {"expected": [], "forbidden": []})[
                polarity].append(fact)
    queries: list[dict[str, object]] = []
    for (subject, relation), objects in sorted(grouped.items()):
        identity = hashlib.sha256(
            f"{case_id}\0{component}\0{subject}\0{relation}".encode("utf-8")
        ).hexdigest()[:16]
        by_version = {}
        for version in SUPPORTED_PYTHON_VERSIONS:
            version_index = SUPPORTED_PYTHON_VERSIONS.index(version)
            by_version[version] = {
                f"{polarity}_objects": sorted({
                    fact["object"] for fact in facts
                    if SUPPORTED_PYTHON_VERSIONS.index(
                        fact["minimum_python"]) <= version_index <=
                    SUPPORTED_PYTHON_VERSIONS.index(
                        fact["maximum_python"])
                })
                for polarity, facts in objects.items()
            }
        default = by_version[SUPPORTED_PYTHON_VERSIONS[0]]
        query = {
            "id": f"{case_id}:{component}:query:{identity}",
            "subject": subject,
            "relation": relation,
            "expected_objects": default["expected_objects"],
            "forbidden_objects": default["forbidden_objects"],
            "domain": "declared-candidate-objects",
            "complete": True,
        }
        if any(candidates != default for candidates in by_version.values()):
            query["objects_by_python_version"] = by_version
        queries.append(query)
    return queries


def build_path_queries(case_id: str, component: str,
                       expected: list[dict[str, object]],
                       forbidden: list[dict[str, object]]) -> list[dict[str, object]]:
    grouped: dict[str, dict[str, set[str]]] = {}
    for polarity, paths in (("expected", expected), ("forbidden", forbidden)):
        for path in paths:
            nodes = path["nodes"]
            assert isinstance(nodes, list)
            encoded = json.dumps(nodes, separators=(",", ":"))
            grouped.setdefault(str(nodes[0]), {
                "expected": set(), "forbidden": set()
            })[polarity].add(encoded)
    queries: list[dict[str, object]] = []
    for subject, paths in sorted(grouped.items()):
        identity = hashlib.sha256(
            f"{case_id}\0{component}\0{subject}\0{PATH_RELATION}".encode("utf-8")
        ).hexdigest()[:16]
        queries.append({
            "id": f"{case_id}:{component}:query:{identity}",
            "subject": subject,
            "relation": PATH_RELATION,
            "expected_objects": sorted(paths["expected"]),
            "forbidden_objects": sorted(paths["forbidden"]),
            "domain": "declared-candidate-paths",
            "complete": True,
        })
    return queries


def catalog_statistics(cases: list[dict[str, object]]) -> dict[str, object]:
    def population(expected: int, negative: int) -> dict[str, int]:
        return {
            "expected": expected,
            "negative": negative,
            "total": expected + negative,
        }

    by_component: dict[str, dict[str, object]] = {}
    total_targeted_sections = 0
    total_query_groups = 0
    graph_direct_expected = graph_direct_negative = 0
    graph_multi_hop_expected = graph_multi_hop_negative = 0
    pta_expected = pta_negative = 0
    for component_name in COMPONENTS:
        targeted_sections = query_groups = 0
        direct_expected = direct_negative = 0
        multi_hop_expected = multi_hop_negative = 0
        for case in cases:
            components = case["components"]
            assert isinstance(components, dict)
            component = components[component_name]
            assert isinstance(component, dict)
            targeted_sections += int(
                component["applicability"] == "targeted")
            queries = component["queries"]
            assert isinstance(queries, list)
            query_groups += len(queries)
            for query in queries:
                assert isinstance(query, dict)
                expected_count = len(query["expected_objects"])
                negative_count = len(query["forbidden_objects"])
                if query["relation"] == PATH_RELATION:
                    multi_hop_expected += expected_count
                    multi_hop_negative += negative_count
                else:
                    direct_expected += expected_count
                    direct_negative += negative_count
        total_targeted_sections += targeted_sections
        total_query_groups += query_groups
        if component_name == "pta":
            pta_expected = direct_expected
            pta_negative = direct_negative
            by_component[component_name] = {
                "targeted_sections": targeted_sections,
                "query_groups": query_groups,
                "memberships": population(direct_expected, direct_negative),
            }
            continue
        direct = population(direct_expected, direct_negative)
        multi_hop = population(multi_hop_expected, multi_hop_negative)
        all_paths = population(
            direct_expected + multi_hop_expected,
            direct_negative + multi_hop_negative)
        by_component[component_name] = {
            "targeted_sections": targeted_sections,
            "query_groups": query_groups,
            "paths": {
                "direct": direct,
                "multi_hop": multi_hop,
                "all": all_paths,
            },
        }
        graph_direct_expected += direct_expected
        graph_direct_negative += direct_negative
        graph_multi_hop_expected += multi_hop_expected
        graph_multi_hop_negative += multi_hop_negative

    graph_expected = graph_direct_expected + graph_multi_hop_expected
    graph_negative = graph_direct_negative + graph_multi_hop_negative
    return {
        "cases": len(cases),
        "targeted_sections": total_targeted_sections,
        "query_groups": total_query_groups,
        "by_component": by_component,
        "evaluation_universe": {
            "pta_memberships": population(pta_expected, pta_negative),
            "graph_paths": {
                "direct": population(
                    graph_direct_expected, graph_direct_negative),
                "multi_hop": population(
                    graph_multi_hop_expected, graph_multi_hop_negative),
                "all": population(graph_expected, graph_negative),
            },
            "all_scored_candidates": population(
                pta_expected + graph_expected,
                pta_negative + graph_negative),
        },
    }


def build_catalog() -> dict[str, object]:
    manifest = json.loads(
        (GROUND_TRUTHS / "manifest.json").read_text(encoding="utf-8"))
    cases: list[dict[str, object]] = []
    for relative_root in manifest["case_roots"]:
        for path in sorted((ROOT / relative_root).rglob("*.py")):
            source = path.read_text(encoding="utf-8")
            oracle = parse_oracle(source)
            if oracle is None:
                continue
            case_id = str(oracle["id"])
            raw_expected = list(oracle["expect"])
            raw_forbidden = list(oracle["forbid"])
            structural = structural_ground_truth(source)
            declared_components = {
                FACT_COMPONENT[fact_text(value)[2].split(":", 1)[0]]
                for value in [*raw_expected, *raw_forbidden]
                if isinstance(value, str) and ":" in value and
                fact_text(value)[2].split(":", 1)[0] in FACT_COMPONENT
            }
            for value in [*oracle["expect_paths"],
                          *oracle["forbid_paths"]]:
                if isinstance(value, str) and ":" in value:
                    component = value.split(":", 1)[0]
                    if component in PATH_COMPONENTS:
                        declared_components.add(component)
            for component in COMPONENTS:
                if component in declared_components:
                    continue
                raw_expected.extend(structural[component]["expected"])
                raw_forbidden.extend(structural[component]["forbidden"])
            expected = typed_facts(raw_expected, case_id, "expected")
            forbidden = typed_facts(raw_forbidden, case_id, "forbidden")
            expected_paths = typed_paths(
                oracle["expect_paths"], case_id, "expected")
            forbidden_paths = typed_paths(
                oracle["forbid_paths"], case_id, "forbidden")
            components: dict[str, object] = {}
            for component in COMPONENTS:
                component_expected_paths = list(
                    expected_paths.get(component, []))
                component_forbidden_paths = list(
                    forbidden_paths.get(component, []))
                if component in PATH_COMPONENTS:
                    composed_expected, composed_forbidden = composed_paths(
                        case_id, component,
                        expected[component], forbidden[component],
                        component_expected_paths,
                        component_forbidden_paths)
                    component_expected_paths.extend(composed_expected)
                    component_forbidden_paths.extend(composed_forbidden)
                has_facts = bool(
                    expected[component] or forbidden[component] or
                    component_expected_paths or component_forbidden_paths)
                components[component] = {
                    "applicability": "targeted" if has_facts else "not_targeted",
                    "expected": expected[component],
                    "forbidden": forbidden[component],
                    "expected_paths": component_expected_paths,
                    "forbidden_paths": component_forbidden_paths,
                    "queries": (
                        build_queries(case_id, component, expected[component],
                                      forbidden[component]) +
                        build_path_queries(
                            case_id, component, component_expected_paths,
                            component_forbidden_paths)),
                }
            dimensions = classify_case(oracle, components, manifest)
            tags = oracle["tags"]
            assert isinstance(tags, list)
            cases.append({
                "id": case_id,
                "category": oracle["category"],
                "tags": tags,
                "limitations": (["async-requires-dedicated-model"]
                                if "async" in tags else []),
                "source": str(path.relative_to(ROOT)),
                "entry_points": entry_points(source),
                "pta_configurations": pta_configurations(),
                "dimensions": dimensions,
                "components": components,
            })
    cases.sort(key=lambda case: str(case["id"]))
    return {
        "schema_version": 6,
        "suite": "PYGBench",
        "provenance": (
            "manually curated feature annotations plus analyzer-independent "
            "structural source facts; never CPyGraph output"),
        "call_graph_scope": "analyzed-package-defined-code-objects",
        "query_domain": "complete classification of declared semantic candidates",
        "identity": ["case", "component", "query", "semantic_fact"],
        "components": list(COMPONENTS),
        "statistics": catalog_statistics(cases),
        "cases": cases,
    }


def validate_catalog(catalog: dict[str, object]) -> None:
    if catalog.get("schema_version") != 6 or catalog.get("suite") != "PYGBench":
        raise AssertionError("invalid semantic ground-truth catalog header")
    cases = catalog.get("cases")
    if not isinstance(cases, list) or not cases:
        raise AssertionError("semantic ground-truth catalog has no cases")
    case_ids: set[str] = set()
    fact_ids: set[str] = set()
    query_ids: set[str] = set()
    for case in cases:
        if not isinstance(case, dict):
            raise AssertionError("ground-truth case must be an object")
        case_id = case.get("id")
        if not isinstance(case_id, str) or not case_id or case_id in case_ids:
            raise AssertionError(f"duplicate or invalid ground-truth case: {case_id!r}")
        case_ids.add(case_id)
        limitations = case.get("limitations")
        if not isinstance(limitations, list) or any(
                not isinstance(value, str) or not value
                for value in limitations):
            raise AssertionError(f"{case_id} has invalid limitations")
        entries = case.get("entry_points")
        if not isinstance(entries, list) or not entries or len(entries) != len(set(entries)):
            raise AssertionError(f"{case_id} has invalid entry points")
        if case.get("pta_configurations") != pta_configurations():
            raise AssertionError(
                f"{case_id} has invalid per-case PTA configurations")
        dimensions = case.get("dimensions")
        required_dimensions = {
            "analysis_component", "language_feature", "sensitivity", "scope",
            "dispatch", "control", "oracle_role", "layer", "python_versions",
        }
        if not isinstance(dimensions, dict) or set(dimensions) != required_dimensions:
            raise AssertionError(f"{case_id} has incomplete benchmark dimensions")
        for name in required_dimensions - {"layer", "python_versions"}:
            values = dimensions[name]
            if not isinstance(values, list) or not values or \
                    any(not isinstance(value, str) or not value for value in values):
                raise AssertionError(f"{case_id}:{name} has invalid dimension values")
        if dimensions["layer"] not in {"micro", "interaction", "package"}:
            raise AssertionError(f"{case_id} has invalid benchmark layer")
        if dimensions["python_versions"] != {"minimum": "3.10", "maximum": "3.14"}:
            raise AssertionError(f"{case_id} has invalid Python version range")
        components = case.get("components")
        if not isinstance(components, dict) or set(components) != set(COMPONENTS):
            raise AssertionError(
                f"{case_id} must define PTA, CG, CFG, CDG, and DDG")
        targeted_components = sorted(
            name for name, component in components.items()
            if isinstance(component, dict) and component.get("applicability") == "targeted")
        if dimensions["analysis_component"] != targeted_components:
            raise AssertionError(f"{case_id} component dimension disagrees with facts")
        for component_name, component in components.items():
            if not isinstance(component, dict):
                raise AssertionError(f"{case_id}:{component_name} must be an object")
            expected = component.get("expected")
            forbidden = component.get("forbidden")
            expected_paths = component.get("expected_paths")
            forbidden_paths = component.get("forbidden_paths")
            queries = component.get("queries")
            if not isinstance(expected, list) or not isinstance(forbidden, list) or \
                    not isinstance(expected_paths, list) or \
                    not isinstance(forbidden_paths, list) or \
                    not isinstance(queries, list):
                raise AssertionError(f"{case_id}:{component_name} facts must be lists")
            applicability = component.get("applicability")
            has_facts = bool(expected or forbidden or
                             expected_paths or forbidden_paths)
            if applicability != ("targeted" if has_facts else "not_targeted"):
                raise AssertionError(f"{case_id}:{component_name} applicability is inconsistent")
            if not has_facts:
                raise AssertionError(
                    f"{case_id}:{component_name} has no scored candidate universe")
            if bool(queries) != (applicability == "targeted"):
                raise AssertionError(f"{case_id}:{component_name} query applicability is inconsistent")
            expected_relations: set[tuple[str, str, str]] = set()
            forbidden_relations: set[tuple[str, str, str]] = set()
            relation_versions: dict[
                str, dict[tuple[str, str, str], set[str]]] = {
                    "expected": {}, "forbidden": {}}
            for polarity, facts, relations in (
                    ("expected", expected, expected_relations),
                    ("forbidden", forbidden, forbidden_relations)):
                for fact in facts:
                    if not isinstance(fact, dict):
                        raise AssertionError(f"{case_id} has a non-object fact")
                    fact_id = fact.get("id")
                    if not isinstance(fact_id, str) or fact_id in fact_ids:
                        raise AssertionError(f"duplicate or invalid fact id: {fact_id!r}")
                    fact_ids.add(fact_id)
                    kind = fact.get("kind")
                    if FACT_COMPONENT.get(str(kind)) != component_name:
                        raise AssertionError(f"{fact_id} belongs to the wrong component")
                    if fact.get("relation") != FACT_RELATION[str(kind)]:
                        raise AssertionError(f"{fact_id} has the wrong semantic relation")
                    subject = fact.get("subject")
                    object_ = fact.get("object")
                    if not isinstance(subject, str) or not subject or \
                            not isinstance(object_, str) or not object_:
                        raise AssertionError(f"{fact_id} has an invalid endpoint")
                    relation = (subject, str(fact["relation"]), object_)
                    relations.add(relation)
                    minimum = str(fact.get(
                        "minimum_python", SUPPORTED_PYTHON_VERSIONS[0]))
                    maximum = str(fact.get(
                        "maximum_python", SUPPORTED_PYTHON_VERSIONS[-1]))
                    begin = SUPPORTED_PYTHON_VERSIONS.index(minimum)
                    end = SUPPORTED_PYTHON_VERSIONS.index(maximum)
                    relation_versions[polarity].setdefault(
                        relation, set()).update(
                            SUPPORTED_PYTHON_VERSIONS[begin:end + 1])
            overlap = expected_relations & forbidden_relations
            conflicting = [
                relation for relation in overlap
                if relation_versions["expected"][relation] &
                   relation_versions["forbidden"][relation]
            ]
            if conflicting:
                raise AssertionError(
                    f"{case_id}:{component_name} expects and forbids "
                    f"{sorted(conflicting)}")
            expected_path_relations: set[tuple[str, str, str]] = set()
            forbidden_path_relations: set[tuple[str, str, str]] = set()
            for polarity, paths, relations in (
                    ("expected", expected_paths, expected_path_relations),
                    ("forbidden", forbidden_paths, forbidden_path_relations)):
                for path in paths:
                    if not isinstance(path, dict):
                        raise AssertionError(f"{case_id} has a non-object path")
                    path_id = path.get("id")
                    nodes = path.get("nodes")
                    if not isinstance(path_id, str) or path_id in fact_ids:
                        raise AssertionError(
                            f"duplicate or invalid path id: {path_id!r}")
                    fact_ids.add(path_id)
                    if not isinstance(nodes, list) or len(nodes) < 2 or \
                            len(nodes) != len(set(nodes)) or \
                            any(not isinstance(node, str) or not node
                                for node in nodes):
                        raise AssertionError(f"{path_id} is not a finite simple path")
                    encoded = json.dumps(nodes, separators=(",", ":"))
                    relations.add((str(nodes[0]), PATH_RELATION, encoded))
            path_overlap = expected_path_relations & forbidden_path_relations
            if path_overlap:
                raise AssertionError(
                    f"{case_id}:{component_name} expects and forbids paths "
                    f"{sorted(path_overlap)}")
            expected_relations.update(expected_path_relations)
            forbidden_relations.update(forbidden_path_relations)
            query_expected: set[tuple[str, str, str]] = set()
            query_forbidden: set[tuple[str, str, str]] = set()
            for query in queries:
                if not isinstance(query, dict) or query.get("complete") is not True or \
                        query.get("domain") not in {
                            "declared-candidate-objects",
                            "declared-candidate-paths",
                        }:
                    raise AssertionError(f"{case_id}:{component_name} has an incomplete query")
                subject = query.get("subject")
                relation = query.get("relation")
                expected_objects = query.get("expected_objects")
                forbidden_objects = query.get("forbidden_objects")
                query_id = query.get("id")
                if not isinstance(query_id, str) or not query_id or query_id in query_ids:
                    raise AssertionError(f"duplicate or invalid query id: {query_id!r}")
                query_ids.add(query_id)
                if not isinstance(subject, str) or not subject or \
                        not isinstance(relation, str) or \
                        not isinstance(expected_objects, list) or \
                        not isinstance(forbidden_objects, list):
                    raise AssertionError(f"{case_id}:{component_name} has an invalid query")
                expected_domain = ("declared-candidate-paths"
                                   if relation == PATH_RELATION
                                   else "declared-candidate-objects")
                if query.get("domain") != expected_domain:
                    raise AssertionError(f"{query_id} has the wrong query domain")
                if any(not isinstance(value, str) or not value
                       for value in expected_objects + forbidden_objects):
                    raise AssertionError(f"{query_id} has an invalid result object")
                if len(expected_objects) != len(set(expected_objects)) or \
                        len(forbidden_objects) != len(set(forbidden_objects)):
                    raise AssertionError(f"{query_id} contains duplicate result objects")
                if set(expected_objects) & set(forbidden_objects):
                    raise AssertionError(f"{query_id} expects and forbids the same object")
                if not expected_objects and not forbidden_objects:
                    raise AssertionError(f"{query_id} has an empty query domain")
                by_version = query.get("objects_by_python_version")
                if isinstance(by_version, dict):
                    all_expected: set[str] = set()
                    all_forbidden: set[str] = set()
                    for version in SUPPORTED_PYTHON_VERSIONS:
                        candidates = by_version.get(version)
                        if not isinstance(candidates, dict):
                            raise AssertionError(
                                f"{query_id} lacks CPython {version} candidates")
                        version_expected = candidates.get("expected_objects")
                        version_forbidden = candidates.get("forbidden_objects")
                        if not isinstance(version_expected, list) or \
                                not isinstance(version_forbidden, list) or \
                                set(version_expected) & set(version_forbidden):
                            raise AssertionError(
                                f"{query_id} has invalid CPython {version} candidates")
                        all_expected.update(str(value)
                                            for value in version_expected)
                        all_forbidden.update(str(value)
                                             for value in version_forbidden)
                    expected_objects = sorted(all_expected)
                    forbidden_objects = sorted(all_forbidden)
                query_expected.update((subject, relation, str(value))
                                      for value in expected_objects)
                query_forbidden.update((subject, relation, str(value))
                                       for value in forbidden_objects)
            if query_expected != expected_relations or query_forbidden != forbidden_relations:
                raise AssertionError(f"{case_id}:{component_name} queries lose semantic facts")
    expected_statistics = catalog_statistics(cases)
    if catalog.get("statistics") != expected_statistics:
        raise AssertionError("ground-truth catalog statistics are stale")


def serialized_catalog() -> str:
    catalog = build_catalog()
    validate_catalog(catalog)
    return json.dumps(catalog, indent=2, sort_keys=True) + "\n"


def case_ground_truths(catalog: dict[str, object]) -> dict[str, dict[str, object]]:
    artifact_python_version = SUPPORTED_PYTHON_VERSIONS[0]
    artifact_version_index = SUPPORTED_PYTHON_VERSIONS.index(
        artifact_python_version)

    def applies_to_artifact_version(fact: dict[str, object]) -> bool:
        minimum = SUPPORTED_PYTHON_VERSIONS.index(str(fact.get(
            "minimum_python", SUPPORTED_PYTHON_VERSIONS[0])))
        maximum = SUPPORTED_PYTHON_VERSIONS.index(str(fact.get(
            "maximum_python", SUPPORTED_PYTHON_VERSIONS[-1])))
        return minimum <= artifact_version_index <= maximum

    records: dict[str, dict[str, object]] = {}
    cases = catalog["cases"]
    assert isinstance(cases, list)
    for case in cases:
        assert isinstance(case, dict)
        components = case["components"]
        assert isinstance(components, dict)
        pta_queries = components["pta"]["queries"]
        points_to_sets = [{
            "subject": query["subject"],
            "points_to": query["expected_objects"],
            "must_not_point_to": query["forbidden_objects"],
        } for query in pta_queries]
        graphs: dict[str, object] = {}
        for component_name in ("cg", "cfg", "cdg", "ddg"):
            component = components[component_name]
            required_edges = [{
                "source": fact["subject"],
                "target": fact["object"],
                **({"outcome": str(fact["kind"])[4:]}
                   if str(fact["kind"]).startswith("cdg-") else {}),
            } for fact in component["expected"]
                if applies_to_artifact_version(fact)]
            forbidden_edges = [{
                "source": fact["subject"],
                "target": fact["object"],
                **({"outcome": str(fact["kind"])[4:]}
                   if str(fact["kind"]).startswith("cdg-") else {}),
            } for fact in component["forbidden"]
                if applies_to_artifact_version(fact)]
            required_paths = [
                path["nodes"] for path in component["expected_paths"]
            ]
            forbidden_paths = [
                path["nodes"] for path in component["forbidden_paths"]
            ]
            if not required_edges and not forbidden_edges and \
                    not required_paths and not forbidden_paths:
                continue
            nodes = sorted({
                str(endpoint)
                for edge in required_edges + forbidden_edges
                for endpoint in (edge["source"], edge["target"])
            } | {
                str(node)
                for path in required_paths + forbidden_paths
                for node in path
            })
            graphs[component_name] = {
                "nodes": nodes,
                "required_edges": required_edges,
                "forbidden_edges": forbidden_edges,
                "required_paths": required_paths,
                "forbidden_paths": forbidden_paths,
            }
        tags = case["tags"]
        assert isinstance(tags, list)
        records[str(case["id"])] = {
            "schema_version": 1,
            "case": case["id"],
            "entry_points": case["entry_points"],
            "tags": tags,
            "limitations": case["limitations"],
            "selective_sensitivity": case["pta_configurations"],
            "points_to_sets": points_to_sets,
            "cg": graphs.get("cg", {
                "nodes": [], "required_edges": [], "forbidden_edges": [],
                "required_paths": [], "forbidden_paths": []}),
            "cfg": graphs.get("cfg", {
                "nodes": [], "required_edges": [], "forbidden_edges": [],
                "required_paths": [], "forbidden_paths": []}),
            "cdg": graphs.get("cdg", {
                "nodes": [], "required_edges": [], "forbidden_edges": [],
                "required_paths": [], "forbidden_paths": []}),
            "ddg": graphs.get("ddg", {
                "nodes": [], "required_edges": [], "forbidden_edges": [],
                "required_paths": [], "forbidden_paths": []}),
        }
    return records


def graph_dot(case_id: str, component: str,
              graph: dict[str, object]) -> str:
    lines = [
        f"digraph {json.dumps(case_id + '.' + component)} {{",
        '  graph [rankdir="LR"];',
        "  node [shape=rectangle];",
    ]
    nodes = graph["nodes"]
    required_edges = graph["required_edges"]
    forbidden_edges = graph["forbidden_edges"]
    required_paths = graph["required_paths"]
    forbidden_paths = graph["forbidden_paths"]
    assert isinstance(nodes, list)
    assert isinstance(required_edges, list)
    assert isinstance(forbidden_edges, list)
    assert isinstance(required_paths, list)
    assert isinstance(forbidden_paths, list)
    for node in nodes:
        lines.append(f"  {json.dumps(str(node))};")
    for edge in required_edges:
        assert isinstance(edge, dict)
        attributes = (f" [label={json.dumps(str(edge['outcome']))}]"
                      if "outcome" in edge else "")
        lines.append(
            f"  {json.dumps(str(edge['source']))} -> "
            f"{json.dumps(str(edge['target']))}{attributes};")
    for edge in forbidden_edges:
        assert isinstance(edge, dict)
        outcome = (f" [{edge['outcome']}]" if "outcome" in edge else "")
        lines.append(
            f"  // forbidden: {json.dumps(str(edge['source']))} -> "
            f"{json.dumps(str(edge['target']))}{outcome}")
    for path in required_paths:
        assert isinstance(path, list)
        lines.append("  // required path: " + " -> ".join(
            json.dumps(str(node)) for node in path))
    for path in forbidden_paths:
        assert isinstance(path, list)
        lines.append("  // forbidden path: " + " -> ".join(
            json.dumps(str(node)) for node in path))
    lines.append("}")
    return "\n".join(lines) + "\n"


def serialized_case_artifacts(
        records: dict[str, dict[str, object]]
        ) -> dict[str, dict[str, str]]:
    artifacts: dict[str, dict[str, str]] = {}
    for case_id, record in records.items():
        case_artifacts = {
            f"{component}.dot": graph_dot(
                case_id, component, record[component])
            for component in ("cg", "cfg", "cdg", "ddg")
        }
        case_artifacts["pta.json"] = json.dumps({
            "schema_version": 1,
            "case": case_id,
            "points_to_sets": record["points_to_sets"],
        }, indent=2, sort_keys=True) + "\n"
        artifacts[case_id] = case_artifacts
    return artifacts


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="fail if groundtruths/oracle.json is absent or stale")
    arguments = parser.parse_args()
    generated = serialized_catalog()
    catalog = json.loads(generated)
    case_records = case_ground_truths(catalog)
    generated_cases = {
        case_id: json.dumps(record, indent=2, sort_keys=True) + "\n"
        for case_id, record in case_records.items()
    }
    generated_artifacts = serialized_case_artifacts(case_records)
    if arguments.check:
        if not CATALOG_PATH.is_file():
            raise SystemExit("groundtruths/oracle.json is missing")
        if CATALOG_PATH.read_text(encoding="utf-8") != generated:
            raise SystemExit("groundtruths/oracle.json is stale; rebuild it")
        if not CASE_GROUND_TRUTHS.is_dir():
            raise SystemExit("groundtruths/cases is missing")
        actual_case_files = {path.stem for path in CASE_GROUND_TRUTHS.glob("*.json")}
        if actual_case_files != set(generated_cases):
            raise SystemExit("groundtruths/cases does not match benchmark cases")
        for case_id, expected in generated_cases.items():
            path = CASE_GROUND_TRUTHS / f"{case_id}.json"
            if path.read_text(encoding="utf-8") != expected:
                raise SystemExit(f"{path.relative_to(ROOT)} is stale")
        if not CASE_ARTIFACTS.is_dir():
            raise SystemExit("groundtruths/artifacts is missing")
        actual_artifact_cases = {
            path.name for path in CASE_ARTIFACTS.iterdir() if path.is_dir()
        }
        if actual_artifact_cases != set(generated_artifacts):
            raise SystemExit(
                "groundtruths/artifacts does not match benchmark cases")
        for case_id, artifacts in generated_artifacts.items():
            case_directory = CASE_ARTIFACTS / case_id
            actual_files = {
                path.name for path in case_directory.iterdir() if path.is_file()
            }
            if actual_files != set(artifacts):
                raise SystemExit(
                    f"{case_directory.relative_to(ROOT)} has incomplete artifacts")
            for filename, expected in artifacts.items():
                path = case_directory / filename
                if path.read_text(encoding="utf-8") != expected:
                    raise SystemExit(f"{path.relative_to(ROOT)} is stale")
        statistics = catalog["statistics"]
        assert isinstance(statistics, dict)
        print(json.dumps({"suite": catalog["suite"],
                          "ground_truth_cases": len(catalog["cases"]),
                          "statistics": statistics,
                          "status": "current"}, sort_keys=True))
        return
    CATALOG_PATH.write_text(generated, encoding="utf-8")
    CASE_GROUND_TRUTHS.mkdir(parents=True, exist_ok=True)
    for case_id, contents in generated_cases.items():
        (CASE_GROUND_TRUTHS / f"{case_id}.json").write_text(
            contents, encoding="utf-8")
    CASE_ARTIFACTS.mkdir(parents=True, exist_ok=True)
    for case_id, artifacts in generated_artifacts.items():
        case_directory = CASE_ARTIFACTS / case_id
        case_directory.mkdir(parents=True, exist_ok=True)
        for filename, contents in artifacts.items():
            (case_directory / filename).write_text(contents, encoding="utf-8")


if __name__ == "__main__":
    main()
