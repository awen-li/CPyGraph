#!/usr/bin/env bash
set -euo pipefail

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly DEFAULT_ENV_ROOT="${REPOSITORY_ROOT}/builds/cpython-venvs"
readonly DEFAULT_BUILD_ROOT="${REPOSITORY_ROOT}/builds"
readonly -a DEFAULT_VERSIONS=("3.10" "3.11" "3.12" "3.13" "3.14")
readonly BUILD_METADATA_SCHEMA_VERSION="1"

env_root="${CPYGRAPH_CPYTHON_ROOT:-${DEFAULT_ENV_ROOT}}"
build_root="${CPYGRAPH_BUILD_ROOT:-${DEFAULT_BUILD_ROOT}}"
jobs="${CPYGRAPH_BUILD_JOBS:-2}"
versions=()

usage() {
    echo "usage: $0 [--env-root DIR] [--build-root DIR] [--jobs N] [VERSION ...]" >&2
}

while (($#)); do
    case "$1" in
        --env-root)
            env_root="$2"
            shift 2
            ;;
        --build-root)
            build_root="$2"
            shift 2
            ;;
        --jobs)
            jobs="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --*)
            usage
            exit 2
            ;;
        *)
            versions+=("$1")
            shift
            ;;
    esac
done

if ((${#versions[@]} == 0)); then
    versions=("${DEFAULT_VERSIONS[@]}")
fi
if [[ ! "${jobs}" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: --jobs must be a positive integer" >&2
    exit 2
fi

if [[ -n "$(git -C "${REPOSITORY_ROOT}" status --porcelain --untracked-files=all)" ]]; then
    echo "error: commit or remove source-tree changes before creating reusable builds" >&2
    exit 1
fi
env_root="$(cd "${env_root}" && pwd)"
mkdir -p "${build_root}"
build_root="$(cd "${build_root}" && pwd)"

for version in "${versions[@]}"; do
    if [[ ! "${version}" =~ ^3\.(10|11|12|13|14)$ ]]; then
        echo "error: unsupported CPython version: ${version}" >&2
        exit 2
    fi
    environment="${env_root}/cpython-${version}"
    python_executable="${environment}/bin/python"
    build_directory="${build_root}/cpython-${version}"
    if [[ ! -x "${python_executable}" ]]; then
        echo "error: missing ${python_executable}; run scripts/setup_cpython_venvs.sh" >&2
        exit 1
    fi
    actual_version="$(${python_executable} -c \
        'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
    if [[ "${actual_version}" != "${version}" ]]; then
        echo "error: ${python_executable} provides ${actual_version}, expected ${version}" >&2
        exit 1
    fi

    cmake -S "${REPOSITORY_ROOT}" -B "${build_directory}" \
        -DPython3_EXECUTABLE="${python_executable}" \
        -DPython3_ROOT_DIR="${environment}" \
        -DPython3_FIND_VIRTUALENV=ONLY \
        -DBUILD_TESTING=ON \
        -DCPYGRAPH_BUILD_BENCHMARKS=ON \
        -DCPYGRAPH_BUILD_TOOLS=ON
    cmake --build "${build_directory}" --parallel "${jobs}"
    ctest --test-dir "${build_directory}" --output-on-failure -L unit

    for executable in \
        "${build_directory}/tools/pygCG" \
        "${build_directory}/tools/pygCFG" \
        "${build_directory}/tools/pygCDG" \
        "${build_directory}/tools/pygDDG" \
        "${build_directory}/pygbench_case_analysis"; do
        if [[ ! -x "${executable}" ]]; then
            echo "error: expected build artifact is missing: ${executable}" >&2
            exit 1
        fi
    done
    source_revision="$(git -C "${REPOSITORY_ROOT}" rev-parse HEAD)"
    metadata_path="${build_directory}/cpygraph-build.json"
    temporary_metadata="${metadata_path}.tmp"
    "${python_executable}" -c \
        'import hashlib,json,pathlib,sys
tool=pathlib.Path(sys.argv[5])
document={
    "schema_version": int(sys.argv[1]),
    "cpython": sys.argv[2],
    "python_executable": sys.argv[3],
    "source_revision": sys.argv[4],
    "pygddg": str(tool),
    "pygddg_sha256": hashlib.sha256(tool.read_bytes()).hexdigest(),
}
pathlib.Path(sys.argv[6]).write_text(json.dumps(document, indent=2, sort_keys=True)+"\n")' \
        "${BUILD_METADATA_SCHEMA_VERSION}" "${version}" \
        "${python_executable}" "${source_revision}" \
        "${build_directory}/tools/pygDDG" "${temporary_metadata}"
    mv "${temporary_metadata}" "${metadata_path}"
    echo "CPyGraph CPython ${version} build ready: ${build_directory}"
done
