#!/usr/bin/env python3
"""Materialize or intentionally replace CPyGraph's frozen PyPI corpus."""

from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timedelta, timezone
from html.parser import HTMLParser
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import random
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
from typing import Any, Callable
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlparse
from urllib.request import Request, urlopen
import zipfile

try:
    from packaging.utils import canonicalize_name
    from packaging.specifiers import InvalidSpecifier, SpecifierSet
    from packaging.version import InvalidVersion, Version
except ImportError as error:
    raise SystemExit("packaging is required: python -m pip install packaging") from error


PYTHON_VERSIONS = ("3.10", "3.11", "3.12", "3.13", "3.14")
PYPI_SIMPLE = "https://pypi.org/simple/"
PYPI_JSON = "https://pypi.org/pypi/{name}/json"
REPOSITORY_HOSTS = {"github.com", "gitlab.com", "bitbucket.org", "codeberg.org"}
REPOSITORY_KEYS = ("source", "repository", "source code", "code", "github")
USER_AGENT = "CPyGraph-benchmark-selector/1.0"
MANIFEST_SCHEMA_VERSION = 5
PROGRESS_SCHEMA_VERSION = 2
CHECKPOINT_INTERVAL = 5_000


class ProjectNameParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.names: list[str] = []
        self._inside_link = False

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        self._inside_link = tag == "a"

    def handle_endtag(self, tag: str) -> None:
        if tag == "a":
            self._inside_link = False

    def handle_data(self, data: str) -> None:
        if self._inside_link and data.strip():
            self.names.append(data.strip())


def request_bytes(url: str, *, accept: str | None = None,
                  token: str | None = None, payload: bytes | None = None,
                  attempts: int = 3) -> bytes:
    headers = {"User-Agent": USER_AGENT}
    if accept:
        headers["Accept"] = accept
    if token and urlparse(url).hostname == "api.github.com":
        headers["Authorization"] = f"Bearer {token}"
    for attempt in range(attempts):
        try:
            request = Request(url, headers=headers, data=payload)
            with urlopen(request, timeout=30) as response:
                return response.read()
        except (HTTPError, URLError, TimeoutError):
            if attempt + 1 == attempts:
                raise
            time.sleep(2 ** attempt)
    raise RuntimeError("unreachable")


def project_names() -> list[str]:
    content = request_bytes(
        PYPI_SIMPLE, accept="application/vnd.pypi.simple.v1+json"
    )
    try:
        document = json.loads(content)
        names = [item["name"] for item in document["projects"]]
    except (json.JSONDecodeError, KeyError, TypeError):
        parser = ProjectNameParser()
        parser.feed(content.decode("utf-8", errors="replace"))
        names = parser.names
    # PEP 503 normalization prevents aliases from affecting sampling probability.
    unique = {re.sub(r"[-_.]+", "-", name).lower(): name for name in names}
    return list(unique.values())


def parse_time(value: str) -> datetime:
    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    return parsed if parsed.tzinfo else parsed.replace(tzinfo=timezone.utc)


def load_population(path: Path) -> dict[str, Any]:
    """Load and validate a frozen latest-stable-sdist population export."""
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema_version") != 1:
        raise ValueError("unsupported PyPI population schema")
    start = parse_time(document["window_start"])
    end = parse_time(document["window_end"])
    if start >= end:
        raise ValueError("invalid PyPI population window")
    projects = document.get("projects")
    if not isinstance(projects, list):
        raise ValueError("PyPI population has no project records")
    required = {
        "package": str, "version": str, "release_uploaded": str,
        "sdist_filename": str, "sdist_url": str, "sdist_sha256": str,
        "project_urls": dict,
    }
    names: set[str] = set()
    for record in projects:
        if not isinstance(record, dict) or any(
                not isinstance(record.get(field), kind)
                for field, kind in required.items()):
            raise ValueError("invalid project record in PyPI population")
        normalized = canonicalize_name(record["package"])
        if normalized in names:
            raise ValueError(f"duplicate project in PyPI population: {normalized}")
        names.add(normalized)
        version = Version(record["version"])
        if version.is_prerelease or version.is_devrelease:
            raise ValueError(f"unstable release in PyPI population: {normalized}")
        uploaded = parse_time(record["release_uploaded"])
        if uploaded < start or uploaded > end:
            raise ValueError(f"release outside PyPI population window: {normalized}")
        if not re.fullmatch(r"[0-9a-f]{64}", record["sdist_sha256"]):
            raise ValueError(f"invalid sdist digest in PyPI population: {normalized}")
        parsed_url = urlparse(record["sdist_url"])
        if (parsed_url.scheme != "https"
                or parsed_url.hostname != "files.pythonhosted.org"
                or not parsed_url.path.startswith("/packages/")):
            raise ValueError(f"invalid sdist URL in PyPI population: {normalized}")
    return document


def github_repositories_in_population(records: list[dict[str, Any]]) -> set[str]:
    """Return supported GitHub repositories needed by CPython-compatible records."""
    repositories = set()
    for record in records:
        if not compatible_versions(record.get("requires_python")):
            continue
        repository = repository_url({
            "project_urls": record.get("project_urls"),
            "home_page": record.get("home_page"),
        })
        if repository and urlparse(repository).hostname == "github.com":
            repositories.add(repository)
    return repositories


def load_github_activity(path: Path, population_sha256: str,
                         start: datetime, end: datetime) -> dict[str, str]:
    """Validate a frozen GitHub PushEvent snapshot for this population/window."""
    document = json.loads(path.read_text(encoding="utf-8"))
    if (document.get("schema_version") != 1
            or document.get("source") != "githubarchive.day"
            or document.get("event_type") != "PushEvent"):
        raise ValueError("unsupported GitHub activity snapshot")
    if document.get("population_sha256") != population_sha256:
        raise ValueError("GitHub activity snapshot targets a different population")
    if (parse_time(document["window_start"]) != start
            or parse_time(document["window_end"]) != end):
        raise ValueError("GitHub activity snapshot has a different time window")
    repositories = document.get("repositories")
    if not isinstance(repositories, dict):
        raise ValueError("GitHub activity snapshot has no repository mapping")
    for url, updated in repositories.items():
        if (not isinstance(url, str) or not isinstance(updated, str)
                or not url.startswith("https://github.com/")):
            raise ValueError("invalid GitHub activity record")
        timestamp = parse_time(updated)
        if timestamp < start or timestamp > end:
            raise ValueError(f"GitHub activity outside snapshot window: {url}")
    return repositories


