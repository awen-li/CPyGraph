#!/usr/bin/env python3
"""Regression checks for experiment process-tree resource collection."""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
if str(REPOSITORY_ROOT) not in sys.path:
    sys.path.insert(0, str(REPOSITORY_ROOT))

from experiments.resource_monitor import _process_table, run_monitored


CHILD_ALLOCATION_MIB = 16
CHILD_HOLD_SECONDS = 0.25
MIBIBYTE_BYTES = 1_024**2
TEST_MEMORY_LIMIT_BYTES = 256 * MIBIBYTE_BYTES
DISAPPEARING_PROCESS_NAME = "4242"
DISAPPEARING_PROCESS_PATH = f"/proc/{DISAPPEARING_PROCESS_NAME}"


class ProcessEntries:
    """Minimal scandir context containing a process that has just exited."""

    def __enter__(self):
        return iter((SimpleNamespace(
            name=DISAPPEARING_PROCESS_NAME,
            path=DISAPPEARING_PROCESS_PATH,
        ),))

    def __exit__(self, exception_type, exception, traceback) -> None:
        return None


def monitored(command: list[str], timeout_seconds: float = 5.0):
    with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
        return run_monitored(
            command,
            cwd=REPOSITORY_ROOT,
            stdout=stdout,
            stderr=stderr,
            timeout_seconds=timeout_seconds,
            memory_limit_bytes=TEST_MEMORY_LIMIT_BYTES,
        )


def main() -> int:
    with (
        patch("experiments.resource_monitor.os.scandir",
              return_value=ProcessEntries()),
        patch.object(Path, "read_text", side_effect=ProcessLookupError()),
    ):
        assert _process_table() == ({}, {})

    child_program = (
        "import os,time; "
        f"allocation=bytearray({CHILD_ALLOCATION_MIB}*1024*1024); "
        "page_bytes=os.sysconf('SC_PAGE_SIZE'); "
        "[(allocation.__setitem__(index,1)) for index in "
        "range(0,len(allocation),page_bytes)]; "
        f"time.sleep({CHILD_HOLD_SECONDS})"
    )
    parent_program = (
        "import subprocess,sys; "
        f"child=subprocess.Popen([sys.executable,'-c',{child_program!r}]); "
        "child.wait()"
    )
    result = monitored([sys.executable, "-c", parent_program])
    assert result.exit_code == 0
    assert not result.resources.timed_out
    assert not result.resources.memory_limit_exceeded
    assert result.resources.wall_seconds > 0
    assert result.resources.cpu_seconds > 0
    assert (result.resources.peak_rss_bytes >=
            CHILD_ALLOCATION_MIB * MIBIBYTE_BYTES)

    timeout = monitored(
        [sys.executable, "-c", "import time; time.sleep(1)"],
        timeout_seconds=0.05,
    )
    assert timeout.resources.timed_out
    assert timeout.exit_code != 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
