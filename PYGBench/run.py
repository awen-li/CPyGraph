#!/usr/bin/env python3
"""Single public entry point for PYGBench validation and evaluation."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys


GROUND_TRUTHS = Path(__file__).resolve().parent / "groundtruths"
COMMANDS = {
    "cases": ("cases.py", ()),
    "compare": ("compare.py", ()),
    "evaluate": ("evaluate.py", ()),
    "evaluate-tool": ("evaluate_adapter.py", ()),
    "ground-truth": ("build.py", ("--check",)),
    "validate": ("validate.py", ()),
    "list": ("cases.py", ("--list",)),
}


def usage() -> int:
    commands = "|".join(sorted(COMMANDS))
    print(f"usage: {Path(sys.argv[0]).name} {commands} [arguments]", file=sys.stderr)
    return 2


def main() -> int:
    if len(sys.argv) < 2 or sys.argv[1] not in COMMANDS:
        return usage()
    script, fixed_arguments = COMMANDS[sys.argv[1]]
    completed = subprocess.run([
        sys.executable,
        str(GROUND_TRUTHS / script),
        *fixed_arguments,
        *sys.argv[2:],
    ], check=False)
    return completed.returncode


if __name__ == "__main__":
    sys.exit(main())