def frozen_github_repository_metadata(
        url: str, activity: dict[str, str] | None
        ) -> tuple[bool, dict[str, Any] | None]:
    """Resolve a GitHub URL from a frozen activity set without filesystem caches."""
    if activity is None or urlparse(url).hostname != "github.com":
        return False, None
    updated = activity.get(url)
    if updated is None:
        return True, None
    return True, {"url": url, "host": "github.com", "updated_at": updated}


def repository_url(info: dict[str, Any]) -> str | None:
    candidates: list[tuple[str, str]] = []
    for key, value in (info.get("project_urls") or {}).items():
        if isinstance(value, str):
            candidates.append((str(key).lower(), value))
    if isinstance(info.get("home_page"), str):
        candidates.append(("home", info["home_page"]))
    candidates.sort(key=lambda item: 0 if any(key in item[0] for key in REPOSITORY_KEYS) else 1)
    for _, value in candidates:
        parsed = urlparse(value)
        host = (parsed.hostname or "").lower()
        if host.startswith("www."):
            host = host[4:]
        if host not in REPOSITORY_HOSTS:
            continue
        pieces = [piece for piece in parsed.path.split("/") if piece]
        if "-" in pieces:
            pieces = pieces[:pieces.index("-")]
        if len(pieces) < 2:
            continue
        # GitLab supports nested namespaces; the other supported forges use
        # the conventional owner/repository shape.
        repository_pieces = pieces if host == "gitlab.com" else pieces[:2]
        repository_pieces[-1] = repository_pieces[-1].removesuffix(".git")
        return f"https://{host}/{'/'.join(repository_pieces)}"
    return None


def repository_metadata(url: str, github_token: str | None) -> dict[str, Any] | None:
    """Return normalized metadata for a public repository on a known forge."""
    parsed = urlparse(url)
    pieces = [piece for piece in parsed.path.split("/") if piece]
    host = parsed.hostname
    if len(pieces) < 2 or host not in REPOSITORY_HOSTS:
        return None
    try:
        if host == "github.com":
            check_url = f"https://api.github.com/repos/{quote(pieces[0])}/{quote(pieces[1])}"
            data = json.loads(request_bytes(check_url, token=github_token))
            if data.get("private") is not False or data.get("disabled") is True:
                return None
            updated = data.get("pushed_at") or data.get("updated_at")
        elif host == "gitlab.com":
            project = quote("/".join(pieces), safe="")
            data = json.loads(request_bytes(f"https://gitlab.com/api/v4/projects/{project}"))
            if data.get("visibility") != "public" or data.get("archived") is True:
                return None
            updated = data.get("last_activity_at")
        elif host == "bitbucket.org":
            data = json.loads(request_bytes(
                f"https://api.bitbucket.org/2.0/repositories/{quote(pieces[0])}/{quote(pieces[1])}"
            ))
            if data.get("is_private") is not False:
                return None
            updated = data.get("updated_on")
        else:
            data = json.loads(request_bytes(
                f"https://codeberg.org/api/v1/repos/{quote(pieces[0])}/{quote(pieces[1])}"
            ))
            if data.get("private") is not False or data.get("archived") is True:
                return None
            updated = data.get("updated_at")
        if not isinstance(updated, str):
            return None
        return {"url": url, "host": host, "updated_at": updated}
    except HTTPError as error:
        if host == "github.com" and error.code in (403, 429):
            raise RuntimeError("GitHub API rate limit reached; set GITHUB_TOKEN") from error
        return None
    except (URLError, TimeoutError, json.JSONDecodeError, KeyError, TypeError):
        return None


def repository_is_public(url: str, github_token: str | None) -> bool:
    """Compatibility wrapper used by callers that only need visibility."""
    return repository_metadata(url, github_token) is not None


def _repository_cache_target(cache: Path, url: str) -> Path:
    key = hashlib.sha256(url.encode("utf-8")).hexdigest()
    return cache / "repositories" / f"{key}.json"


def _read_repository_cache(cache: Path, url: str
                           ) -> tuple[bool, dict[str, Any] | None]:
    target = _repository_cache_target(cache, url)
    if not target.exists():
        return False, None
    try:
        state = json.loads(target.read_text(encoding="utf-8"))
        if state.get("url") != url:
            raise ValueError("repository cache URL mismatch")
        if state.get("available") is False:
            return True, None
        if isinstance(state.get("updated_at"), str):
            return True, state
    except (OSError, ValueError, json.JSONDecodeError, AttributeError):
        pass
    target.unlink(missing_ok=True)
    return False, None


def _write_repository_cache(cache: Path, url: str,
                            state: dict[str, Any] | None) -> None:
    value = state if state is not None else {"url": url, "available": False}
    write_json_atomic(_repository_cache_target(cache, url), value)


def cached_repository_metadata(cache: Path, url: str,
                               github_token: str | None) -> dict[str, Any] | None:
    """Cache forge lookups so interrupted collections resume cheaply."""
    cached, state = _read_repository_cache(cache, url)
    if cached:
        return state
    state = repository_metadata(url, github_token)
    if state is not None:
        _write_repository_cache(cache, url, state)
    return state


def prefetch_github_repository_metadata(cache: Path, urls: set[str],
                                        github_token: str | None,
                                        batch_size: int = 100) -> int:
    """Resolve public GitHub repository activity in resumable GraphQL batches."""
    pending = [url for url in sorted(urls)
               if not _read_repository_cache(cache, url)[0]]
    if not pending:
        return 0
    if not github_token:
        raise RuntimeError(
            "GITHUB_TOKEN is required to validate the frozen population in batches"
        )
    resolved = 0
    for begin in range(0, len(pending), batch_size):
        batch = pending[begin:begin + batch_size]
        fields = []
        for index, url in enumerate(batch):
            pieces = [piece for piece in urlparse(url).path.split("/") if piece]
            owner, name = pieces[:2]
            fields.append(
                f"r{index}:repository(owner:{json.dumps(owner)},name:{json.dumps(name)})"
                "{isPrivate isArchived isDisabled pushedAt updatedAt}"
            )
        query = "query{" + " ".join(fields) + "}"
        payload = json.dumps({"query": query}).encode("utf-8")
        try:
            document = json.loads(request_bytes(
                "https://api.github.com/graphql",
                accept="application/vnd.github+json",
                token=github_token,
                payload=payload,
            ))
        except (HTTPError, URLError, TimeoutError, json.JSONDecodeError) as error:
            raise RuntimeError(f"GitHub GraphQL request failed: {error}") from error
        data = document.get("data")
        if not isinstance(data, dict):
            messages = "; ".join(
                str(item.get("message", "unknown error"))
                for item in document.get("errors", []) if isinstance(item, dict)
            )
            raise RuntimeError(f"GitHub GraphQL request failed: {messages}")
        for index, url in enumerate(batch):
            item = data.get(f"r{index}")
            state = None
            if (isinstance(item, dict)
                    and item.get("isPrivate") is False
                    and item.get("isArchived") is False
                    and item.get("isDisabled") is False):
                updated = item.get("pushedAt") or item.get("updatedAt")
                if isinstance(updated, str):
                    state = {
                        "url": url,
                        "host": "github.com",
                        "updated_at": updated,
                    }
            _write_repository_cache(cache, url, state)
            resolved += 1
        if resolved % 1000 == 0 or resolved == len(pending):
            print(f"Validated {resolved}/{len(pending)} uncached GitHub repositories")
    return resolved


