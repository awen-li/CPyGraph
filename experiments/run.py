#!/usr/bin/env python3
"""Run the full experiment campaign or one measured CPyGraph invocation.

With no ``--tool`` argument this is the main entry point for the complete
frozen-corpus campaign. Supplying ``--tool`` retains the focused single-package
runner used internally by the campaign and for diagnostics.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import signal
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
if str(REPOSITORY_ROOT) not in sys.path:
    sys.path.insert(0, str(REPOSITORY_ROOT))

from experiments.resource_monitor import run_monitored


DEFAULT_TIMEOUT_SECONDS = 1_800
DEFAULT_MEMORY_GIB = 32
GIBIBYTE_BYTES = 1_024**3
STATUS_SUCCESS = "SUCCESS"
STATUS_ANALYSIS_EXCEPTION = "ANALYSIS_EXCEPTION"
STATUS_TIMEOUT = "TIMEOUT"
STATUS_OUT_OF_MEMORY = "OUT_OF_MEMORY"
STATUS_INVALID_OUTPUT = "INVALID_OUTPUT"
STATUS_COMPILATION_FAILURE = "COMPILATION_FAILURE"
STATUS_UNSUPPORTED_VERSION = "UNSUPPORTED_VERSION"
STATUS_INVALID_BYTECODE = "INVALID_BYTECODE"
RUN_SCHEMA_VERSION = 2
FULL_ANALYSIS_SCHEMA_VERSION = 2


def failure_status(stderr: str) -> str:
    lowered = stderr.lower()
    if "bad_alloc" in lowered or "memoryerror" in lowered:
        return STATUS_OUT_OF_MEMORY
    if "failed to compile" in lowered or "cannot start python compiler" in lowered:
        return STATUS_COMPILATION_FAILURE
    if "unsupported cpython" in lowered or "must match cpygraph" in lowered:
        return STATUS_UNSUPPORTED_VERSION
    if "invalid bytecode" in lowered or "invalid code object" in lowered:
        return STATUS_INVALID_BYTECODE
    return STATUS_ANALYSIS_EXCEPTION


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def run_one(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", required=True, type=Path,
                        help="pygCG, pygCFG, pygDDG, or another JSON-emitting tool")
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--timeout-seconds", type=int,
                        default=DEFAULT_TIMEOUT_SECONDS)
    parser.add_argument("--memory-gib", type=int, default=DEFAULT_MEMORY_GIB)
    args = parser.parse_args(argv)

    tool = args.tool.resolve()
    package = args.input.resolve()
    if not tool.is_file():
        parser.error(f"analysis tool does not exist: {tool}")
    if not package.exists():
        parser.error(f"analysis input does not exist: {package}")
    if args.timeout_seconds <= 0 or args.memory_gib <= 0:
        parser.error("timeout and memory limit must be positive")

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    analysis_path = output / "analysis.json"
    stderr_path = output / "stderr.log"
    memory_limit_bytes = args.memory_gib * GIBIBYTE_BYTES
    command = [str(tool), str(package)]
    metadata: dict[str, object] = {
        "schema_version": RUN_SCHEMA_VERSION,
        "tool": str(tool),
        "input": str(package),
        "command": command,
        "started_at": utc_now(),
        "timeout_seconds": args.timeout_seconds,
        "memory_limit_bytes": memory_limit_bytes,
        "measurement_scope": "analyzer-process-tree",
    }
    environment = os.environ.copy()
    environment["PYTHONHASHSEED"] = "0"
    with analysis_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        monitored = run_monitored(
            command,
            cwd=output,
            stdout=stdout,
            stderr=stderr,
            environment=environment,
            timeout_seconds=args.timeout_seconds,
            memory_limit_bytes=memory_limit_bytes,
        )

    status = STATUS_SUCCESS
    if monitored.resources.timed_out:
        status = STATUS_TIMEOUT
    elif monitored.resources.memory_limit_exceeded:
        status = STATUS_OUT_OF_MEMORY
    elif (monitored.exit_code < 0 and
          -monitored.exit_code == signal.SIGKILL):
        status = STATUS_ANALYSIS_EXCEPTION
    elif monitored.exit_code != 0:
        try:
            status = failure_status(stderr_path.read_text(
                encoding="utf-8", errors="replace"))
        except OSError:
            status = STATUS_ANALYSIS_EXCEPTION
    else:
        try:
            result = json.loads(analysis_path.read_text(encoding="utf-8"))
            if not isinstance(result, dict):
                raise ValueError("analysis output is not a JSON object")
            if (tool.name == "pygDDG" and
                    result.get("analysis_schema_version") !=
                    FULL_ANALYSIS_SCHEMA_VERSION):
                raise ValueError("pygDDG output is not a full-pipeline result")
            metadata["internal_profiling"] = result.get("profiling")
            metadata["analysis_summary"] = {
                name: value for name, value in result.items()
                if name != "profiling"
            }
        except (OSError, ValueError, json.JSONDecodeError) as error:
            status = STATUS_INVALID_OUTPUT
            metadata["error"] = str(error)

    metadata.update(
        status=status,
        exit_code=monitored.exit_code,
        **monitored.resources.to_json(),
        finished_at=utc_now(),
    )
    (output / "run.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")
    return 0 if status == STATUS_SUCCESS else 1


def main(argv: list[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    if "--tool" in arguments:
        return run_one(arguments)

    from experiments.campaign import main as run_campaign
    return run_campaign(arguments)


if __name__ == "__main__":
    raise SystemExit(main())
