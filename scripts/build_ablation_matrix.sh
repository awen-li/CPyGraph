#!/usr/bin/env bash
set -euo pipefail

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly DEFAULT_ENV_ROOT="${REPOSITORY_ROOT}/builds/cpython-venvs"
readonly DEFAULT_BUILD_ROOT="${REPOSITORY_ROOT}/builds/ablations"
readonly DEFAULT_PYTHON_VERSION="3.10"
readonly -a ABLATIONS=(
    "none"
    "field-model"
    "bound-method"
    "exception-flow"
    "interprocedural-ddg"
)

env_root="${CPYGRAPH_CPYTHON_ROOT:-${DEFAULT_ENV_ROOT}}"
build_root="${CPYGRAPH_ABLATION_BUILD_ROOT:-${DEFAULT_BUILD_ROOT}}"
python_version="${CPYGRAPH_ABLATION_PYTHON_VERSION:-${DEFAULT_PYTHON_VERSION}}"
jobs="${CPYGRAPH_BUILD_JOBS:-2}"

if [[ -n "$(git -C "${REPOSITORY_ROOT}" status --porcelain --untracked-files=all)" ]]; then
    echo "error: commit or remove source-tree changes before building ablations" >&2
    exit 1
fi
python_executable="${env_root}/cpython-${python_version}/bin/python"
if [[ ! -x "${python_executable}" ]]; then
    echo "error: missing ${python_executable}; run scripts/setup_cpython_venvs.sh" >&2
    exit 1
fi
if [[ ! "${jobs}" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: CPYGRAPH_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

mkdir -p "${build_root}"
source_revision="$(git -C "${REPOSITORY_ROOT}" rev-parse HEAD)"
for ablation in "${ABLATIONS[@]}"; do
    build_directory="${build_root}/${ablation}"
    cmake -S "${REPOSITORY_ROOT}" -B "${build_directory}" \
        -DPython3_EXECUTABLE="${python_executable}" \
        -DPython3_ROOT_DIR="${env_root}/cpython-${python_version}" \
        -DPython3_FIND_VIRTUALENV=ONLY \
        -DBUILD_TESTING=ON \
        -DCPYGRAPH_BUILD_BENCHMARKS=ON \
        -DCPYGRAPH_BUILD_TOOLS=ON \
        -DCPYGRAPH_ABLATION="${ablation}"
    cmake --build "${build_directory}" --parallel "${jobs}"
    executable="${build_directory}/pygbench_case_analysis"
    if [[ ! -x "${executable}" ]]; then
        echo "error: missing ablation executable: ${executable}" >&2
        exit 1
    fi
    metadata="${build_directory}/cpygraph-ablation-build.json"
    "${python_executable}" -c \
        'import hashlib,json,pathlib,sys
tool=pathlib.Path(sys.argv[4])
pathlib.Path(sys.argv[5]).write_text(json.dumps({
    "schema_version": 1,
    "ablation": sys.argv[1],
    "source_revision": sys.argv[2],
    "python_version": sys.argv[3],
    "executable": str(tool),
    "executable_sha256": hashlib.sha256(tool.read_bytes()).hexdigest(),
}, indent=2, sort_keys=True)+"\n")' \
        "${ablation}" "${source_revision}" "${python_version}" \
        "${executable}" "${metadata}"
done