def stable_releases(document: dict[str, Any]) -> list[tuple[Version, str, list[dict[str, Any]]]]:
    releases = []
    for text, files in document.get("releases", {}).items():
        try:
            version = Version(text)
        except InvalidVersion:
            continue
        if version.is_prerelease or version.is_devrelease or not files:
            continue
        timestamps = [item.get("upload_time_iso_8601") or item.get("upload_time")
                      for item in files]
        uploaded = max((timestamp for timestamp in timestamps if isinstance(timestamp, str)),
                       default=None)
        if uploaded:
            releases.append((version, uploaded, files))
    return releases


def compatible_versions(requires_python: str | None) -> list[str]:
    if not requires_python:
        return []
    try:
        specifier = SpecifierSet(requires_python)
    except InvalidSpecifier:
        return []
    # A corpus targets a CPython minor line, not specifically its .0 release.
    # Testing the patch range handles constraints such as >=3.10.1,<3.11.
    return [minor for minor in PYTHON_VERSIONS
            if any(specifier.contains(f"{minor}.{patch}", prereleases=False)
                   for patch in range(100))]


def choose_sdist(files: list[dict[str, Any]]) -> dict[str, Any] | None:
    sdists = [item for item in files if item.get("packagetype") == "sdist"
              and not item.get("yanked", False)]
    return min(sdists, key=lambda item: item.get("filename", "")) if sdists else None


def _source_lines(content: bytes) -> int:
    """Count non-blank, non-comment physical Python source lines."""
    return sum(1 for line in content.splitlines()
               if line.strip() and not line.lstrip().startswith(b"#"))


def usable_python_source(python_files: int, source_lines: int) -> bool:
    """Require actual analyzable Python text, not only empty package markers."""
    return python_files > 0 and source_lines > 0


def source_measurement(archive: Path) -> tuple[int, int, int]:
    files = source_bytes = source_lines = 0
    if tarfile.is_tarfile(archive):
        with tarfile.open(archive, "r:*") as package:
            for member in package.getmembers():
                if not member.isfile() or PurePosixPath(member.name).suffix != ".py":
                    continue
                source = package.extractfile(member)
                if source is None:
                    raise ValueError(f"cannot read Python source: {member.name}")
                with source:
                    content = source.read()
                files += 1
                source_bytes += len(content)
                source_lines += _source_lines(content)
    elif zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as package:
            for member in package.infolist():
                if member.is_dir() or PurePosixPath(member.filename).suffix != ".py":
                    continue
                content = package.read(member)
                files += 1
                source_bytes += len(content)
                source_lines += _source_lines(content)
    else:
        raise ValueError("unsupported sdist archive")
    return files, source_bytes, source_lines


SOURCE_MEASUREMENT_FIELDS = (
    "python_files", "python_source_bytes", "python_source_lines",
)


def read_cached_source_measurement(cache: Path, digest: str
                                   ) -> tuple[int, int, int] | None:
    target = cache / "measurements" / f"{digest}.json"
    if target.exists():
        try:
            state = json.loads(target.read_text(encoding="utf-8"))
            values = tuple(state[key] for key in SOURCE_MEASUREMENT_FIELDS)
            if all(isinstance(value, int) and value >= 0 for value in values):
                return values
        except (OSError, KeyError, TypeError, json.JSONDecodeError):
            target.unlink(missing_ok=True)
    return None


def cached_source_measurement(cache: Path, digest: str,
                              archive: Path) -> tuple[int, int, int]:
    values = read_cached_source_measurement(cache, digest)
    if values is not None:
        return values
    target = cache / "measurements" / f"{digest}.json"
    values = source_measurement(archive)
    write_json_atomic(target, dict(zip(SOURCE_MEASUREMENT_FIELDS, values)))
    return values


def source_measurement_for_sdist(cache: Path, sdist: dict[str, Any]
                                 ) -> tuple[int, int, int]:
    """Reuse a verified measurement; verify/download only uncached archives."""
    digest = sdist["digests"]["sha256"]
    values = read_cached_source_measurement(cache, digest)
    if values is not None:
        return values
    archive = cached_sdist(cache, sdist)
    return cached_source_measurement(cache, digest, archive)


