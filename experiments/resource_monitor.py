"""Run one analyzer while measuring time and aggregate process-tree memory."""

from __future__ import annotations

from dataclasses import asdict, dataclass
import os
from pathlib import Path
import resource
import signal
import subprocess
import time
from typing import BinaryIO, Mapping, Sequence


DEFAULT_SAMPLE_INTERVAL_SECONDS = 0.01
TERMINATION_GRACE_SECONDS = 5
KIBIBYTE_BYTES = 1_024
PROCFS_ROOT = Path("/proc")
PROCFS_STAT_NAME = "stat"


@dataclass(frozen=True)
class ResourceUsage:
    """Resources consumed by exactly one monitored analyzer invocation."""

    wall_seconds: float
    cpu_seconds: float
    peak_rss_bytes: int
    sample_interval_seconds: float
    timed_out: bool
    memory_limit_exceeded: bool

    def to_json(self) -> dict[str, object]:
        return asdict(self)


@dataclass(frozen=True)
class MonitoredResult:
    exit_code: int
    resources: ResourceUsage


def _process_table() -> tuple[dict[int, int], dict[int, int]]:
    """Return PID-to-parent and PID-to-process-group maps from procfs."""

    parents: dict[int, int] = {}
    groups: dict[int, int] = {}
    try:
        with os.scandir(PROCFS_ROOT) as entries:
            stat_paths = tuple(
                Path(entry.path) / PROCFS_STAT_NAME
                for entry in entries
                if entry.name.isdecimal()
            )
    except (FileNotFoundError, PermissionError, ProcessLookupError):
        return parents, groups

    for stat_path in stat_paths:
        try:
            pid = int(stat_path.parent.name)
            value = stat_path.read_text(encoding="utf-8")
            fields = value[value.rfind(")") + 2:].split()
            parents[pid] = int(fields[1])
            groups[pid] = int(fields[2])
        except (FileNotFoundError, PermissionError, ProcessLookupError,
                ValueError, IndexError):
            continue
    return parents, groups


def _rss_bytes(pid: int) -> int:
    try:
        lines = Path(f"/proc/{pid}/status").read_text(
            encoding="utf-8").splitlines()
        for line in lines:
            if line.startswith("VmRSS:"):
                return int(line.split()[1]) * KIBIBYTE_BYTES
    except (FileNotFoundError, PermissionError, ProcessLookupError,
            ValueError, IndexError):
        return 0
    return 0


def process_tree_rss_bytes(root_pid: int) -> int:
    """Sum RSS for the root and its live descendants/process-group peers."""

    parents, groups = _process_table()
    selected = {root_pid}
    changed = True
    while changed:
        changed = False
        for pid, parent in parents.items():
            if pid not in selected and parent in selected:
                selected.add(pid)
                changed = True
    selected.update(pid for pid, group in groups.items() if group == root_pid)
    return sum(_rss_bytes(pid) for pid in selected)


def _child_setup(memory_limit_bytes: int | None):
    def configure() -> None:
        os.setsid()
        if memory_limit_bytes is not None:
            resource.setrlimit(
                resource.RLIMIT_AS,
                (memory_limit_bytes, memory_limit_bytes),
            )

    return configure


def _terminate_group(process: subprocess.Popen[bytes]) -> None:
    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=TERMINATION_GRACE_SECONDS)
    except ProcessLookupError:
        return
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            return


def run_monitored(
    command: Sequence[str],
    *,
    cwd: Path,
    stdout: BinaryIO,
    stderr: BinaryIO,
    environment: Mapping[str, str] | None = None,
    timeout_seconds: float | None = None,
    memory_limit_bytes: int | None = None,
    sample_interval_seconds: float = DEFAULT_SAMPLE_INTERVAL_SECONDS,
) -> MonitoredResult:
    """Execute command once and collect wall, CPU, and tree RSS together."""

    if timeout_seconds is not None and timeout_seconds <= 0:
        raise ValueError("timeout must be positive")
    if memory_limit_bytes is not None and memory_limit_bytes <= 0:
        raise ValueError("memory limit must be positive")
    if sample_interval_seconds <= 0:
        raise ValueError("sample interval must be positive")

    started = time.monotonic()
    usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
    process = subprocess.Popen(
        list(command), cwd=cwd, stdout=stdout, stderr=stderr,
        env=None if environment is None else dict(environment),
        preexec_fn=_child_setup(memory_limit_bytes),
    )
    peak_rss_bytes = 0
    timed_out = False
    memory_limit_exceeded = False
    while True:
        rss_bytes = process_tree_rss_bytes(process.pid)
        peak_rss_bytes = max(peak_rss_bytes, rss_bytes)
        if process.poll() is not None:
            break
        elapsed = time.monotonic() - started
        if (memory_limit_bytes is not None and
                rss_bytes > memory_limit_bytes):
            memory_limit_exceeded = True
            _terminate_group(process)
            break
        if timeout_seconds is not None and elapsed >= timeout_seconds:
            timed_out = True
            _terminate_group(process)
            break
        time.sleep(sample_interval_seconds)
    exit_code = process.wait()
    peak_rss_bytes = max(peak_rss_bytes, process_tree_rss_bytes(process.pid))
    usage_after = resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu_seconds = (
        usage_after.ru_utime - usage_before.ru_utime +
        usage_after.ru_stime - usage_before.ru_stime
    )
    return MonitoredResult(
        exit_code=exit_code,
        resources=ResourceUsage(
            wall_seconds=time.monotonic() - started,
            cpu_seconds=cpu_seconds,
            peak_rss_bytes=peak_rss_bytes,
            sample_interval_seconds=sample_interval_seconds,
            timed_out=timed_out,
            memory_limit_exceeded=memory_limit_exceeded,
        ),
    )
