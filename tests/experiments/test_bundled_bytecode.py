from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
if str(REPOSITORY_ROOT) not in sys.path:
    sys.path.insert(0, str(REPOSITORY_ROOT))

from experiments.run_bundled_bytecode import (
    CLASS_INVALID_HEADER,
    CLASS_SUPPORTED,
    CLASS_UNSUPPORTED_CPYTHON,
    CLASS_UNSUPPORTED_PYPY,
    classify_artifact,
    component_completion,
    package_identity,
    tagged_version,
)
from experiments.audit_bundled_bytecode import (
    SOURCE_ABSENT,
    SOURCE_HEADER_MATCHED,
    module_identity,
    source_candidate,
    source_evidence,
)


PYC_HEADER_BYTES = 16


def bytecode_file(path: Path, magic: bytes) -> Path:
    path.write_bytes(magic + bytes(PYC_HEADER_BYTES - len(magic)))
    return path


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="cpygraph-rq5-test-") as root:
        temporary = Path(root)
        magic = importlib.util.MAGIC_NUMBER
        artifact = bytecode_file(
            temporary / "module.cpython-399.pyc", magic)
        record = classify_artifact(artifact, {magic.hex(): "3.10"})
        assert record["classification"] == CLASS_SUPPORTED
        assert record["python_version"] == "3.10"
        assert record["tag_matches_magic"] is False

        unknown_magic = bytes((1, 2, 3, 4))
        cpython = bytecode_file(
            temporary / "module.cpython-39.pyc", unknown_magic)
        pypy = bytecode_file(
            temporary / "module.pypy311.pyc", unknown_magic)
        invalid = temporary / "invalid.pyc"
        invalid.write_bytes(bytes((1, 2, 3)))
        assert classify_artifact(cpython, {})[
            "classification"] == CLASS_UNSUPPORTED_CPYTHON
        assert classify_artifact(pypy, {})[
            "classification"] == CLASS_UNSUPPORTED_PYPY
        assert classify_artifact(invalid, {})[
            "classification"] == CLASS_INVALID_HEADER

    path = Path(
        "cpython-3.10/example/1.0/source/pkg/__pycache__/m.cpython-314.pyc")
    assert tagged_version(path) == "3.14"
    assert package_identity(path) == "cpython-3.10/example/1.0"

    complete = {
        "analysis_summary": {
            "code_objects": 1,
            "pta": {}, "cg": {}, "cfg": {}, "cdg": {}, "ddg": {},
        }
    }
    failed = {"analysis_summary": None}
    result = component_completion((complete, failed))
    assert result["bytecode"] == {"complete": 1, "total": 2, "rate": 0.5}
    assert result["ddg"] == {"complete": 1, "total": 2, "rate": 0.5}

    with tempfile.TemporaryDirectory(prefix="cpygraph-rq5-source-test-") as root:
        temporary = Path(root)
        package = temporary / "pkg"
        cache = package / "__pycache__"
        cache.mkdir(parents=True)
        source = package / "module.py"
        source.write_text("value = 1\n", encoding="utf-8")
        stat = source.stat()
        artifact = cache / "module.cpython-310.pyc"
        header = (importlib.util.MAGIC_NUMBER + bytes(4) +
                  (int(stat.st_mtime) & 0xFFFFFFFF).to_bytes(4, "little") +
                  (stat.st_size & 0xFFFFFFFF).to_bytes(4, "little"))
        artifact.write_bytes(header)
        assert source_candidate(artifact) == source
        assert source_evidence(artifact)["source_status"] == \
            SOURCE_HEADER_MATCHED
        source.unlink()
        assert source_evidence(artifact)["source_status"] == SOURCE_ABSENT

    relative = (
        "cpython-3.10/example/1.0/source/pkg/__pycache__/"
        "module.cpython-310.pyc")
    assert module_identity(relative) == "pkg.module"
    assert module_identity(relative.replace(
        ".pyc", "-pytest-9.1.1.pyc")) == "pkg.module"
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