def cached_json(cache: Path, name: str) -> dict[str, Any]:
    target = cache / "metadata" / f"{re.sub(r'[^A-Za-z0-9_.-]', '_', name)}.json"
    if target.exists():
        try:
            return json.loads(target.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            target.unlink(missing_ok=True)
    target.parent.mkdir(parents=True, exist_ok=True)
    payload = request_bytes(PYPI_JSON.format(name=quote(name)))
    document = json.loads(payload)
    with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as temporary:
        temporary.write(payload)
        temporary_path = Path(temporary.name)
    temporary_path.replace(target)
    return document


def cached_sdist(cache: Path, file: dict[str, Any]) -> Path:
    expected = file.get("digests", {}).get("sha256")
    if not expected:
        raise ValueError("sdist has no SHA-256 digest")
    target = cache / "sdists" / expected / file["filename"]
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        payload = request_bytes(file["url"])
        if hashlib.sha256(payload).hexdigest() != expected:
            raise ValueError("downloaded sdist SHA-256 mismatch")
        with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as temporary:
            temporary.write(payload)
            temporary_path = Path(temporary.name)
        temporary_path.replace(target)
    elif hashlib.sha256(target.read_bytes()).hexdigest() != expected:
        raise ValueError("cached sdist SHA-256 mismatch")
    return target


def selected_packages(manifest: dict[str, Any]) -> list[dict[str, Any]]:
    """Return unique pinned packages from all version/size portions."""
    unique: dict[str, dict[str, Any]] = {}
    try:
        for portions in manifest["benchmarks"].values():
            for records in portions.values():
                for record in records:
                    digest = record["sdist_sha256"]
                    existing = unique.get(digest)
                    locked_fields = ("package", "version", "sdist_filename", "sdist_url",
                                     "sdist_sha256")
                    if existing is not None and any(
                            existing[field] != record[field] for field in locked_fields):
                        raise ValueError(f"conflicting package records for sdist digest {digest}")
                    unique[digest] = record
    except (AttributeError, KeyError, TypeError) as error:
        raise ValueError("invalid benchmark manifest structure") from error
    return sorted(unique.values(), key=lambda item: (
        item["package"].lower(), item["version"], item["sdist_filename"]
    ))


def validate_manifest(manifest: dict[str, Any]) -> int:
    """Validate a complete frozen package list and return its cohort-entry count."""
    schema_version = manifest.get("schema_version")
    if schema_version not in (4, MANIFEST_SCHEMA_VERSION):
        raise ValueError("unsupported package-list schema")
    selection = manifest.get("selection")
    benchmarks = manifest.get("benchmarks")
    if not isinstance(selection, dict) or not isinstance(benchmarks, dict):
        raise ValueError("package list has no selection or benchmark mapping")
    portions = selection.get("size_portions_per_version")
    per_portion = selection.get("random_selections_per_portion")
    if not isinstance(portions, int) or portions <= 0 or not isinstance(per_portion, int) or per_portion <= 0:
        raise ValueError("package list has invalid selection dimensions")
    if set(benchmarks) != set(PYTHON_VERSIONS):
        raise ValueError("package list does not contain every CPython cohort")
    required = {
        "package": str, "version": str, "release_uploaded": str,
        "requires_python": str, "compatible_cpython": list,
        "repository": str, "repository_updated": str,
        "sdist_filename": str, "sdist_url": str, "sdist_sha256": str,
        "python_files": int, "python_source_bytes": int,
        "python_source_lines": int, "size_portion": int,
        "syntax_validated": bool,
        "source_directory": str,
    }
    count = 0
    global_projects: set[str] = set()
    global_repositories: set[str] = set()
    for python_version, version_portions in benchmarks.items():
        expected_labels = {f"portion-{index:02d}" for index in range(1, portions + 1)}
        if not isinstance(version_portions, dict) or set(version_portions) != expected_labels:
            raise ValueError(f"invalid portions for CPython {python_version}")
        seen: set[str] = set()
        for label, records in version_portions.items():
            if not isinstance(records, list) or len(records) != per_portion:
                raise ValueError(f"incomplete selection for CPython {python_version}/{label}")
            expected_portion = int(label.split("-")[1])
            for record in records:
                if not isinstance(record, dict) or any(
                        not isinstance(record.get(field), kind) for field, kind in required.items()):
                    raise ValueError(f"invalid package record in CPython {python_version}/{label}")
                digest = record["sdist_sha256"]
                if not re.fullmatch(r"[0-9a-f]{64}", digest):
                    raise ValueError(f"invalid sdist digest for {record['package']}")
                if Path(record["sdist_filename"]).name != record["sdist_filename"]:
                    raise ValueError(f"unsafe sdist filename: {record['sdist_filename']}")
                expected_source = source_directory(python_version, record)
                if record["source_directory"] != expected_source:
                    raise ValueError(f"invalid source directory for {record['package']}")
                if python_version not in record["compatible_cpython"]:
                    raise ValueError(f"incompatible record in CPython {python_version}")
                if record["size_portion"] != expected_portion or digest in seen:
                    raise ValueError(f"duplicate or misplaced record in CPython {python_version}/{label}")
                if record["syntax_validated"] is not True:
                    raise ValueError(f"unvalidated syntax for {record['package']}")
                parse_time(record["release_uploaded"])
                parse_time(record["repository_updated"])
                if schema_version >= 5:
                    project = canonicalize_name(record["package"])
                    repository = record["repository"].rstrip("/").lower()
                    if project in global_projects:
                        raise ValueError(f"duplicate project across CPython cohorts: {project}")
                    if repository in global_repositories:
                        raise ValueError(
                            "duplicate repository across CPython cohorts: " + repository
                        )
                    global_projects.add(project)
                    global_repositories.add(repository)
                seen.add(digest)
                count += 1
    if manifest.get("package_version_entries") != count:
        raise ValueError("package-version entry count does not match package list")
    if manifest.get("unique_sdists") != len(selected_packages(manifest)):
        raise ValueError("unique sdist count does not match package list")
    return count


def _safe_parts(name: str) -> tuple[str, ...] | None:
    path = PurePosixPath(name.replace("\\", "/"))
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        return None
    return path.parts


def source_directory(python_version: str, record: dict[str, Any]) -> str:
    normalized = re.sub(r"[-_.]+", "-", record["package"]).lower()
    return f"cpython-{python_version}/{normalized}/{record['version']}/source"


def extract_source(archive: Path, destination: Path) -> None:
    """Safely and atomically extract regular files from an sdist."""
    if destination.exists():
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".source-", dir=destination.parent))
    try:
        if tarfile.is_tarfile(archive):
            with tarfile.open(archive, "r:*") as package:
                members = [member for member in package.getmembers() if member.isfile()]
                entries = [(member.name, member, member.mode) for member in members]
                for name, member, mode in entries:
                    parts = _safe_parts(name)
                    if parts is None:
                        raise ValueError(f"unsafe path in sdist: {name}")
                    target = temporary.joinpath(*parts)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    source = package.extractfile(member)
                    if source is None:
                        raise ValueError(f"cannot read sdist member: {name}")
                    with source, target.open("wb") as output:
                        shutil.copyfileobj(source, output)
                    target.chmod(mode & 0o777)
        elif zipfile.is_zipfile(archive):
            with zipfile.ZipFile(archive) as package:
                for member in package.infolist():
                    if member.is_dir():
                        continue
                    parts = _safe_parts(member.filename)
                    if parts is None:
                        raise ValueError(f"unsafe path in sdist: {member.filename}")
                    target = temporary.joinpath(*parts)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with package.open(member) as source, target.open("wb") as output:
                        shutil.copyfileobj(source, output)
        else:
            raise ValueError(f"unsupported sdist archive: {archive}")

        children = list(temporary.iterdir())
        extraction_root = children[0] if len(children) == 1 and children[0].is_dir() else temporary
        if extraction_root == temporary:
            temporary.replace(destination)
        else:
            extraction_root.replace(destination)
            temporary.rmdir()
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def archive_paths_are_safe(archive: Path) -> tuple[bool, str]:
    """Reject selected sdists whose regular files cannot be safely extracted."""
    try:
        if tarfile.is_tarfile(archive):
            with tarfile.open(archive, "r:*") as package:
                names = [member.name for member in package.getmembers()
                         if member.isfile()]
        elif zipfile.is_zipfile(archive):
            with zipfile.ZipFile(archive) as package:
                names = [member.filename for member in package.infolist()
                         if not member.is_dir()]
        else:
            return False, "unsupported sdist archive"
    except (OSError, tarfile.TarError, zipfile.BadZipFile) as error:
        return False, f"unreadable sdist archive: {error}"
    unsafe = next((name for name in names if _safe_parts(name) is None), None)
    if unsafe is not None:
        return False, f"unsafe path in sdist: {unsafe}"
    return True, ""


