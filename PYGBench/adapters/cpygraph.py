#!/usr/bin/env python3
"""Collect CPyGraph observations and bind them to PYGBench semantic queries."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Iterable

ADAPTER_ROOT = Path(__file__).resolve().parents[1]
GROUND_TRUTHS = ADAPTER_ROOT / "groundtruths"
ROOT = ADAPTER_ROOT


MEASURED = "measured"
UNSUPPORTED = "unsupported"

SOUNDNESS_CONCRETE_ONLY = 0
SOUNDNESS_CONSERVATIVE_TOP = 1
SOUNDNESS_TYPED_UNRESOLVED = 2
COVERAGE_CONCRETE_ONLY = "concrete-only"
COVERAGE_CONSERVATIVE_TOP = "conservative-top"
COVERAGE_TYPED_UNRESOLVED = "typed-unresolved"

OPCODE_LOAD_CONST = 1
OPCODE_LOAD_LOCAL = 2
OPCODE_STORE_LOCAL = 3
OPCODE_LOAD_GLOBAL = 4
OPCODE_STORE_GLOBAL = 5
OPCODE_LOAD_ATTRIBUTE = 6
OPCODE_STORE_ATTRIBUTE = 7
OPCODE_CREATE_FUNCTION = 11
OPCODE_CALL = 13
OPCODE_GENERIC = 16
OPCODE_LOAD_ELEMENT = 21
OPCODE_RAISE = 30
OPCODE_RETURN = 31
OPCODE_BRANCH = 33
OPCODE_CONDITIONAL_BRANCH = 34
OPCODE_SUSPEND = 35
OPCODE_YIELD = 39

CFG_EXCEPTION_EDGE = 4
CFG_BRANCH_TRUE_EDGE = 1
CFG_BRANCH_FALSE_EDGE = 2
CFG_RESUME_EDGE = 5
CDG_NORMAL_OUTCOME = 0
CDG_TRUE_OUTCOME = 1
CDG_FALSE_OUTCOME = 2
CDG_EXCEPTION_OUTCOME = 3
CDG_RESUME_OUTCOME = 4
PROTOCOL_CONTEXT_EXIT = 4

ORIGIN_INSTRUCTION = 0
ORIGIN_PARAMETER = 1
ORIGIN_CLASS_INSTANCE = 2
ORIGIN_MODULE = 3
ORIGIN_CALLABLE = 4
PTA_UNKNOWN_OBJECT = (1 << 64) - 1

DDG_INPUT = 0
DDG_CONSTANT = 1
DDG_LOAD = 2
DDG_STORE = 3
DDG_OPERATION = 4
DDG_RETURN = 5
DDG_ADDRESS_EDGE = 4
DDG_CALLEE_EDGE = 5

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
SENSITIVITY_ATTRIBUTES = ["flow", "context", "path"]
ORDINAL_TOKENS = (
    ("first", 0),
    ("second", 1),
    ("third", 2),
    ("fourth", 3),
    ("fifth", 4),
    ("sixth", 5),
    ("seventh", 6),
    ("eighth", 7),
)
UNRESOLVED_CALLABLE = 0
UNRESOLVED_PYTHON_PROTOCOL = 1
PYTHON_SPECIAL_METHODS = (
    "__import__", "__enter__", "__exit__", "__getattribute__",
    "__getattr__", "__get__", "__setattr__", "__set__", "__getitem__",
    "__setitem__", "__delitem__", "__bool__", "__len__", "__add__",
    "__radd__", "__iadd__", "__and__", "__rand__", "__iand__",
    "__floordiv__", "__rfloordiv__", "__ifloordiv__", "__lshift__",
    "__rlshift__", "__ilshift__", "__matmul__", "__rmatmul__",
    "__imatmul__", "__mul__", "__rmul__", "__imul__", "__mod__",
    "__rmod__", "__imod__", "__or__", "__ror__", "__ior__", "__pow__",
    "__rpow__", "__ipow__", "__rshift__", "__rrshift__", "__irshift__",
    "__sub__", "__rsub__", "__isub__", "__truediv__", "__rtruediv__",
    "__itruediv__", "__xor__", "__rxor__", "__ixor__",
    "__pos__", "__neg__", "__invert__", "__delattr__", "__iter__",
    "__eq__", "__ne__", "__lt__", "__le__", "__gt__", "__ge__",
    "__aenter__", "__aexit__", "__aiter__", "__anext__", "__await__",
    "__call__", "__contains__", "__float__", "__int__", "__str__",
    "__format__", "__hash__", "__next__", "__reversed__",
)


def validate_case_configuration(case: dict[str, object], policy: str) -> None:
    configurations = case.get("pta_configurations")
    if not isinstance(configurations, dict):
        raise ValueError(f"{case['id']}: missing PTA configurations")
    if policy in {"insensitive", "complete"}:
        # These are analysis modes, not per-function configurations.
        return
    if policy == "selective-entry":
        entry_points = case.get("entry_points")
        if not isinstance(entry_points, list) or any(
                not isinstance(entry, str) for entry in entry_points):
            raise ValueError(f"{case['id']}: invalid entry-point configuration")
        return
    configuration = configurations.get(policy)
    if not isinstance(configuration, dict):
        raise ValueError(f"{case['id']}: missing {policy} PTA configuration")
    attributes = (SENSITIVITY_ATTRIBUTES if policy == "selective-all"
                  else [policy.removeprefix("selective-")])
    expected = {
        "level": "selective",
        "functions": [{
            "selector": "*",
            "attributes": attributes,
        }],
    }
    if configuration != expected:
        raise ValueError(
            f"{case['id']}: invalid {policy} PTA configuration")


def words(value: str) -> set[str]:
    return set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", value))


def normalized_qualname(value: str) -> str:
    return value.replace(".<locals>", "")


def query_candidates(query: dict[str, object], python_version: str) -> list[str]:
    by_version = query.get("objects_by_python_version")
    selected = (by_version.get(python_version)
                if isinstance(by_version, dict) else None)
    source = selected if isinstance(selected, dict) else query
    return [
        *(str(value) for value in source["expected_objects"]),
        *(str(value) for value in source["forbidden_objects"]),
    ]


def supported_case(case: dict[str, object]) -> bool:
    limitations = case.get("limitations", [])
    if not isinstance(limitations, list):
        raise ValueError(f"{case.get('id', '<unknown>')} has invalid limitations")
    return not (set(str(value) for value in limitations) & EXCLUDED_LIMITATIONS)


class CaseObservations:
    def __init__(self, raw: dict[str, object]) -> None:
        self.raw = raw
        soundness = raw["soundness"]
        assert isinstance(soundness, dict)
        self.soundness = {str(component): int(coverage)
                          for component, coverage in soundness.items()}
        code_values = raw["code_objects"]
        assert isinstance(code_values, list)
        self.codes = {int(code["id"]): code for code in code_values}
        cg = raw["cg"]
        assert isinstance(cg, dict)
        self.cg_edges = cg["edges"]
        assert isinstance(self.cg_edges, list)
        self.cg_sites = {int(site["site"]): site for site in cg["sites"]}
        self.cg_activations = cg["activations"]
        assert isinstance(self.cg_activations, list)
        pta = raw["pta"]
        assert isinstance(pta, dict)
        self.pta_values = pta["values"]
        self.pta_origins = pta["origins"]
        self.pta_contents = pta["contents"]
        self.pta_fields = pta["fields"]
        self.field_names = {int(value["field"]): str(value["name"])
                            for value in pta["field_names"]}
        self.origins_by_object: dict[int, list[dict[str, object]]] = {}
        for origin in self.pta_origins:
            self.origins_by_object.setdefault(int(origin["object"]), []).append(origin)
        ddg = raw["ddg"]
        assert isinstance(ddg, dict)
        self.ddg_nodes = ddg["nodes"]
        self.ddg_edges = ddg["edges"]
        self.ddg_nodes_by_id = {int(node["id"]): node for node in self.ddg_nodes}
        self.ddg_successors: dict[int, set[int]] = {}
        self.ddg_predecessors: dict[int, set[int]] = {}
        self.ddg_address_predecessors: dict[int, set[int]] = {}
        for edge in self.ddg_edges:
            if int(edge["kind"]) in {DDG_ADDRESS_EDGE, DDG_CALLEE_EDGE}:
                self.ddg_address_predecessors.setdefault(
                    int(edge["target"]), set()).add(int(edge["source"]))
                continue
            self.ddg_successors.setdefault(int(edge["source"]), set()).add(
                int(edge["target"]))
            self.ddg_predecessors.setdefault(int(edge["target"]), set()).add(
                int(edge["source"]))
        self.cfg_blocks: dict[tuple[int, int], dict[str, object]] = {}
        self.cfg_successors: dict[tuple[int, int], set[tuple[int, int]]] = {}
        self.cfg_edge_kinds: dict[
            tuple[tuple[int, int], tuple[int, int]], set[int]] = {}
        self.cfg_exception_sources: set[tuple[int, int]] = set()
        self.cfg_exception_targets: set[tuple[int, int]] = set()
        for graph in raw["cfg"]:
            code_id = int(graph["code"])
            for block in graph["blocks"]:
                key = (code_id, int(block["id"]))
                self.cfg_blocks[key] = block
            for edge in graph["edges"]:
                source = (code_id, int(edge["source"]))
                target = (code_id, int(edge["target"]))
                self.cfg_successors.setdefault(source, set()).add(target)
                self.cfg_edge_kinds.setdefault((source, target), set()).add(
                    int(edge["kind"]))
                if int(edge["kind"]) == CFG_EXCEPTION_EDGE:
                    self.cfg_exception_sources.add(source)
                    self.cfg_exception_targets.add(target)
        self.cdg_successors: dict[
            tuple[int, int], set[tuple[int, int]]] = {}
        self.cdg_edge_outcomes: dict[
            tuple[tuple[int, int], tuple[int, int]], set[int]] = {}
        for graph in raw["cdg"]:
            code_id = int(graph["code"])
            for edge in graph["edges"]:
                source = (code_id, int(edge["controller"]))
                target = (code_id, int(edge["dependent"]))
                self.cdg_successors.setdefault(source, set()).add(target)
                self.cdg_edge_outcomes.setdefault((source, target), set()).add(
                    int(edge["outcome"]))

    def classified_candidates(self, component: str, candidates: Iterable[str],
                              concrete: Iterable[str],
                              requires_top: bool | None = None
                              ) -> tuple[list[str], list[str], str]:
        domain = set(candidates)
        concrete_set = set(concrete)
        if requires_top is None:
            requires_top = (
                self.soundness.get(component, SOUNDNESS_CONCRETE_ONLY) ==
                SOUNDNESS_CONSERVATIVE_TOP)
        if requires_top:
            abstract = domain - concrete_set
            coverage = COVERAGE_CONSERVATIVE_TOP
        else:
            abstract = set()
            coverage = COVERAGE_CONCRETE_ONLY
        return sorted(concrete_set), sorted(abstract), coverage

    def code_aliases(self, code: dict[str, object]) -> set[str]:
        module = str(code["module"])
        name = str(code["name"])
        qualname = normalized_qualname(str(code["qualname"]))
        lexical_parts = [name]
        parent_id = int(code["parent"])
        while parent_id != 0:
            parent = self.codes[parent_id]
            parent_name = str(parent["name"])
            if parent_name != "<module>":
                lexical_parts.append(parent_name)
            parent_id = int(parent["parent"])
        lexical_qualname = ".".join(reversed(lexical_parts))
        aliases = {
            name,
            qualname,
            lexical_qualname,
            f"{module}.{qualname}",
            f"{module}.{lexical_qualname}",
        }
        aliases.update(alias.replace("<", "").replace(">", "")
                       for alias in tuple(aliases))
        if name == "<module>":
            aliases.update({"module", "<module>", module})
        return aliases

    def codes_named_in(self, description: str) -> set[int]:
        matches: list[tuple[int, int]] = []
        for code_id, code in self.codes.items():
            for alias in self.code_aliases(code):
                if alias in description:
                    matches.append((len(alias), code_id))
        if not matches:
            return set()
        longest = max(length for length, _ in matches)
        return {code_id for length, code_id in matches if length == longest}

    def caller_codes(self, description: str) -> set[int]:
        selector = description.split("@", 1)[0]
        tokens = words(selector)
        # Site descriptions commonly append a receiver or operation after the
        # caller (for example, "run child.work"). Prefer an explicitly named
        # entry/caller before matching the longer callee-like suffix.
        for preferred in ("run", "caller", "invoke", "dispatch", "delegate",
                          "repeat", "create", "forward"):
            if preferred not in tokens:
                continue
            matches = {code_id for code_id, code in self.codes.items()
                       if str(code["name"]) == preferred}
            if matches:
                return matches
        matches = self.codes_named_in(selector)
        if matches:
            return matches
        for preferred in ("module",):
            if preferred not in tokens:
                continue
            matches = {code_id for code_id, code in self.codes.items()
                       if str(code["name"]) == ("<module>" if preferred == "module"
                                                else preferred)}
            if matches:
                return matches
        if "closure" in tokens:
            matches = {code_id for code_id, code in self.codes.items()
                       if str(code["name"]) in {"inner", "closure"}}
            if matches:
                return matches
        if "call" in tokens and "site" in tokens:
            non_modules = {int(edge["caller"]) for edge in self.cg_edges
                           if self.codes[int(edge["caller"])]["name"] != "<module>"}
            if len(non_modules) == 1:
                return non_modules
        if tokens & {"branch", "format", "cleanup", "assignment", "deletion",
                     "read", "write", "attribute"}:
            matches = {code_id for code_id, code in self.codes.items()
                       if str(code["name"]) == "run"}
            if matches:
                return matches
        return set()

    def candidate_codes(self, description: str) -> set[int]:
        tokens = words(description)
        matches = self.codes_named_in(description)
        if matches:
            if "getter" in tokens:
                getters = {
                    code_id for code_id in matches
                    if int(self.codes[code_id]["argument_count"]) == 1
                }
                if getters:
                    return getters
            if "setter" in tokens:
                setters = {
                    code_id for code_id in matches
                    if int(self.codes[code_id]["argument_count"]) == 2
                }
                if setters:
                    return setters
            if "local" in tokens:
                nested = {
                    code_id for code_id in matches
                    if int(self.codes[code_id]["parent"]) != 0 and
                    int(self.codes[int(self.codes[code_id]["parent"])]["parent"]) != 0
                }
                if nested:
                    return nested
            return matches
        ignored = {
            "group", "directly", "local", "module", "function", "object",
            "instance", "with", "default", "callback", "when", "imported",
            "via", "alias", "attribute", "both", "comparisons", "and", "then",
            "before", "after", "at", "from", "to", "the", "a", "an",
        }
        names = tokens - ignored
        by_name = {code_id for code_id, code in self.codes.items()
                   if str(code["name"]) in names}
        return by_name

    def selected_cg_sites(self, caller_ids: set[int], subject: str) -> set[int]:
        sites = [site for site in self.cg_sites.values()
                 if int(site["code"]) in caller_ids]
        if "@" in subject:
            anchor_ids = self.codes_named_in(subject.split("@", 1)[1])
            anchored_codes = set(anchor_ids)
            for code_id, code in self.codes.items():
                parent_id = int(code["parent"])
                while parent_id != 0:
                    if parent_id in anchor_ids:
                        anchored_codes.add(code_id)
                        break
                    parent_id = int(self.codes[parent_id]["parent"])
            anchored_site_ids = {
                int(edge["site"])
                for edge in self.cg_edges
                if edge["target"] is not None and
                int(edge["target"]) in anchored_codes
            }
            anchored_contexts = {
                int(site["context"])
                for site in sites
                if int(site["site"]) in anchored_site_ids
            }
            selected = {
                int(site["site"])
                for site in sites
                if int(site["context"]) in anchored_contexts
            }
            if selected:
                return selected
            reached_contexts = set()
            frontier_sites = {
                int(site["site"])
                for site in self.cg_sites.values()
                if int(site["code"]) in anchor_ids
            }
            while frontier_sites:
                next_contexts = {
                    int(activation["context"])
                    for activation in self.cg_activations
                    if int(activation["site"]) in frontier_sites
                } - reached_contexts
                if not next_contexts:
                    break
                reached_contexts |= next_contexts
                frontier_sites = {
                    int(site["site"])
                    for site in self.cg_sites.values()
                    if int(site["context"]) in next_contexts
                }
            selected = {
                int(site["site"])
                for site in sites
                if int(site["context"]) in reached_contexts
            }
            if selected:
                return selected
            incoming_sites = {
                int(edge["site"])
                for edge in self.cg_edges
                if int(edge["caller"]) in anchor_ids and
                edge["target"] is not None and
                int(edge["target"]) in caller_ids
            }
            contexts = {
                int(activation["context"])
                for activation in self.cg_activations
                if int(activation["site"]) in incoming_sites and
                int(activation["target"]) in caller_ids
            }
            selected = {int(site["site"]) for site in sites
                        if int(site["context"]) in contexts}
            if selected:
                return selected
        if len(sites) <= 1:
            return {int(site["site"]) for site in sites}
        tokens = words(subject)
        if "write" in tokens:
            writes = [
                site for site in sites
                if int(self.codes[int(site["code"])]["instructions"]
                       [int(site["instruction"])]["opcode"]) ==
                OPCODE_STORE_ATTRIBUTE
            ]
            if writes:
                sites = writes
        elif "read" in tokens:
            reads = [
                site for site in sites
                if int(self.codes[int(site["code"])]["instructions"]
                       [int(site["instruction"])]["opcode"]) ==
                OPCODE_LOAD_ATTRIBUTE
            ]
            if reads:
                sites = reads
        attribute_names = {
            str(self.codes[int(site["code"])]["instructions"]
                [int(site["instruction"])]["symbol"])
            for site in sites
            if int(self.codes[int(site["code"])]["instructions"]
                   [int(site["instruction"])]["opcode"]) ==
            OPCODE_LOAD_ATTRIBUTE
        }
        selected_attribute_names = tokens & attribute_names
        if "attribute" in tokens and selected_attribute_names:
            sites = [
                site for site in sites
                if str(self.codes[int(site["code"])]["instructions"]
                       [int(site["instruction"])]["symbol"]) in
                selected_attribute_names
            ]
        if tokens & {"call", "site"}:
            explicit = [site for site in sites
                        if int(self.codes[int(site["code"])]["instructions"]
                               [int(site["instruction"])]["opcode"]) == OPCODE_CALL]
            if explicit:
                sites = explicit
        caller_names = {str(self.codes[code_id]["name"])
                        for code_id in caller_ids}
        target_names = (
            tokens & {str(code["name"]) for code in self.codes.values()}
        ) - caller_names
        if target_names:
            target_sites = {int(edge["site"]) for edge in self.cg_edges
                            if edge["target"] is not None and
                            str(self.codes[int(edge["target"])]["name"]) in target_names}
            filtered = [site for site in sites if int(site["site"]) in target_sites]
            if filtered:
                sites = filtered
        ordered = sorted(sites, key=lambda site: int(site["instruction"]))
        if tokens & {"base", "recursive"} and "repeat" in caller_names:
            explicit = [
                site for site in ordered
                if int(self.codes[int(site["code"])]["instructions"]
                       [int(site["instruction"])]["opcode"]) == OPCODE_CALL
            ]
            if explicit:
                return {int((explicit[0] if "base" in tokens
                             else explicit[-1])["site"])}
        if "work" not in caller_names and \
                tokens & {"true", "first", "left", "child"} and \
                tokens & {"site", "call", "work", "result", "return"}:
            return {int(ordered[0]["site"])}
        if "work" not in caller_names and \
                tokens & {"false", "second", "right", "base"} and \
                tokens & {"site", "call", "work", "result", "return"}:
            return {int(ordered[-1]["site"])}
        if "cleanup" in tokens:
            selected = set()
            for site in sites:
                code = self.codes[int(site["code"])]
                instruction = code["instructions"][int(site["instruction"])]
                if int(instruction["protocol"]) == PROTOCOL_CONTEXT_EXIT:
                    selected.add(int(site["site"]))
            selected.update(
                int(edge["site"])
                for edge in self.cg_edges
                if edge["target"] is not None and
                int(edge["caller"]) in caller_ids and
                str(self.codes[int(edge["target"])]["name"]) == "__exit__"
            )
            if selected:
                return selected
        return {int(site["site"]) for site in sites}

    def cg_candidate_present(self, caller_ids: set[int], subject: str,
                             description: str) -> bool | None:
        candidate_ids = self.candidate_codes(description)
        selected_sites = self.selected_cg_sites(caller_ids, subject)
        relevant = [edge for edge in self.cg_edges
                    if int(edge["caller"]) in caller_ids and
                    int(edge["site"]) in selected_sites]
        resolved_targets = {int(edge["target"]) for edge in relevant
                            if edge["target"] is not None}
        if candidate_ids:
            # A combined candidate such as "left.__enter__ and right.__enter__"
            # denotes a conjunction of named targets in one legacy fact.
            return candidate_ids <= resolved_targets
        if "callback" in words(description):
            return any(edge["target"] is not None for edge in relevant)
        # Concrete in-package CG predictions cannot target a code object that
        # the compiled package does not contain. This is a measured absence,
        # not a selector failure.
        return False

    def cg_has_unresolved(self, caller_ids: set[int], subject: str) -> bool:
        selected_sites = self.selected_cg_sites(caller_ids, subject)
        if not selected_sites:
            return False
        return any(
            int(edge["caller"]) in caller_ids and
            int(edge["site"]) in selected_sites and
            edge["unresolved"] is not None
            for edge in self.cg_edges)

    def cg_group_covers(self, caller_ids: set[int], subject: str,
                        description: str) -> bool:
        # A group is not an oracle target. Project only its in-package portion
        # onto known package code; the residual external set stays outside the
        # package-scoped candidate domain.
        if not self.candidate_codes(description):
            return False
        special_names = {token for token in words(description)
                         if token.startswith("__") and token.endswith("__")}
        method_ids = set()
        for name in special_names:
            if name in PYTHON_SPECIAL_METHODS:
                method_ids.add(PYTHON_SPECIAL_METHODS.index(name))
        selected_sites = self.selected_cg_sites(caller_ids, subject)
        groups = [
            edge["unresolved"] for edge in self.cg_edges
            if int(edge["caller"]) in caller_ids and
            int(edge["site"]) in selected_sites and
            edge["unresolved"] is not None
        ]
        if method_ids:
            def packed_methods(group: dict[str, object]) -> set[int]:
                encoded = int(group["methods"])
                result = set()
                while encoded:
                    method = encoded & 0xff
                    if method == 0:
                        break
                    result.add(method - 1)
                    encoded >>= 8
                return result
            return any(
                int(group["kind"]) == UNRESOLVED_PYTHON_PROTOCOL and
                packed_methods(group) & method_ids
                for group in groups)
        # DynamicCall and zero-mask protocol groups are explicit unresolved
        # facts, not aliases for every package-defined callable.  Projecting
        # either group onto concrete code objects fabricates both targets and
        # precision; concrete recall must instead be recovered by PTA/CG.
        return False

    def origin_words(self, origin: dict[str, object]) -> set[str]:
        kind = int(origin["kind"])
        code = self.codes.get(int(origin["code"]))
        result: set[str] = set()
        if code is not None:
            result |= words(str(code["name"]))
            result |= words(normalized_qualname(str(code["qualname"])))
            result |= words(str(code["module"]))
        index = int(origin["index"])
        if kind == ORIGIN_PARAMETER and code is not None:
            locals_ = code["locals"]
            if index < len(locals_):
                local_name = str(locals_[index])
                result.add(local_name)
                result.update(local_name.split("_"))
            result.add("parameter")
            if str(code["name"]) == "__init__":
                result.add("constructor")
        elif kind == ORIGIN_CLASS_INSTANCE:
            result.add("instance")
        elif kind == ORIGIN_MODULE:
            result.add("module")
        elif kind == ORIGIN_CALLABLE:
            result.add("function")
            result.add("closure")
        elif kind == ORIGIN_INSTRUCTION and code is not None:
            instructions = code["instructions"]
            if index < len(instructions):
                instruction = instructions[index]
                symbol = str(instruction["symbol"])
                if symbol:
                    result.add(symbol)
                opcode = int(instruction["opcode"])
                if opcode == OPCODE_CREATE_FUNCTION:
                    result -= words(str(code["name"]))
                    result -= words(normalized_qualname(str(code["qualname"])))
                    result.add("function")
                elif opcode == OPCODE_CALL:
                    result.add("result")
                    result.add("call")
                    if index + 1 < len(instructions):
                        stored_name = str(instructions[index + 1]["symbol"])
                        if stored_name:
                            result.add(stored_name)
                            result.update(stored_name.split("_"))
                elif opcode == OPCODE_LOAD_CONST:
                    result.add("constant")
        return result

    def object_matches(self, object_id: int, description: str) -> bool:
        candidate = words(description)
        ignored = {
            "allocated", "from", "at", "return", "result", "read", "write",
            "parameter", "value", "object", "function", "field", "through",
            "the", "a", "an", "in", "with", "via", "payload", "shared",
        }
        significant = candidate - ignored
        for origin in self.origins_by_object.get(object_id, []):
            origin_tokens = self.origin_words(origin)
            kind = int(origin["kind"])
            if "instance" in candidate and "value" not in candidate and \
                    kind != ORIGIN_CLASS_INSTANCE:
                continue
            if "module" in candidate and kind != ORIGIN_MODULE:
                continue
            if candidate & {"function", "closure"} and kind != ORIGIN_CALLABLE:
                continue
            if significant and significant & origin_tokens:
                return True
            if not significant and candidate & origin_tokens:
                return True
            # Generic parameter-value facts intentionally match any named
            # parameter origin only when no more specific identity is present.
            if not significant and "parameter" in candidate and kind == ORIGIN_PARAMETER:
                return True
        return False

    def pta_dotted_field_objects(self, description: str,
                                 named_fields: set[str]) -> set[int] | None:
        chains = re.findall(
            r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)+",
            description)
        if not chains:
            return None
        parts = chains[-1].split(".")
        if parts[-1] not in named_fields:
            return None

        receiver_name = parts[0]
        receiver_objects = {
            int(object_id)
            for value in self.pta_values
            if str(self.codes[int(value["code"])]["instructions"]
                   [int(value["instruction"])]["symbol"]) == receiver_name
            for object_id in value["objects"]
        }
        if not receiver_objects:
            receiver_fields = {
                field for field, name in self.field_names.items()
                if name == receiver_name
            }
            receiver_objects = {
                int(fact["object"]) for fact in self.pta_fields
                if int(fact["field"]) in receiver_fields
            }
        if not receiver_objects:
            return None

        for field_name in parts[1:-1]:
            field_ids = {
                field for field, name in self.field_names.items()
                if name == field_name
            }
            receiver_objects = {
                int(fact["object"]) for fact in self.pta_fields
                if int(fact["base"]) in receiver_objects and
                int(fact["field"]) in field_ids
            }
            if not receiver_objects:
                return set()

        target_fields = {
            field for field, name in self.field_names.items()
            if name == parts[-1]
        }
        facts = [
            fact for fact in self.pta_fields
            if int(fact["base"]) in receiver_objects and
            int(fact["field"]) in target_fields
        ]
        objects = {int(fact["object"]) for fact in facts}
        if PTA_UNKNOWN_OBJECT in receiver_objects:
            objects.add(PTA_UNKNOWN_OBJECT)
        return objects

    def pta_subject_objects(self, description: str) -> set[int] | None:
        tokens = words(description)
        # Module bindings and attributes share the numeric field table.  A
        # bare function name in subjects such as "run result" must therefore
        # not be mistaken for a module-field query.  Attribute subjects use a
        # dotted spelling, except for explicit getattr/setattr cases.
        named_fields = {
            field for field in self.field_names.values()
            if f".{field}" in description or
            (tokens & {"getattr", "setattr"} and field in tokens)
        }
        dotted_objects = self.pta_dotted_field_objects(
            description, named_fields)
        if dotted_objects is not None:
            return dotted_objects
        if named_fields:
            facts = [fact for fact in self.pta_fields
                     if self.field_names.get(int(fact["field"])) in named_fields]
            if facts:
                objects = {int(fact["object"]) for fact in facts}
                if any(int(fact["base"]) == PTA_UNKNOWN_OBJECT for fact in facts):
                    objects.add(PTA_UNKNOWN_OBJECT)
                return objects
        if "[" in description:
            if self.pta_contents:
                objects = {int(fact["object"]) for fact in self.pta_contents}
                if any(int(fact["base"]) == PTA_UNKNOWN_OBJECT
                       for fact in self.pta_contents):
                    objects.add(PTA_UNKNOWN_OBJECT)
                return objects
            # A missing modeled container relation is still a measured empty
            # points-to set for a benchmark query.
            if "__dict__" in tokens:
                return set()

        leading = re.match(r"[A-Za-z_][A-Za-z0-9_]*", description)
        code_ids = set()
        if leading is not None:
            leading_name = leading.group(0)
            code_ids = {
                code_id for code_id, code in self.codes.items()
                if str(code["name"]) == leading_name
            }
        if not code_ids and tokens & {"getattr", "setattr"}:
            code_ids = {
                code_id for code_id, code in self.codes.items()
                if str(code["name"]) == "run"
            }
        if not code_ids:
            code_ids = self.codes_named_in(description)
        if not code_ids:
            function_tokens = tokens & {str(code["name"]) for code in self.codes.values()}
            if function_tokens:
                code_ids = {code_id for code_id, code in self.codes.items()
                            if str(code["name"]) in function_tokens}
        if not code_ids and tokens & {"return", "result"}:
            return_counts: dict[int, int] = {}
            for value in self.pta_values:
                code_id = int(value["code"])
                code = self.codes[code_id]
                instruction = code["instructions"][int(value["instruction"])]
                if (str(code["name"]) != "<module>" and
                        int(instruction["opcode"]) == OPCODE_RETURN):
                    return_counts[code_id] = return_counts.get(code_id, 0) + 1
            multiple_return_codes = {
                code_id for code_id, count in return_counts.items() if count > 1
            }
            if len(multiple_return_codes) == 1:
                code_ids = multiple_return_codes
        candidate_values: list[dict[str, object]] = []
        for value in self.pta_values:
            code_id = int(value["code"])
            if code_ids and code_id not in code_ids:
                continue
            code = self.codes[code_id]
            instruction = code["instructions"][int(value["instruction"])]
            symbol = str(instruction[
                "secondary_symbol" if int(value.get("output_index", 0)) != 0
                else "symbol"])
            opcode = int(instruction["opcode"])
            if symbol and symbol in tokens:
                candidate_values.append(value)
                continue
            if tokens & {"return", "result"} and opcode == OPCODE_RETURN:
                candidate_values.append(value)
                continue
            if "getattr" in tokens and opcode == OPCODE_CALL:
                candidate_values.append(value)
                continue
        if not candidate_values and code_ids and ("return" in tokens or "result" in tokens):
            for value in self.pta_values:
                if int(value["code"]) not in code_ids:
                    continue
                code = self.codes[int(value["code"])]
                instruction = code["instructions"][int(value["instruction"])]
                if int(instruction["opcode"]) in {OPCODE_RETURN, OPCODE_CALL}:
                    candidate_values.append(value)
        if not candidate_values:
            return set()
        if "getattr" in tokens:
            calls = [
                value for value in candidate_values
                if int(self.codes[int(value["code"])]["instructions"]
                       [int(value["instruction"])]["opcode"]) == OPCODE_CALL
            ]
            if calls:
                candidate_values = [max(
                    calls, key=lambda value: int(value["instruction"]))]
        # Preserve branch-site distinctions when a query explicitly names an
        # ordered true/false return in a small micro case.
        if "true" in tokens or "false" in tokens:
            returns = [value for value in candidate_values
                       if int(self.codes[int(value["code"])]["instructions"]
                              [int(value["instruction"])]["opcode"]) == OPCODE_RETURN]
            if returns:
                returns.sort(key=lambda value: int(value["instruction"]))
                candidate_values = [returns[0] if "true" in tokens else returns[-1]]
        return {int(object_id) for value in candidate_values
                for object_id in value["objects"]}

    def pta_candidate_present(self, objects: set[int], description: str) -> bool:
        return any(object_id != PTA_UNKNOWN_OBJECT and
                   self.object_matches(object_id, description)
                   for object_id in objects)

    def ddg_node_candidates(self, description: str) -> set[int]:
        tokens = {token.lower() for token in words(description)}
        if {"module", "object"} <= tokens:
            # Module identity is an address-space owner, not the data stored in
            # that module's globals.
            return set()
        code_ids = self.caller_codes(description)
        indexed_element = re.search(r"\[(\d+)\]", description)
        if indexed_element is not None:
            result_label = f"value operation result {indexed_element.group(1)}"
            named_tokens = tokens - {"element", "item", "value"}
            indexed_results: set[int] = set()
            for node in self.ddg_nodes:
                node_id = int(node["id"])
                if str(node["label"]) != result_label or (
                        code_ids and int(node["code"]) not in code_ids):
                    continue
                pending = list(self.ddg_predecessors.get(node_id, set()))
                visited = set(pending)
                matched = not named_tokens
                while pending and not matched:
                    current = pending.pop()
                    matched = bool(named_tokens & {
                        token.lower() for token in words(str(
                            self.ddg_nodes_by_id[current]["label"]))
                    })
                    for predecessor in self.ddg_predecessors.get(
                            current, set()):
                        if predecessor not in visited:
                            visited.add(predecessor)
                            pending.append(predecessor)
                if matched:
                    indexed_results.add(node_id)
            if indexed_results:
                return indexed_results
        requested_kinds: set[int] = set()
        if tokens & {"parameter", "input"}:
            requested_kinds.add(DDG_INPUT)
        if "constant" in tokens:
            requested_kinds.add(DDG_CONSTANT)
        if tokens & {"load", "read"}:
            requested_kinds.add(DDG_LOAD)
        if tokens & {"assignment", "store", "stored", "update", "initialization",
                     "definition"}:
            requested_kinds.add(DDG_STORE)
        if tokens & {"operation", "allocation", "constructor", "call", "function",
                     "element", "elements", "iteration", "yielded", "cause"}:
            requested_kinds.add(DDG_OPERATION)
        if tokens & {"return", "returned"}:
            requested_kinds.add(DDG_RETURN)
            if tokens & {"field", "payload"} and any(
                    int(node["kind"]) == DDG_LOAD and
                    tokens & {
                        token.lower()
                        for token in words(str(node["label"]))
                    }
                    for node in self.ddg_nodes):
                requested_kinds.add(DDG_LOAD)
        ignored = {
            "parameter", "input", "constant", "load", "read", "assignment",
            "store", "stored", "update", "initialization", "definition",
            "operation", "allocation", "constructor", "call", "function",
            "object", "element", "elements", "iteration", "yielded", "return",
            "returned", "value", "field", "payload", "later", "next", "each",
            "data", "directly", "cause", "and", "the", "a", "an", "at", "in",
            "subscription",
        }
        significant = tokens - ignored
        # A code-object name scopes the search; it is not itself evidence that
        # every node in that function denotes the requested value. For
        # example, "run left" must select nodes for ``left`` inside ``run``,
        # not all nodes whose owning code object is named ``run``.
        for code_id in code_ids:
            for alias in self.code_aliases(self.codes[code_id]):
                significant.difference_update(
                    token.lower() for token in words(alias))
        if tokens & {"return", "returned"} and significant:
            requested_kinds.add(DDG_OPERATION)
        if "condition" in tokens:
            requested_kinds.add(DDG_OPERATION)
        binding_tokens = tokens - {
            "a", "an", "and", "at", "call", "constant", "data",
            "definition", "directly", "field", "function", "in", "input",
            "later", "load", "operation", "parameter", "read", "result",
            "return", "returned", "store", "stored", "the", "update",
        }
        for code_id in code_ids:
            for alias in self.code_aliases(self.codes[code_id]):
                binding_tokens.difference_update(
                    token.lower() for token in words(alias))
        if code_ids and not requested_kinds and binding_tokens:
            boundary_sources = {
                predecessor
                for node in self.ddg_nodes
                if int(node["kind"]) == DDG_INPUT and
                int(node["code"]) not in code_ids and
                binding_tokens & {
                    token.lower()
                    for token in words(str(node["label"]))
                }
                for predecessor in self.ddg_predecessors.get(
                    int(node["id"]), set())
                if int(self.ddg_nodes_by_id[predecessor]["code"]) in code_ids
            }
            if boundary_sources:
                return boundary_sources
        semantic_identifiers = tokens & {
            "data", "item", "items", "object", "objects", "payload",
            "value", "values",
        }
        for token in semantic_identifiers:
            if any(
                    (not code_ids or int(node["code"]) in code_ids) and
                    (not requested_kinds or
                     int(node["kind"]) in requested_kinds) and
                    token in {
                        word.lower() for word in words(str(node["label"]))
                    }
                    for node in self.ddg_nodes):
                significant.add(token)
        if "key" in tokens and requested_kinds & {DDG_LOAD, DDG_STORE}:
            significant.add("element")
        exact_result: set[int] = set()
        contextual_result: set[int] = set()
        for node in self.ddg_nodes:
            code_id = int(node["code"])
            if code_ids and code_id not in code_ids:
                continue
            kind = int(node["kind"])
            if requested_kinds and kind not in requested_kinds:
                continue
            label_tokens = {
                token.lower() for token in words(str(node["label"]))}
            code_tokens = ({
                token.lower() for token in words(normalized_qualname(
                    str(self.codes[code_id]["qualname"])))
            } if code_ids else set())
            predecessor_tokens = {
                token.lower()
                for predecessor in (
                    self.ddg_predecessors.get(int(node["id"]), set()) |
                    self.ddg_address_predecessors.get(
                        int(node["id"]), set()))
                for token in words(str(
                    self.ddg_nodes_by_id[predecessor]["label"]))
            }
            direct_evidence = label_tokens | code_tokens
            all_evidence = direct_evidence | predecessor_tokens
            if not significant or significant <= direct_evidence:
                exact_result.add(int(node["id"]))
            elif significant <= all_evidence:
                contextual_result.add(int(node["id"]))
        result = exact_result or contextual_result
        if tokens & {"function", "object"} == {"function", "object"}:
            object_names = tokens - {
                "function", "object", "the", "a", "an",
            }
            callees = {
                predecessor
                for node in self.ddg_nodes
                if str(node["label"]).startswith("call")
                for predecessor in self.ddg_address_predecessors.get(
                    int(node["id"]), set())
                if not object_names or object_names & {
                    token.lower() for token in words(str(
                        self.ddg_nodes_by_id[predecessor]["label"]))
                }
            }
            if callees:
                return callees
        if tokens & {"true", "false"} and requested_kinds:
            ordered = sorted(
                (node_id for node_id in result),
                key=lambda node_id: (
                    int(self.ddg_nodes_by_id[node_id]["code"]),
                    int(self.ddg_nodes_by_id[node_id]["offset"])))
            if ordered:
                return {ordered[0] if "true" in tokens else ordered[-1]}
        if result or code_ids or significant:
            return result
        return {int(node["id"]) for node in self.ddg_nodes
                if not requested_kinds or int(node["kind"]) in requested_kinds}

    def ddg_reaches(self, sources: set[int], targets: set[int]) -> bool:
        pending = list(sources)
        visited = set(sources)
        while pending:
            current = pending.pop()
            for successor in self.ddg_successors.get(current, set()):
                if successor in targets:
                    return True
                if successor not in visited:
                    visited.add(successor)
                    pending.append(successor)
        return bool(sources & targets)

    def ddg_candidate_present(self, subject: str, object_: str) -> bool:
        sources = self.ddg_node_candidates(subject)
        targets = self.ddg_node_candidates(object_)
        if "directly" in {
                token.lower() for token in words(f"{subject} {object_}")}:
            return bool(targets & {
                successor for source in sources
                for successor in self.ddg_successors.get(source, set())
            })
        return self.ddg_reaches(sources, targets)

    def ddg_candidate_observation(self, subject: str,
                                  object_: str) -> tuple[bool, bool]:
        if self.ddg_candidate_present(subject, object_):
            return True, True
        original_tokens = words(f"{subject} {object_}")
        tokens = {token.lower() for token in original_tokens}
        resolved_absence = (
            "unrelated" in tokens or
            "directly" in tokens or
            "allocation" in tokens or
            "keys" in tokens or
            "normal" in tokens or
            {"function", "object"} <= tokens or
            {"self", "definition"} <= tokens or
            {"module", "object"} <= tokens or
            {"container", "object"} <= tokens or
            ({"named", "positional"} <= tokens and "keyword" not in tokens)
        )
        if resolved_absence:
            return False, True
        unresolved_features = {
            "cause", "class", "condition", "dict", "element", "elements",
            "exception", "false", "handler", "inner", "items",
            "keyword", "keywords", "mapping", "nonlocal",
            "outer", "positional", "set", "true", "tuple", "yielded",
        }
        capitalized_qualifier = any(
            token[:1].isupper() and "." in f"{subject} {object_}"
            for token in original_tokens)
        if tokens & unresolved_features or capitalized_qualifier:
            return False, False
        return False, True

    def block_instructions(self, key: tuple[int, int]) -> list[dict[str, object]]:
        code_id, _ = key
        code = self.codes[code_id]
        return [code["instructions"][int(index)]
                for index in self.cfg_blocks[key]["instructions"]]

    def cfg_block_for_instruction(
            self, code_id: int, instruction_index: int
            ) -> tuple[int, int] | None:
        for key, block in self.cfg_blocks.items():
            if key[0] == code_id and instruction_index in {
                    int(index) for index in block["instructions"]}:
                return key
        return None

    def cfg_resolved_call_blocks(
            self, description: str) -> set[tuple[int, int]]:
        """Map a semantic method description through resolved CG sites.

        CPython releases lower context cleanup differently.  The resolved
        package callee remains stable, so use the shared call-site identity
        instead of bytecode offsets or release-specific cleanup opcodes.
        """

        tokens = {token.lower() for token in words(description)}
        owner = next((value for value in ("left", "right")
                      if value in tokens), None)
        if "exit" not in tokens:
            return set()
        result: set[tuple[int, int]] = set()
        for edge in self.cg_edges:
            target = edge.get("target")
            if target is None:
                continue
            code = self.codes[int(target)]
            if str(code["name"]) != "__exit__":
                continue
            target_tokens: set[str] = set()
            owner_code: dict[str, object] | None = code
            while owner_code is not None:
                target_tokens.update(
                    token.lower()
                    for token in words(
                        f"{owner_code['name']} {owner_code['qualname']}"))
                parent = owner_code.get("parent")
                owner_code = (self.codes.get(int(parent))
                              if parent is not None else None)
            if owner is not None and owner not in target_tokens:
                continue
            site = self.cg_sites[int(edge["site"])]
            site_instruction = self.codes[int(site["code"])]["instructions"][
                int(site["instruction"])]
            # Newer CPython releases preload __exit__ before __enter__.  That
            # lookup is a resolved protocol site, but it is not an invocation.
            if int(site_instruction["opcode"]) == OPCODE_LOAD_ATTRIBUTE:
                continue
            block = self.cfg_block_for_instruction(
                int(site["code"]), int(site["instruction"]))
            if block is not None:
                result.add(block)
        return result

    def cfg_first_condition_after(
            self, starts: set[tuple[int, int]]) -> set[tuple[int, int]]:
        """Find the nearest non-exception handler test after each marker."""

        result: set[tuple[int, int]] = set()
        for start in starts:
            pending = [(start, 0)]
            visited = {start}
            best_distance: int | None = None
            while pending:
                current, distance = pending.pop(0)
                if best_distance is not None and distance > best_distance:
                    continue
                if any(int(instruction["opcode"]) == OPCODE_CONDITIONAL_BRANCH
                       for instruction in self.block_instructions(current)):
                    best_distance = distance
                    result.add(current)
                    continue
                for successor in self.cfg_successors.get(current, set()):
                    if successor[0] != start[0] or successor in visited:
                        continue
                    if CFG_EXCEPTION_EDGE in self.cfg_edge_kinds.get(
                            (current, successor), set()):
                        continue
                    visited.add(successor)
                    pending.append((successor, distance + 1))
        return result

    def cfg_cleanup_blocks(
            self, candidates: set[tuple[int, int]]) -> set[tuple[int, int]]:
        """Return blocks that materialize synchronous cleanup operations."""

        direct = {key for key in candidates if any(
            int(instruction["protocol"]) == PROTOCOL_CONTEXT_EXIT or
            (int(instruction["opcode"]) == OPCODE_LOAD_ATTRIBUTE and
             str(instruction["symbol"]) == "close")
            for instruction in self.block_instructions(key))}
        return direct

    def cfg_block_candidates(self, description: str) -> set[tuple[int, int]]:
        tokens = {token.lower() for token in words(description)}
        code_ids = self.codes_named_in(description)
        candidates = {key for key in self.cfg_blocks
                      if not code_ids or key[0] in code_ids}
        if not code_ids and "module" not in tokens:
            non_module = {key for key in candidates
                          if str(self.codes[key[0]]["name"]) != "<module>"}
            if non_module:
                candidates = non_module
        recognized = False
        if "entry" in tokens:
            if "dispatch" in tokens and "exception" in tokens:
                selected = candidates & self.cfg_exception_targets
                if selected:
                    return selected
            return {key for key in candidates if key[1] == 1}
        if "handler" in tokens or "except" in tokens:
            recognized = True
            all_handler_markers = {key for key in candidates if any(
                str(instruction["symbol"]).lower().endswith("error") or
                str(instruction["symbol"]).lower() == "exception"
                for instruction in self.block_instructions(key))}
            all_handler_checks = all_handler_markers
            all_handler_bodies = {
                target for source in all_handler_checks
                for target in self.cfg_successors.get(source, set())
                if CFG_BRANCH_TRUE_EDGE in
                self.cfg_edge_kinds.get((source, target), set())
            }
            if tokens & {"inner", "outer", "matching"} and all_handler_bodies:
                ordered_bodies = sorted(
                    all_handler_bodies,
                    key=lambda key: (key[0], int(self.cfg_blocks[key]["start_offset"])))
                if "outer" in tokens:
                    return {ordered_bodies[-1]}
                return {ordered_bodies[0]}
            typed_markers = {key for key in candidates if any(
                str(instruction["symbol"]).lower() in tokens
                and (str(instruction["symbol"]).lower().endswith("error") or
                     str(instruction["symbol"]).lower() == "exception")
                for instruction in self.block_instructions(key))}
            if tokens & {"check", "selection"} and typed_markers:
                typed_checks = {key for key in candidates if any(
                    int(instruction["opcode"]) == OPCODE_CONDITIONAL_BRANCH
                    for instruction in self.block_instructions(key)) and any(
                        marker == key or self.cfg_reaches({marker}, {key})
                        for marker in typed_markers)} or typed_markers
                return typed_checks
            typed_checks = self.cfg_first_condition_after(typed_markers)
            typed_bodies = {
                target for source in typed_checks
                for target in self.cfg_successors.get(source, set())
                if CFG_BRANCH_TRUE_EDGE in
                self.cfg_edge_kinds.get((source, target), set())
            }
            if typed_bodies and "return" not in tokens:
                return typed_bodies
            selected = candidates & self.cfg_exception_targets
            if selected and "return" not in tokens:
                return selected
        if tokens & {"protected", "try"}:
            recognized = True
            selected = candidates & self.cfg_exception_sources
            if selected:
                first_sources: dict[int, tuple[int, int]] = {}
                for key in selected:
                    current = first_sources.get(key[0])
                    if current is None or int(self.cfg_blocks[key]["start_offset"]) < \
                            int(self.cfg_blocks[current]["start_offset"]):
                        first_sources[key[0]] = key
                selected = set(first_sources.values())
                candidates = selected
        if "try" in tokens and "exit" in tokens and \
                tokens & {"normal", "exceptional"}:
            selected: set[tuple[int, int]] = set()
            edge_kind = (CFG_EXCEPTION_EDGE if "exceptional" in tokens
                         else None)
            for source in candidates:
                for target in self.cfg_successors.get(source, set()):
                    kinds = self.cfg_edge_kinds.get((source, target), set())
                    if ((edge_kind is not None and edge_kind in kinds) or
                            (edge_kind is None and
                             CFG_EXCEPTION_EDGE not in kinds)):
                        selected.add(target)
            return selected

        resolved_exits = self.cfg_resolved_call_blocks(description)
        if resolved_exits:
            return resolved_exits
        context_exits = {key for key in candidates if any(
            int(instruction["protocol"]) == PROTOCOL_CONTEXT_EXIT
            for instruction in self.block_instructions(key))}
        ordered_context_exits = sorted(
            context_exits,
            key=lambda key: (key[0], int(self.cfg_blocks[key]["start_offset"])))
        if tokens & {"left", "right"} and "exit" in tokens and ordered_context_exits:
            recognized = True
            if "right" in tokens:
                return {ordered_context_exits[0]}
            return {ordered_context_exits[-1]}

        requested_opcodes: set[int] = set()
        if tokens & {"return", "exit"}:
            recognized = True
            requested_opcodes.add(OPCODE_RETURN)
        if tokens & {"raise", "reraise", "exception"}:
            recognized = True
            requested_opcodes.add(OPCODE_RAISE)
        if ((tokens & {"condition", "guard", "match"} and
             not tokens & {"return", "exit"}) or
                ("left" in tokens and tokens & {"true", "false"})):
            recognized = True
            requested_opcodes.add(OPCODE_CONDITIONAL_BRANCH)
        if tokens & {"continue", "branch"}:
            recognized = True
            requested_opcodes.add(OPCODE_BRANCH)
        if tokens & {"call", "action"}:
            recognized = True
            requested_opcodes.add(OPCODE_CALL)
        if tokens & {"assignment", "store", "update"}:
            recognized = True
            requested_opcodes.add(OPCODE_STORE_LOCAL)
        if "attribute" in tokens and not tokens & {"return", "exit"}:
            recognized = True
            requested_opcodes.add(OPCODE_LOAD_ATTRIBUTE)
        if "subscript" in tokens and not tokens & {"return", "exit"}:
            recognized = True
            requested_opcodes.add(OPCODE_LOAD_ELEMENT)
        if "keyerror" in tokens and tokens & {"raise", "exception"}:
            implicit_key_error = {key for key in candidates if any(
                int(instruction["opcode"]) == OPCODE_LOAD_ELEMENT
                for instruction in self.block_instructions(key))}
            if implicit_key_error:
                recognized = True
                candidates = implicit_key_error
                requested_opcodes.discard(OPCODE_RAISE)
        if requested_opcodes:
            selected = {key for key in candidates
                        if any(int(instruction["opcode"]) in requested_opcodes
                               for instruction in self.block_instructions(key))}
            if selected:
                candidates = selected
            else:
                return set()
        if "normal" in tokens and tokens & {"continuation", "success"}:
            returns = {key for key in candidates if any(
                int(instruction["opcode"]) == OPCODE_RETURN
                for instruction in self.block_instructions(key))}
            handler_reachable = {
                candidate for candidate in returns
                if any(self.cfg_reaches({handler}, {candidate})
                       for handler in self.cfg_exception_targets
                       if handler[0] == candidate[0])
            }
            normal_returns = returns - handler_reachable
            if normal_returns:
                return normal_returns
        if requested_opcodes in ({OPCODE_STORE_LOCAL}, {OPCODE_RETURN},
                                 {OPCODE_CALL}) and candidates:
            ordinal = next((index for token, index in ORDINAL_TOKENS
                            if token in tokens), None)
            if ordinal is not None:
                ordered_blocks = sorted(
                    candidates,
                    key=lambda key: (
                        key[0], int(self.cfg_blocks[key]["start_offset"])))
                return {ordered_blocks[
                    min(ordinal, len(ordered_blocks) - 1)]}
        if requested_opcodes == {OPCODE_CONDITIONAL_BRANCH} and candidates:
            ordinal = next((index for token, index in ORDINAL_TOKENS
                            if token in tokens), None)
            if ordinal is not None or tokens & {"true", "false"}:
                ordered_conditions = sorted(
                    candidates,
                    key=lambda key: (
                        key[0], int(self.cfg_blocks[key]["start_offset"])))
                condition = ordered_conditions[
                    min(ordinal or 0, len(ordered_conditions) - 1)]
                if not tokens & {"true", "false"}:
                    return {condition}
                edge_kind = (CFG_BRANCH_FALSE_EDGE if "false" in tokens
                             else CFG_BRANCH_TRUE_EDGE)
                outcomes = {
                    target for target in self.cfg_successors.get(condition, set())
                    if edge_kind in self.cfg_edge_kinds.get(
                        (condition, target), set())
                }
                if outcomes:
                    candidates = outcomes
        if requested_opcodes == {OPCODE_RETURN} and tokens & {"normal", "handled"}:
            handler_reachable = {
                candidate for candidate in candidates
                if any(self.cfg_reaches({handler}, {candidate})
                       for handler in self.cfg_exception_targets
                       if handler[0] == candidate[0])
            }
            selected = (candidates - handler_reachable
                        if "normal" in tokens else candidates & handler_reachable)
            if selected:
                candidates = selected
        if tokens & {"cleanup", "finally"}:
            recognized = True
            selected = self.cfg_cleanup_blocks(candidates)
            if selected:
                candidates = selected
        if "context" in tokens and "cleanup" in tokens:
            recognized = True
            selected = {key for key in candidates if any(
                int(instruction["protocol"]) == PROTOCOL_CONTEXT_EXIT
                for instruction in self.block_instructions(key))}
            if selected:
                candidates = selected
        generic_blocks = {key for key in candidates if any(
            int(instruction["opcode"]) == OPCODE_GENERIC
            for instruction in self.block_instructions(key))}
        suspend_blocks = {key for key in candidates if any(
            int(instruction["opcode"]) in {OPCODE_SUSPEND, OPCODE_YIELD}
            for instruction in self.block_instructions(key))}
        if tokens & {"await", "yield", "next"}:
            recognized = True
            candidates = suspend_blocks or generic_blocks
        if "suspend" in tokens:
            recognized = True
            candidates = suspend_blocks
        if "resume" in tokens:
            recognized = True
            if "exceptional" in tokens:
                candidates = {
                    successor for key in suspend_blocks
                    for successor in self.cfg_successors.get(key, set())
                    if CFG_EXCEPTION_EDGE in
                    self.cfg_edge_kinds.get((key, successor), set())
                }
            else:
                candidates = {
                    successor for key in suspend_blocks
                    for successor in self.cfg_successors.get(key, set())
                    if CFG_RESUME_EDGE in
                    self.cfg_edge_kinds.get((key, successor), set())
                }
        if tokens & {"right", "action"} and "evaluate" in tokens:
            recognized = True
            candidates = {key for key in candidates if any(
                int(instruction["opcode"]) == OPCODE_CALL
                for instruction in self.block_instructions(key))}
        back_edges = {(source, target)
                      for source, successors in self.cfg_successors.items()
                      for target in successors
                      if source[0] == target[0] and
                      int(self.cfg_blocks[target]["start_offset"]) <=
                      int(self.cfg_blocks[source]["start_offset"])}
        if "continue" in tokens:
            recognized = True
            selected = {source for source, _ in back_edges} & candidates
            if selected:
                candidates = selected
        if "break" in tokens:
            recognized = True
            back_sources = {source for source, _ in back_edges}
            selected: set[tuple[int, int]] = set()
            for source in candidates:
                successors = {
                    target for target in self.cfg_successors.get(source, set())
                    if self.cfg_edge_kinds.get((source, target), set()) &
                    {CFG_BRANCH_TRUE_EDGE, CFG_BRANCH_FALSE_EDGE}
                }
                if len(successors) < 2:
                    continue
                edge_kinds = {
                    kind for target in successors
                    for kind in self.cfg_edge_kinds.get((source, target), set())
                }
                if not edge_kinds & {CFG_BRANCH_TRUE_EDGE,
                                     CFG_BRANCH_FALSE_EDGE}:
                    continue
                looping = {target for target in successors
                           if any(self.cfg_reaches({target}, {back_source})
                                  for back_source in back_sources
                                  if back_source[0] == target[0])}
                if looping and looping != successors:
                    selected.update(successors - looping)
            if selected:
                candidates = selected
        if "header" in tokens:
            recognized = True
            candidates &= {target for _, target in back_edges}
        if "body" in tokens and not (tokens & {"try", "with"}):
            recognized = True
            candidates &= {source for source, _ in back_edges}
        if "latch" in tokens:
            recognized = True
            candidates &= {source for source, _ in back_edges}
        if tokens & {"else", "arm"}:
            recognized = True
            candidates = {key for key in candidates if any(
                int(instruction["opcode"]) == OPCODE_RETURN
                for instruction in self.block_instructions(key))}
        if tokens & {"continuation", "success", "fallthrough", "after"}:
            recognized = True
            non_exception_targets = {target
                for source, successors in self.cfg_successors.items()
                if source in (self.cfg_exception_sources or candidates)
                for target in successors
                if target not in self.cfg_exception_targets}
            if non_exception_targets:
                candidates = non_exception_targets
        ordered_return_role = (
            requested_opcodes == {OPCODE_RETURN} or
            requested_opcodes == {OPCODE_STORE_LOCAL} or
            bool(tokens & {"arm", "else"}))
        if tokens & {"true", "false", "normal", "handled", "default", "sequence",
                     "zero", "else"} and ordered_return_role:
            ordered = sorted(candidates,
                             key=lambda key: (key[0], int(self.cfg_blocks[key]["start_offset"])))
            if ordered:
                if tokens & {"true", "handled", "zero"}:
                    return {ordered[0]}
                if "sequence" in tokens:
                    return {ordered[len(ordered) // 2]}
                if tokens & {"false", "normal", "default", "else"}:
                    return {ordered[-1]}
        return candidates if recognized else set()

    def cfg_reaches(self, sources: set[tuple[int, int]],
                    targets: set[tuple[int, int]]) -> bool:
        pending = list(sources)
        visited = set(sources)
        while pending:
            current = pending.pop()
            for successor in self.cfg_successors.get(current, set()):
                if successor in targets:
                    return True
                if successor not in visited:
                    visited.add(successor)
                    pending.append(successor)
        return bool(sources & targets)

    def cfg_candidate_present(self, subject: str, object_: str) -> bool:
        sources = self.cfg_block_candidates(subject)
        targets = self.cfg_block_candidates(object_)
        if ("bypassing" in object_ or "without" in object_ or
                "only" in object_ or "directly" in object_):
            # A bypass query denotes reachability to a function exit while
            # avoiding cleanup/continuation blocks. The generic CFG artifact
            # has no synthetic bypass edge, so only a direct source-to-return
            # edge can satisfy it.
            return any(
                target in self.cfg_successors.get(source, set()) and
                target not in self.cfg_cleanup_blocks({target})
                for source in sources for target in targets)
        return self.cfg_reaches(sources, targets)

    def cfg_candidate_observation(self, subject: str,
                                  object_: str) -> tuple[bool, bool]:
        """Return (present, resolved) for one semantic CFG candidate.

        An empty semantic selector is unresolved because the current compact
        CFG has no node that can represent the endpoint. A mapped source and
        target make absence meaningful, so an absent edge must not be widened
        back to whole-domain top.
        """
        sources = self.cfg_block_candidates(subject)
        targets = self.cfg_block_candidates(object_)
        if not sources or not targets:
            return False, False
        source_codes = {code for code, _ in sources}
        target_codes = {code for code, _ in targets}
        if source_codes.isdisjoint(target_codes):
            # PackageControlFlowGraphResult currently contains per-code CFGs;
            # cross-code call/exception transfers require an interprocedural
            # CFG edge kind and therefore remain explicitly unresolved.
            return False, False
        present = self.cfg_candidate_present(subject, object_)
        if present:
            return True, True
        semantic_tokens = {
            token.lower() for token in words(f"{subject} {object_}")}
        typed_exception = any(
            token.endswith("error") or token == "exception"
            for token in semantic_tokens)
        object_tokens = {token.lower() for token in words(object_)}
        subject_tokens = {token.lower() for token in words(subject)}
        if ("handler" in object_tokens and typed_exception and
                "handler" not in subject_tokens) or (
                "cleanup" in semantic_tokens) or (
                "caller" in semantic_tokens and
                semantic_tokens & {"exceptional", "continuation"}):
            return False, False
        return False, True

    def cdg_candidate_observation(
            self, subject: str, object_: str,
            relation: str = "may-control") -> tuple[bool, bool]:
        outcome_by_token = {
            "true": CDG_TRUE_OUTCOME,
            "false": CDG_FALSE_OUTCOME,
            "normal": CDG_NORMAL_OUTCOME,
            "exception": CDG_EXCEPTION_OUTCOME,
            "exceptional": CDG_EXCEPTION_OUTCOME,
            "resume": CDG_RESUME_OUTCOME,
        }
        relation_outcome = relation.removeprefix("may-control-")
        requested_outcomes = (
            {outcome_by_token[relation_outcome]}
            if relation_outcome != relation and
            relation_outcome in outcome_by_token else set())
        controller_description = re.sub(
            r"\b(?:true|false|normal|exceptional?|resume)\s+outcome\b",
            "", subject, flags=re.IGNORECASE)
        controllers = self.cfg_block_candidates(controller_description)
        dependents = self.cfg_block_candidates(object_)
        if not controllers or not dependents:
            return False, False
        same_code = {
            (controller, dependent)
            for controller in controllers for dependent in dependents
            if controller[0] == dependent[0]
        }
        if not same_code:
            return False, False
        for controller, dependent in same_code:
            outcomes = self.cdg_edge_outcomes.get(
                (controller, dependent), set())
            if outcomes and (not requested_outcomes or
                             outcomes & requested_outcomes):
                return True, True
        return False, True

    def graph_edge_observation(self, component: str, subject: str,
                               object_: str) -> tuple[bool, bool]:
        if component == "cfg":
            return self.cfg_candidate_observation(subject, object_)
        if component == "cdg":
            return self.cdg_candidate_observation(subject, object_)
        if component == "ddg":
            return self.ddg_candidate_observation(subject, object_)
        if component == "cg":
            callers = self.caller_codes(subject)
            if not callers:
                return False, False
            present = self.cg_candidate_present(callers, subject, object_)
            if present:
                return True, True
            unresolved = self.cg_has_unresolved(callers, subject)
            covered = self.cg_group_covers(callers, subject, object_)
            return False, not (unresolved and covered)
        return False, False

    def graph_path_observation(self, component: str,
                               encoded_path: str) -> tuple[bool, bool]:
        try:
            nodes = json.loads(encoded_path)
        except json.JSONDecodeError:
            return False, False
        if not isinstance(nodes, list) or len(nodes) < 2 or any(
                not isinstance(node, str) or not node for node in nodes):
            return False, False
        unresolved = False
        for subject, object_ in zip(nodes, nodes[1:]):
            present, resolved = self.graph_edge_observation(
                component, subject, object_)
            if not present and resolved:
                return False, True
            if not resolved:
                unresolved = True
        return (not unresolved), (not unresolved)


def run_case(executable: Path, case: dict[str, object],
             policy: str, artifact_root: Path | None = None
             ) -> dict[str, object]:
    source = ROOT / str(case["source"])
    entry_points = case["entry_points"]
    assert isinstance(entry_points, list)
    artifact_arguments: list[str] = []
    case_artifacts: Path | None = None
    if artifact_root is not None:
        case_artifacts = artifact_root / str(case["id"])
        case_artifacts.mkdir(parents=True, exist_ok=True)
        artifact_arguments = ["--dot-directory", str(case_artifacts)]
        if policy.startswith("selective-"):
            if policy == "selective-entry":
                configuration = {
                    "level": "selective",
                    "functions": [{
                        "selector": entry,
                        "attributes": SENSITIVITY_ATTRIBUTES,
                    } for entry in entry_points if entry != "<module>"],
                }
            else:
                configuration = case["pta_configurations"][policy]
            (case_artifacts / "sensitivity.json").write_text(
                json.dumps(configuration, indent=2, sort_keys=True) + "\n",
                encoding="utf-8")
    completed = subprocess.run(
        [str(executable), "--dump-observations", "--sensitivity", policy,
         *artifact_arguments,
         str(source),
         *(str(entry) for entry in entry_points)],
        check=False, capture_output=True, text=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"{case['id']}: {completed.stderr.strip() or 'analysis failed'}")
    result = json.loads(completed.stdout)
    if case_artifacts is not None:
        (case_artifacts / "analysis.json").write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        (case_artifacts / "pta.json").write_text(
            json.dumps(result["pta"], indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
    return result


def query_observations(case: dict[str, object],
                       raw: dict[str, object]) -> Iterable[dict[str, object]]:
    view = CaseObservations(raw)
    python_version = str(raw.get("python_version", "unknown"))
    components = case["components"]
    assert isinstance(components, dict)
    for component_name, component in components.items():
        assert isinstance(component, dict)
        queries = component["queries"]
        assert isinstance(queries, list)
        for query in queries:
            assert isinstance(query, dict)
            base = {"id": query["id"], "actual_objects": [],
                    "abstract_objects": [],
                    "coverage": COVERAGE_CONCRETE_ONLY}
            if component_name not in {"cg", "pta", "cfg", "cdg", "ddg"}:
                yield {**base, "status": UNSUPPORTED,
                       "detail": f"{component_name} semantic selector is not implemented"}
                continue
            if query["relation"] == "may-follow-path":
                candidates = query_candidates(query, python_version)
                actual: list[str] = []
                unresolved: list[str] = []
                for candidate in candidates:
                    present, resolved = view.graph_path_observation(
                        component_name, candidate)
                    if present:
                        actual.append(candidate)
                    elif not resolved:
                        unresolved.append(candidate)
                yield {
                    **base,
                    "status": MEASURED,
                    "actual_objects": sorted(set(actual)),
                    "abstract_objects": sorted(set(unresolved)),
                    "coverage": (COVERAGE_TYPED_UNRESOLVED if unresolved
                                 else COVERAGE_CONCRETE_ONLY),
                }
                continue
            if component_name == "cfg":
                candidates = query_candidates(query, python_version)
                actual: list[str] = []
                unresolved: list[str] = []
                for candidate in candidates:
                    present, resolved = view.cfg_candidate_observation(
                        str(query["subject"]), candidate)
                    if present:
                        actual.append(candidate)
                    elif not resolved:
                        unresolved.append(candidate)
                concrete = sorted(set(actual))
                abstract = sorted(set(unresolved))
                coverage = (COVERAGE_TYPED_UNRESOLVED if abstract
                            else COVERAGE_CONCRETE_ONLY)
                yield {**base, "status": MEASURED,
                       "actual_objects": concrete,
                       "abstract_objects": abstract,
                       "coverage": coverage}
                continue

            if component_name == "cdg":
                candidates = query_candidates(query, python_version)
                actual: list[str] = []
                unresolved: list[str] = []
                for candidate in candidates:
                    present, resolved = view.cdg_candidate_observation(
                        str(query["subject"]), candidate,
                        str(query["relation"]))
                    if present:
                        actual.append(candidate)
                    elif not resolved:
                        unresolved.append(candidate)
                yield {
                    **base,
                    "status": MEASURED,
                    "actual_objects": sorted(set(actual)),
                    "abstract_objects": sorted(set(unresolved)),
                    "coverage": (COVERAGE_TYPED_UNRESOLVED if unresolved
                                 else COVERAGE_CONCRETE_ONLY),
                }
                continue

            if component_name == "ddg":
                candidates = query_candidates(query, python_version)
                actual: list[str] = []
                unresolved: list[str] = []
                for candidate in candidates:
                    present, resolved = view.ddg_candidate_observation(
                        str(query["subject"]), candidate)
                    if present:
                        actual.append(candidate)
                    elif not resolved:
                        unresolved.append(candidate)
                concrete = sorted(set(actual))
                abstract = sorted(set(unresolved))
                coverage = (COVERAGE_TYPED_UNRESOLVED if abstract
                            else COVERAGE_CONCRETE_ONLY)
                yield {**base, "status": MEASURED,
                       "actual_objects": concrete,
                       "abstract_objects": abstract,
                       "coverage": coverage}
                continue

            if component_name == "pta":
                objects = view.pta_subject_objects(str(query["subject"]))
                if objects is None:
                    yield {**base, "status": UNSUPPORTED,
                           "detail": f"cannot resolve PTA subject {query['subject']!r}"}
                    continue
                candidates = query_candidates(query, python_version)
                actual = [candidate for candidate in candidates
                          if view.pta_candidate_present(objects, candidate)]
                concrete, abstract, coverage = view.classified_candidates(
                    component_name, candidates, actual,
                    PTA_UNKNOWN_OBJECT in objects)
                yield {**base, "status": MEASURED,
                       "actual_objects": concrete,
                       "abstract_objects": abstract,
                       "coverage": coverage}
                continue

            callers = view.caller_codes(str(query["subject"]))
            if not callers:
                yield {**base, "status": UNSUPPORTED,
                       "detail": f"cannot resolve CG subject {query['subject']!r}"}
                continue
            candidates = query_candidates(query, python_version)
            actual: list[str] = []
            unresolved: list[str] = []
            for candidate in candidates:
                present = view.cg_candidate_present(
                    callers, str(query["subject"]), candidate)
                if present is None:
                    unresolved.append(candidate)
                elif present:
                    actual.append(candidate)
            if unresolved:
                yield {**base, "status": UNSUPPORTED,
                       "detail": "cannot resolve CG candidates: " + ", ".join(unresolved)}
                continue
            subject = str(query["subject"])
            has_unresolved = view.cg_has_unresolved(callers, subject)
            abstract = [
                candidate for candidate in candidates
                if candidate not in actual and
                view.cg_group_covers(callers, subject, candidate)
            ]
            # An unactivated body or an unavailable context remains an
            # explicit coverage diagnostic. Top is not materialized as every
            # concrete package target in the benchmark candidate domain.
            no_site_top = not view.selected_cg_sites(callers, subject)
            context_top = "@" in subject and not actual and not has_unresolved
            concrete = sorted(set(actual))
            abstract = sorted(set(abstract))
            if no_site_top or context_top or has_unresolved:
                coverage = COVERAGE_TYPED_UNRESOLVED
            else:
                coverage = COVERAGE_CONCRETE_ONLY
            yield {**base, "status": MEASURED,
                   "actual_objects": concrete,
                   "abstract_objects": abstract,
                   "coverage": coverage}


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--ground-truth", type=Path,
                        default=GROUND_TRUTHS / "oracle.json")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--artifacts-dir", type=Path)
    parser.add_argument("--case")
    parser.add_argument("--policy", choices=ANALYSIS_POLICIES,
                        default="insensitive")
    parser.add_argument("--quiet", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    executable = arguments.executable.resolve()
    catalog = json.loads(arguments.ground_truth.read_text(encoding="utf-8"))
    cases = [case for case in catalog["cases"]
             if (arguments.case is None or case["id"] == arguments.case)
             and (arguments.case is not None or supported_case(case))]
    if not cases:
        raise SystemExit("no matching ground-truth cases")
    observations: list[dict[str, object]] = []
    python_version = "unknown"
    loaded_code_objects = 0
    configured_function_instances = 0
    for index, case in enumerate(cases, 1):
        try:
            validate_case_configuration(case, arguments.policy)
            raw = run_case(executable, case, arguments.policy,
                           arguments.artifacts_dir)
            if raw.get("analysis_policy") != arguments.policy:
                raise RuntimeError(
                    f"{case['id']}: analyzer reported policy "
                    f"{raw.get('analysis_policy')!r}, expected {arguments.policy!r}")
            python_version = str(raw["python_version"])
            code_objects = raw.get("code_objects")
            if isinstance(code_objects, list):
                loaded_code_objects += len(code_objects)
            configured_function_instances += int(
                raw.get("sensitive_functions", 0))
            observations.extend(query_observations(case, raw))
        except Exception as error:  # preserve a result for every affected query
            components = case["components"]
            for component in components.values():
                for query in component["queries"]:
                    observations.append({
                        "id": query["id"], "status": "analysis-error",
                        "actual_objects": [], "abstract_objects": [],
                        "coverage": COVERAGE_CONCRETE_ONLY,
                        "detail": str(error),
                    })
        if not arguments.quiet:
            print(f"[{index}/{len(cases)}] {case['id']}", file=sys.stderr)
    document = {
        "schema_version": 2,
        "suite": "PYGBench",
        "analyzer": "CPyGraph",
        "python_version": python_version,
        "analysis_policy": arguments.policy,
        "sensitivity_selection": {
            "configured_function_instances": configured_function_instances,
            "loaded_code_objects": loaded_code_objects,
            "fraction": (configured_function_instances / loaded_code_objects
                         if loaded_code_objects else 0.0),
        },
        "queries": observations,
    }
    arguments.output.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
