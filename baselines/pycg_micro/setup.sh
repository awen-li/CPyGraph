#!/usr/bin/env bash
set -euo pipefail

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly DEFAULT_BASELINE_ROOT="${REPOSITORY_ROOT}/builds/baselines"
readonly DEFAULT_CPYTHON_ROOT="${REPOSITORY_ROOT}/builds/cpython-venvs"
readonly BASELINE_PYTHON_VERSION="3.10"
readonly PYCG_URL="https://github.com/vitsalis/PyCG.git"
readonly PYCG_COMMIT="3b37b54f0ba86eb272c5fb70b0d0a3cfccc85986"

baseline_root="${CPYGRAPH_BASELINE_ROOT:-${DEFAULT_BASELINE_ROOT}}"
cpython_root="${CPYGRAPH_CPYTHON_ROOT:-${DEFAULT_CPYTHON_ROOT}}"
source_root="${baseline_root}/sources/pycg"
environment="${baseline_root}/envs/pycg"

mkdir -p "${baseline_root}/sources"
if [[ ! -d "${source_root}/.git" ]]; then
    git clone "${PYCG_URL}" "${source_root}"
fi
if [[ -n "$(git -C "${source_root}" status --porcelain)" ]]; then
    echo "error: PyCG source checkout has local changes: ${source_root}" >&2
    exit 1
fi
git -C "${source_root}" fetch origin "${PYCG_COMMIT}"
git -C "${source_root}" checkout --detach "${PYCG_COMMIT}"

actual_commit="$(git -C "${source_root}" rev-parse HEAD)"
if [[ "${actual_commit}" != "${PYCG_COMMIT}" ]]; then
    echo "error: expected PyCG ${PYCG_COMMIT}, found ${actual_commit}" >&2
    exit 1
fi

python_executable="${cpython_root}/cpython-${BASELINE_PYTHON_VERSION}/bin/python"
if [[ ! -x "${python_executable}" ]]; then
    echo "error: missing ${python_executable}; run scripts/setup_cpython_venvs.sh" >&2
    exit 1
fi
if [[ ! -x "${environment}/bin/python" ]]; then
    mkdir -p "${baseline_root}/envs"
    "${python_executable}" -m venv "${environment}"
fi
"${environment}/bin/python" -c 'import pkg_resources' 2>/dev/null

echo "PyCG ICSE 2021 micro-benchmark ready: ${source_root}/micro-benchmark/snippets"
echo "PyCG CPython runtime ready: ${environment}/bin/python"