def _archive_for_record(record: dict[str, Any], cache: Path) -> Path:
    file = {"filename": record["sdist_filename"], "url": record["sdist_url"],
            "digests": {"sha256": record["sdist_sha256"]}}
    return cached_sdist(cache, file)


def materialize_locked_sources(manifest: dict[str, Any], package_dir: Path,
                               cache: Path) -> int:
    """Create a source tree for every selected CPython/package observation."""
    count = validate_manifest(manifest)
    for python_version, portions in manifest["benchmarks"].items():
        for records in portions.values():
            for record in records:
                source = package_dir / record["source_directory"]
                root = source.parent
                marker = root / "artifact.json"
                if source.exists():
                    if not marker.exists():
                        raise ValueError(f"unverified existing source tree: {source}")
                    state = json.loads(marker.read_text(encoding="utf-8"))
                    if state.get("sdist_sha256") != record["sdist_sha256"]:
                        raise ValueError(f"source tree does not match package list: {source}")
                    continue
                archive = _archive_for_record(record, cache)
                write_json_atomic(marker, {
                    "package": record["package"], "version": record["version"],
                    "cpython": python_version, "sdist_filename": record["sdist_filename"],
                    "sdist_sha256": record["sdist_sha256"],
                })
                extract_source(archive, source)
    return count


def prune_unselected_sources(manifest: dict[str, Any], package_dir: Path) -> int:
    """Remove only artifact-marked source trees absent from the frozen manifest."""
    expected = {
        record["source_directory"]
        for portions in manifest["benchmarks"].values()
        for records in portions.values()
        for record in records
    }
    removed = 0
    for marker in sorted(package_dir.glob("cpython-*/*/*/artifact.json")):
        source = marker.parent / "source"
        relative = source.relative_to(package_dir).as_posix()
        if relative in expected:
            continue
        shutil.rmtree(marker.parent)
        removed += 1
        for parent in (marker.parent.parent, marker.parent.parent.parent):
            try:
                parent.rmdir()
            except OSError:
                break
    return removed


def prune_unselected_cache(manifest: dict[str, Any], cache: Path) -> int:
    """Retain only selected sdists and remove transient collection metadata."""
    selected = {record["sdist_sha256"] for record in selected_packages(manifest)}
    removed = 0
    sdist_cache = cache / "sdists"
    if sdist_cache.is_dir():
        for digest_directory in sdist_cache.iterdir():
            if digest_directory.is_dir() and digest_directory.name not in selected:
                shutil.rmtree(digest_directory)
                removed += 1
    for transient in ("metadata", "repositories", "git-repositories", "measurements"):
        path = cache / transient
        if path.is_dir():
            shutil.rmtree(path)
    return removed


def stratified_selection(records: list[dict[str, Any]], portions: int,
                         per_portion: int, seed: int, version: str,
                         validator: Callable[[dict[str, Any]], tuple[bool, str]] | None = None,
                         rejections: list[dict[str, str]] | None = None,
                         excluded_projects: set[str] | None = None,
                         excluded_repositories: set[str] | None = None,
                         diversity_exclusions: Counter[str] | None = None,
                         ) -> dict[str, list[dict[str, Any]]]:
    """Partition a size-ranked population into quantiles and sample each one."""
    ranked = sorted(records, key=lambda item: (item["python_source_lines"],
                                                item["package"].lower()))
    selected: dict[str, list[dict[str, Any]]] = {}
    population_size = len(ranked)
    for index in range(portions):
        begin = index * population_size // portions
        end = (index + 1) * population_size // portions
        population = ranked[begin:end]
        label = f"portion-{index + 1:02d}"
        # A textual seed is stable across processes and keeps each version and
        # portion independent of iteration order in other strata.
        chooser = random.Random(f"{seed}:{version}:{index + 1}")
        initial_indices = chooser.sample(
            range(len(population)), min(per_portion, len(population))
        )
        initial = set(initial_indices)
        remaining_indices = [value for value in range(len(population)) if value not in initial]
        chooser.shuffle(remaining_indices)
        ordered = [population[value] for value in initial_indices + remaining_indices]
        selected[label] = []
        for item in ordered:
            if len(selected[label]) == per_portion:
                break
            project = canonicalize_name(item["package"])
            repository = item.get("repository", "").rstrip("/").lower()
            if excluded_projects is not None and project in excluded_projects:
                if diversity_exclusions is not None:
                    diversity_exclusions["duplicate_project"] += 1
                continue
            if (excluded_repositories is not None and repository
                    and repository in excluded_repositories):
                if diversity_exclusions is not None:
                    diversity_exclusions["duplicate_repository"] += 1
                continue
            valid, reason = (True, "") if validator is None else validator(item)
            if not valid:
                if rejections is not None:
                    rejections.append({
                        "cpython": version,
                        "size_portion": str(index + 1),
                        "package": item["package"],
                        "version": item["version"],
                        "reason": reason,
                    })
                continue
            selected_item = dict(item, size_portion=index + 1)
            if "version" in item:
                selected_item["source_directory"] = source_directory(version, item)
            if validator is not None:
                selected_item["syntax_validated"] = True
            selected[label].append(selected_item)
            if excluded_projects is not None:
                excluded_projects.add(project)
            if excluded_repositories is not None and repository:
                excluded_repositories.add(repository)
    return selected


