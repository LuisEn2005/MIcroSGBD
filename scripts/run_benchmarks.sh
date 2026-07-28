#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-release"

cd "${ROOT_DIR}"
mkdir -p results data

printf '%s\n' "=== Configuring release benchmark build ==="
cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" -j"$(nproc)"

printf '%s\n' "=== Running stabilization acceptance ==="
ctest \
    --test-dir "${BUILD_DIR}" \
    --output-on-failure \
    -R '^sprint5_stabilization_tests$'

printf '%s\n' "=== Generating deterministic demonstration SQL ==="
python3 scripts/generate_dataset.py \
    10000 \
    data/dataset_10k.sql \
    --seed 42

printf '%s\n' "=== Running Sprint 5 experiments ==="
"${BUILD_DIR}/sprint5_experiments"

RESULT_FILE="results/sprint5_experiments_summary.csv"

if [[ ! -s "${RESULT_FILE}" ]]; then
    printf '%s\n' "[ERROR] Benchmark result file was not created: ${RESULT_FILE}" >&2
    exit 1
fi

printf '%s\n' "=== Benchmarks completed successfully ==="
printf 'Results: %s\n' "${RESULT_FILE}"
