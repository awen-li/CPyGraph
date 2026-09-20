#!/usr/bin/env python3
"""Generate version-qualified opcode macros from target CPython runtimes.

The generator also replaces numeric opcode keys in each version table. Generic
stack counts and operands remain normal C++ values; only target-version opcode
identities are macros.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ADAPTERS_ROOT = ROOT / "cpygraph/bytecode/adapters"
VERSION_DIRECTORY = re.compile(r"python(?P<compact>[0-9]+)")


def discover_adapters() -> dict[str, tuple[str, Path]]:
    adapters: dict[str, tuple[str, Path]] = {}
    for directory in sorted(ADAPTERS_ROOT.glob("python*")):
        match = VERSION_DIRECTORY.fullmatch(directory.name)
        if match is None or not directory.is_dir():
            continue
        compact = match.group("compact")
        if len(compact) < 2:
            raise RuntimeError(
                f"{directory}: version directory must encode major and minor versions"
            )
        for required in ("adapter.cpp", "adapter.h"):
            if not (directory / required).is_file():
                raise RuntimeError(f"{directory}: missing {required}")
        version = f"{compact[0]}.{compact[1:]}"
        adapters[compact] = (version, directory)
    if not adapters:
        raise RuntimeError(f"no version adapters found under {ADAPTERS_ROOT}")
    return adapters


def compatibility_opcodes(directory: Path) -> dict[int, str]:
    config_path = directory / "opcode_overrides.json"
    if not config_path.exists():
        return {}
    config = json.loads(config_path.read_text(encoding="utf-8"))
    entries = config.get("compatibility_opcodes", {})
    result: dict[int, str] = {}
    for encoded_number, name in entries.items():
        number = int(encoded_number)
        if number < 0 or not isinstance(name, str) or not name:
            raise RuntimeError(f"invalid compatibility opcode in {config_path}")
        result[number] = name
    return result


def opcode_map(interpreter: str) -> dict[str, int]:
    output = subprocess.check_output(
        [interpreter, "-c", "import json, opcode; print(json.dumps(opcode.opmap))"],
        text=True,
    )
    return json.loads(output)


def macro_name(version: str, opname: str) -> str:
    sanitized = re.sub(r"[^A-Z0-9]+", "_", opname.upper()).strip("_")
    return f"CPYGRAPH_PY{version}_{sanitized}"


def generate(version: str, directory: Path, interpreter: str) -> None:
    opmap = opcode_map(interpreter)
    overrides = compatibility_opcodes(directory)
    conflicting_numbers = set(opmap.values()) & set(overrides)
    if conflicting_numbers:
        raise RuntimeError(
            f"{directory}: compatibility opcodes conflict with opcode.opmap: "
            f"{sorted(conflicting_numbers)}"
        )
    by_number = {number: name for name, number in opmap.items()}
    by_number.update(overrides)
    compact = directory.name.removeprefix("python")
    header = directory / "opcodes.h"
    lines = [
        "#pragma once",
        "",
        f"// Generated from CPython {version} opcode.opmap.",
        "// Regenerate with tools/generate_opcode_macros.py.",
        "",
    ]
    for name, number in sorted(opmap.items(), key=lambda item: (item[1], item[0])):
        lines.append(f"#define {macro_name(compact, name)} {number}")
    for number, name in sorted(overrides.items()):
        lines.append(f"#define {macro_name(compact, name)} {number}")
    header.write_text("\n".join(lines) + "\n", encoding="utf-8")

    source = directory / "adapter.cpp"
    text = source.read_text(encoding="utf-8")
    include = f'#include "bytecode/adapters/{directory.name}/opcodes.h"\n'
    if include not in text:
        first_line, remainder = text.split("\n", 1)
        text = f"{first_line}\n{include}{remainder}"

    def replace_table_key(match: re.Match[str]) -> str:
        number = int(match.group(1))
        try:
            name = by_number[number]
        except KeyError as error:
            raise RuntimeError(
                f"{source}: opcode {number} is absent from CPython {version}"
            ) from error
        return "{" + macro_name(compact, name) + ","

    text = re.sub(r"\{(\d+),", replace_table_key, text)
    source.write_text(text, encoding="utf-8")


def main() -> None:
    adapters = discover_adapters()
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--only",
        action="append",
        choices=adapters,
        help="generate only the selected compact version (for example, 315)",
    )
    for compact, (version, _) in adapters.items():
        parser.add_argument(
            f"--python{compact}",
            default=os.environ.get(f"CPYGRAPH_PYTHON{compact}", f"python{version}"),
            help=f"CPython {version} executable",
        )
    arguments = parser.parse_args()
    selected = arguments.only or adapters
    for compact in selected:
        version, directory = adapters[compact]
        generate(version, directory, getattr(arguments, f"python{compact}"))


if __name__ == "__main__":
    main()