def syntax_check(archive: Path, runtime: Path, checker: Path,
                 timeout: int = 300) -> tuple[bool, str]:
    try:
        result = subprocess.run(
            [str(runtime), str(checker), str(archive)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return False, str(error)
    detail = result.stdout.strip().splitlines()
    return result.returncode == 0, detail[0] if detail else f"exit {result.returncode}"


def parse_runtime_specs(values: list[str]) -> dict[str, Path]:
    runtimes: dict[str, Path] = {}
    for value in values:
        version, separator, path = value.partition("=")
        if not separator or version not in PYTHON_VERSIONS or not path:
            raise ValueError(f"invalid runtime specification: {value}")
        runtime = Path(path).resolve()
        if not runtime.is_file() or not os.access(runtime, os.X_OK):
            raise ValueError(f"runtime is not executable: {runtime}")
        runtimes[version] = runtime
    return runtimes


def write_json_atomic(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    rendered = json.dumps(value, indent=2, sort_keys=True) + "\n"
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent,
                                     delete=False) as temporary:
        temporary.write(rendered)
        temporary_path = Path(temporary.name)
    temporary_path.replace(path)


def save_progress(path: Path, parameters: dict[str, Any], names: list[str] | None, examined: int,
                  eligible: dict[str, list[dict[str, Any]]], failures: Counter[str]) -> None:
    unique_records: dict[tuple[str, str, str], dict[str, Any]] = {}
    for records in eligible.values():
        for record in records:
            key = (record["package"], record["version"], record["sdist_sha256"])
            unique_records.setdefault(key, record)
    write_json_atomic(path, {
        "schema_version": PROGRESS_SCHEMA_VERSION,
        "parameters": parameters,
        "candidate_names": names,
        "examined_projects": examined,
        "eligible_records": list(unique_records.values()),
        "exclusion_counts": dict(failures),
    })


def load_progress(path: Path, parameters: dict[str, Any], fallback_names: list[str] | None = None
                  ) -> tuple[list[str], int, dict[str, list[dict[str, Any]]], Counter[str]]:
    state = json.loads(path.read_text(encoding="utf-8"))
    schema_version = state.get("schema_version")
    if schema_version not in (1, PROGRESS_SCHEMA_VERSION):
        raise ValueError("unsupported selection checkpoint schema")
    if state.get("parameters") != parameters:
        raise ValueError("selection checkpoint parameters differ; use --refresh to restart")
    names = state.get("candidate_names")
    if names is None:
        names = fallback_names
    examined = state.get("examined_projects")
    if schema_version == 1:
        eligible = state.get("eligible")
    else:
        records = state.get("eligible_records")
        if not isinstance(records, list):
            raise ValueError("invalid eligible records in selection checkpoint")
        eligible = {version: [] for version in PYTHON_VERSIONS}
        for record in records:
            if not isinstance(record, dict):
                raise ValueError("invalid eligible record in selection checkpoint")
            for version in record.get("compatible_cpython", []):
                if version in eligible:
                    eligible[version].append(record)
    if not isinstance(names, list) or not isinstance(examined, int) or not isinstance(eligible, dict):
        raise ValueError("invalid selection checkpoint")
    if set(eligible) != set(PYTHON_VERSIONS) or not all(
            isinstance(items, list) for items in eligible.values()):
        raise ValueError("invalid CPython cohorts in selection checkpoint")
    failures = Counter(state.get("exclusion_counts", {}))
    return names, examined, eligible, failures


def evaluate_candidate(
        name: str, *, population_by_name: dict[str, dict[str, Any]],
        use_population: bool, cache: Path, github_token: str | None,
        github_activity: dict[str, str] | None, release_cutoff: datetime,
        as_of: datetime, activity_cutoff: datetime,
        ) -> tuple[dict[str, Any] | None, list[str], str | None, str | None]:
    """Evaluate and measure one project without mutating aggregate selection state."""
    try:
        if use_population:
            frozen = population_by_name[canonicalize_name(name)]
            latest_version = Version(frozen["version"])
            latest_uploaded = frozen["release_uploaded"]
            sdist = {
                "filename": frozen["sdist_filename"],
                "url": frozen["sdist_url"],
                "digests": {"sha256": frozen["sdist_sha256"]},
            }
            requires_python = frozen.get("requires_python")
            package_name = frozen["package"]
            project_info = {
                "project_urls": frozen["project_urls"],
                "home_page": frozen.get("home_page"),
            }
        else:
            document = cached_json(cache, name)
            releases = stable_releases(document)
            if not releases:
                return None, [], "no_stable_release", None
            latest_version, latest_uploaded, latest_files = max(
                releases, key=lambda item: item[0]
            )
            sdist = choose_sdist(latest_files)
            if not sdist:
                return None, [], "no_sdist", None
            requires_python = (
                sdist.get("requires_python")
                or document.get("info", {}).get("requires_python")
            )
            package_name = document.get("info", {}).get("name", name)
            project_info = document.get("info", {})
        release_time = parse_time(latest_uploaded)
        if release_time < release_cutoff or release_time > as_of:
            return None, [], "release_outside_window", None
        versions = compatible_versions(requires_python)
        if not versions:
            return None, [], "no_supported_cpython", None
        repository = repository_url(project_info)
        if not repository:
            return None, [], "no_repository", None
        handled, repository_state = frozen_github_repository_metadata(
            repository, github_activity
        )
        if not handled:
            repository_state = cached_repository_metadata(
                cache, repository, github_token
            )
        if repository_state is None:
            return None, [], "repository_not_public_or_unreachable", None
        repository_updated = parse_time(repository_state["updated_at"])
        if repository_updated < activity_cutoff:
            return None, [], "repository_not_recently_active", None
        python_files, source_bytes, source_lines = source_measurement_for_sdist(
            cache, sdist
        )
        if not usable_python_source(python_files, source_lines):
            return None, [], "no_python_source", None
        record = {
            "package": package_name,
            "version": str(latest_version),
            "release_uploaded": latest_uploaded,
            "requires_python": requires_python,
            "compatible_cpython": versions,
            "repository": repository,
            "repository_updated": repository_state["updated_at"],
            "sdist_filename": sdist["filename"],
            "sdist_url": sdist["url"],
            "sdist_sha256": sdist["digests"]["sha256"],
            "python_files": python_files,
            "python_source_bytes": source_bytes,
            "python_source_lines": source_lines,
        }
        return record, versions, None, None
    except RuntimeError as error:
        if "rate limit" in str(error).lower():
            return None, [], None, str(error)
        return None, [], "runtime_error", None
    except (HTTPError, URLError, TimeoutError, ValueError, KeyError,
            OSError, tarfile.TarError, zipfile.BadZipFile,
            json.JSONDecodeError):
        return None, [], "metadata_or_archive_error", None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True,
                        help="frozen package-information list (JSON)")
    parser.add_argument("--package-dir", type=Path,
                        help="extracted package root; defaults to packages beside the list")
    parser.add_argument("--cache", type=Path, default=Path(".cache/pypi-selector"))
    parser.add_argument(
        "--population-file", type=Path,
        help="frozen latest-stable-sdist population produced by export_pypi_window.py",
    )
    parser.add_argument(
        "--github-activity-file", type=Path,
        help="frozen PushEvent activity produced by export_github_activity.py",
    )
    parser.add_argument("--refresh", action="store_true",
                        help="replace an existing manifest with a new selection")
    parser.add_argument("--validate-only", action="store_true",
                        help="validate the frozen manifest without downloading sources")
    parser.add_argument(
        "--prune-unused", action="store_true",
        help="after successful materialization, retain only selected sdists in the cache",
    )
    parser.add_argument(
        "--prune-unselected-sources", action="store_true",
        help="remove artifact-marked package trees absent from the selected manifest",
    )
    parser.add_argument("--as-of", type=str,
                        help="UTC ISO date/time; defaults to the current time")
    parser.add_argument("--seed", type=int, default=20260910)
    parser.add_argument("--portions", type=int, default=10,
                        help="equal-count Python-SLoC portions per CPython version")
    parser.add_argument("--per-portion", type=int, default=50,
                        help="packages randomly selected from each size portion")
    parser.add_argument("--release-window-years", "--project-window-years",
                        dest="project_window_years", type=int, default=3,
                        help="maximum age of the selected stable release (default: 3)")
    parser.add_argument(
        "--active-within-days", type=int, default=90,
        help="maximum age of the repository host's last update (default: 90 days)",
    )
    parser.add_argument(
        "--max-projects", type=int,
        help="optional diagnostic cap; omit when constructing a formal benchmark",
    )
    parser.add_argument(
        "--workers", type=int, default=16,
        help="concurrent metadata/download workers (default: 16)",
    )
    parser.add_argument(
        "--python-runtime", action="append", default=[], metavar="VERSION=PATH",
        help="target interpreter used to syntax-check selected sdists; repeat for 3.10--3.14",
    )
    args = parser.parse_args()
    if min(args.portions, args.per_portion, args.project_window_years,
           args.active_within_days) <= 0:
        parser.error("numeric selection parameters must be positive")
    if args.max_projects is not None and args.max_projects <= 0:
        parser.error("--max-projects must be positive")
    if args.workers <= 0:
        parser.error("--workers must be positive")
    if args.validate_only:
        if args.refresh:
            parser.error("--validate-only cannot be combined with --refresh")
        try:
            manifest = json.loads(args.output.read_text(encoding="utf-8"))
            count = validate_manifest(manifest)
        except (OSError, ValueError, KeyError, TypeError,
                json.JSONDecodeError) as error:
            print(f"error: invalid frozen manifest: {error}", file=sys.stderr)
            return 1
        print(f"Validated {count} frozen CPython/package subjects in {args.output}")
        return 0
    try:
        runtimes = parse_runtime_specs(args.python_runtime)
    except ValueError as error:
        parser.error(str(error))

    package_dir = args.package_dir or args.output.parent / "packages"
    progress_path = args.output.with_suffix(args.output.suffix + ".progress.json")
    if args.output.exists() and not args.refresh and not progress_path.exists():
        try:
            manifest = json.loads(args.output.read_text(encoding="utf-8"))
            count = materialize_locked_sources(manifest, package_dir, args.cache)
        except (OSError, ValueError, KeyError, HTTPError, URLError,
                TimeoutError, json.JSONDecodeError) as error:
            print(f"error: cannot reproduce existing manifest: {error}", file=sys.stderr)
            return 1
        print(f"Reused {args.output}; verified/extracted {count} package-version sources into {package_dir}")
        if args.prune_unselected_sources:
            removed = prune_unselected_sources(manifest, package_dir)
            print(f"Removed {removed} unselected package source trees")
        if args.prune_unused:
            removed = prune_unselected_cache(manifest, args.cache)
            print(f"Removed {removed} unselected source archives and transient collection metadata")
        return 0

    missing_runtimes = set(PYTHON_VERSIONS) - set(runtimes)
    if missing_runtimes:
        parser.error(
            "new selections require --python-runtime for: "
            + ", ".join(sorted(missing_runtimes))
        )

    population = None
    population_names = None
    population_by_name: dict[str, dict[str, Any]] = {}
    population_digest = None
    if args.population_file is not None:
        try:
            population = load_population(args.population_file)
            population_names = [record["package"] for record in population["projects"]]
            population_by_name = {
                canonicalize_name(record["package"]): record
                for record in population["projects"]
            }
            population_digest = hashlib.sha256(args.population_file.read_bytes()).hexdigest()
        except (OSError, ValueError, KeyError, TypeError,
                json.JSONDecodeError, InvalidVersion) as error:
            print(f"error: cannot load frozen PyPI population: {error}", file=sys.stderr)
            return 1

    if population is not None:
        as_of = parse_time(population["window_end"])
        if args.as_of is not None and parse_time(args.as_of) != as_of:
            parser.error("--as-of must equal the frozen population's window_end")
    elif not args.as_of and progress_path.exists() and not args.refresh:
        try:
            checkpoint = json.loads(progress_path.read_text(encoding="utf-8"))
            as_of = parse_time(checkpoint["parameters"]["as_of"])
        except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
            print(f"error: cannot read checkpoint date: {error}", file=sys.stderr)
            return 1
    else:
        as_of = parse_time(args.as_of) if args.as_of else datetime.now(timezone.utc)
    activity_cutoff = as_of - timedelta(days=args.active_within_days)
    release_cutoff = as_of - timedelta(days=round(args.project_window_years * 365.2425))
    github_activity = None
    github_activity_digest = None
    if args.github_activity_file is not None:
        if population is None or population_digest is None:
            parser.error("--github-activity-file requires --population-file")
        try:
            github_activity = load_github_activity(
                args.github_activity_file, population_digest,
                activity_cutoff, as_of,
            )
            github_activity_digest = hashlib.sha256(
                args.github_activity_file.read_bytes()
            ).hexdigest()
        except (OSError, ValueError, KeyError, TypeError,
                json.JSONDecodeError) as error:
            print(f"error: cannot load frozen GitHub activity: {error}", file=sys.stderr)
            return 1
    parameters = {
        "as_of": as_of.isoformat(), "seed": args.seed, "portions": args.portions,
        "per_portion": args.per_portion, "project_window_years": args.project_window_years,
        "active_within_days": args.active_within_days, "max_projects": args.max_projects,
        "population_sha256": population_digest,
        "github_activity_sha256": github_activity_digest,
    }
    if args.refresh and progress_path.exists():
        progress_path.unlink()
    if progress_path.exists():
        try:
            names, examined, eligible, failures = load_progress(
                progress_path, parameters, population_names
            )
        except (OSError, ValueError, json.JSONDecodeError) as error:
            print(f"error: cannot resume selection: {error}", file=sys.stderr)
            return 1
        print(f"Resuming selection after {examined} projects from {progress_path}")
    else:
        try:
            names = population_names if population_names is not None else project_names()
        except (HTTPError, URLError, TimeoutError, ValueError,
                json.JSONDecodeError) as error:
            print(f"error: cannot fetch the PyPI project index: {error}", file=sys.stderr)
            return 1
        if args.max_projects is not None:
            random.Random(args.seed).shuffle(names)
            names = names[:args.max_projects]
        examined = 0
        eligible = {version: [] for version in PYTHON_VERSIONS}
        failures = Counter()
        save_progress(
            progress_path, parameters,
            None if population_names is not None else names,
            examined, eligible, failures,
        )
    github_token = os.environ.get("GITHUB_TOKEN")
    checkpoint_names = None if population_names is not None else names
    if population is not None:
        frozen_records = [
            population_by_name[canonicalize_name(name)] for name in names
        ]
        github_repositories = github_repositories_in_population(frozen_records)
        prefetched = 0
        if github_activity is None:
            try:
                prefetched = prefetch_github_repository_metadata(
                    args.cache, github_repositories, github_token
                )
            except RuntimeError as error:
                print(f"error: {error}", file=sys.stderr)
                return 1
        print(
            f"Prepared activity metadata for {len(github_repositories)} GitHub "
            f"repositories ({prefetched} newly queried; "
            f"{len(github_activity or {})} active in frozen snapshot)"
        )

    def evaluate(name: str) -> tuple[
            dict[str, Any] | None, list[str], str | None, str | None]:
        return evaluate_candidate(
            name,
            population_by_name=population_by_name,
            use_population=population is not None,
            cache=args.cache,
            github_token=github_token,
            github_activity=github_activity,
            release_cutoff=release_cutoff,
            as_of=as_of,
            activity_cutoff=activity_cutoff,
        )

    batch_size = max(CHECKPOINT_INTERVAL, args.workers * 8)
    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        while examined < len(names):
            batch = names[examined:examined + batch_size]
            results = executor.map(evaluate, batch)
            for record, versions, failure, fatal in results:
                if fatal is not None:
                    save_progress(
                        progress_path, parameters, checkpoint_names,
                        examined, eligible, failures,
                    )
                    print(f"error: {fatal}", file=sys.stderr)
                    return 1
                examined += 1
                if failure is not None:
                    failures[failure] += 1
                elif record is not None:
                    for version in versions:
                        eligible[version].append(record)
            save_progress(
                progress_path, parameters, checkpoint_names,
                examined, eligible, failures,
            )
            cohort_counts = ", ".join(
                f"{version}={len(records)}"
                for version, records in eligible.items()
            )
            print(
                f"Examined {examined}/{len(names)} projects; "
                f"eligible observations: {cohort_counts}",
                flush=True,
            )

    checker = Path(__file__).with_name("check_package_syntax.py").resolve()
    compilation_rejections: list[dict[str, str]] = []
    diversity_exclusions: Counter[str] = Counter()
    selected_projects: set[str] = set()
    selected_repositories: set[str] = set()
    benchmarks = {}
    for version, records in eligible.items():
        def validate(record: dict[str, Any], *, target: str = version
                     ) -> tuple[bool, str]:
            archive = _archive_for_record(record, args.cache)
            safe, reason = archive_paths_are_safe(archive)
            if not safe:
                return False, reason
            return syntax_check(archive, runtimes[target], checker)

        benchmarks[version] = stratified_selection(
            records, args.portions, args.per_portion, args.seed, version,
            validator=validate, rejections=compilation_rejections,
            excluded_projects=selected_projects,
            excluded_repositories=selected_repositories,
            diversity_exclusions=diversity_exclusions,
        )
    manifest = {
        "schema_version": MANIFEST_SCHEMA_VERSION,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "selection": {
            "as_of": as_of.isoformat(),
            "seed": args.seed,
            "release_window_years": args.project_window_years,
            "repository_active_within_days": args.active_within_days,
            "public_repository_required": True,
            "repository_activity_source": (
                "GitHub Archive PushEvent snapshot and public forge repository APIs"
                if github_activity is not None
                else "public forge repository API"
            ),
            "github_activity_sha256": github_activity_digest,
            "stable_releases_only": True,
            "sdist_required": True,
            "requires_python_required": True,
            "size_metric": "non-blank, non-comment physical Python source lines",
            "size_stratification": "equal-count portions after ranking by Python source lines",
            "cross_cohort_deduplication": "canonical PyPI project and repository URL",
            "syntax_validation": "every Python file compiled by the assigned CPython runtime",
            "syntax_validated_cpython": list(PYTHON_VERSIONS),
            "size_portions_per_version": args.portions,
            "random_selections_per_portion": args.per_portion,
            "population_order": (
                "complete frozen PyPI latest-stable-sdist population"
                if population is not None and args.max_projects is None
                else "seeded diagnostic subsample of the frozen PyPI population"
                if population is not None
                else "complete PEP 503 project index"
                if args.max_projects is None
                else "seeded diagnostic subsample of the PEP 503 project index"
            ),
            "candidate_scan_limit": args.max_projects,
            "population_sha256": population_digest,
            "population_source_table": (
                population.get("source_table") if population is not None else None
            ),
            "population_window_start": (
                population.get("window_start") if population is not None else None
            ),
            "population_window_end": (
                population.get("window_end") if population is not None else None
            ),
        },
        "examined_projects": examined,
        "eligible_projects": {version: len(records) for version, records in eligible.items()},
        "exclusion_counts": dict(sorted(failures.items())),
        "compilation_rejections": compilation_rejections,
        "diversity_exclusion_counts": dict(sorted(diversity_exclusions.items())),
        "benchmarks": benchmarks,
    }
    missing = [(version, portion, args.per_portion - len(items))
               for version, portions in benchmarks.items() for portion, items in portions.items()
               if len(items) < args.per_portion]
    if missing:
        save_progress(
            progress_path, parameters, checkpoint_names,
            examined, eligible, failures,
        )
        print(f"Examined {examined} projects; selection is incomplete", file=sys.stderr)
        for version, portion, count in missing:
            print(f"missing {count}: CPython {version} / {portion}", file=sys.stderr)
        return 1
    manifest["package_version_entries"] = sum(
        len(records) for portions in benchmarks.values() for records in portions.values()
    )
    manifest["unique_sdists"] = len(selected_packages(manifest))
    validate_manifest(manifest)
    write_json_atomic(args.output, manifest)
    progress_path.unlink(missing_ok=True)
    print(f"Examined {examined} projects; wrote {args.output}")
    try:
        count = materialize_locked_sources(manifest, package_dir, args.cache)
    except (OSError, ValueError, KeyError, HTTPError, URLError, TimeoutError) as error:
        print(f"error: package list was saved, but source extraction failed: {error}", file=sys.stderr)
        return 1
    print(f"Verified/extracted {count} package-version sources into {package_dir}")
    if args.prune_unselected_sources:
        removed = prune_unselected_sources(manifest, package_dir)
        print(f"Removed {removed} unselected package source trees")
    if args.prune_unused:
        removed = prune_unselected_cache(manifest, args.cache)
        print(f"Removed {removed} unselected source archives and transient collection metadata")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
