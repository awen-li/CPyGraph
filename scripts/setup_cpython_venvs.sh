#!/usr/bin/env bash
set -euo pipefail

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly DEFAULT_ENV_ROOT="${REPOSITORY_ROOT}/builds/cpython-venvs"
readonly DEFAULT_CONDA="conda"
readonly CONDA_CHANNEL="conda-forge"
readonly -a CPYTHON_VERSIONS=("3.10" "3.11" "3.12" "3.13" "3.14")

env_root="${CPYGRAPH_CPYTHON_ROOT:-${DEFAULT_ENV_ROOT}}"
conda_executable="${CONDA_EXE:-${DEFAULT_CONDA}}"

usage() {
    echo "usage: $0 [--root DIRECTORY] [--conda EXECUTABLE]" >&2
}

while (($#)); do
    case "$1" in
        --root)
            env_root="$2"
            shift 2
            ;;
        --conda)
            conda_executable="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done

if ! command -v "${conda_executable}" >/dev/null 2>&1; then
    echo "error: conda executable not found: ${conda_executable}" >&2
    exit 1
fi
conda_executable="$(command -v "${conda_executable}")"

mkdir -p "${env_root}"
for version in "${CPYTHON_VERSIONS[@]}"; do
    environment="${env_root}/cpython-${version}"
    if [[ ! -x "${environment}/bin/python" ]]; then
        "${conda_executable}" create --yes --prefix "${environment}" \
            --channel "${CONDA_CHANNEL}" "python=${version}" pip
    fi
    actual_version="$("${environment}/bin/python" -c \
        'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
    if [[ "${actual_version}" != "${version}" ]]; then
        echo "error: ${environment} provides CPython ${actual_version}, expected ${version}" >&2
        exit 1
    fi
    "${environment}/bin/python" -m pip --version
done

echo "Reusable CPython environments are ready under ${env_root}."
