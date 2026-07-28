#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"
RUN_SANITIZERS=0

if [[ "${1:-}" == "--sanitizers" || "${1:-}" == "--all" ]]; then
    RUN_SANITIZERS=1
fi

clean_generated_data() {
    mkdir -p data results
    rm -f data/*.db data/*.idx data/*.sql minisgbd.db
}

configure_build_test() {
    local build_dir="$1"
    local build_type="$2"

    rm -rf "$build_dir"
    cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE="$build_type"
    cmake --build "$build_dir" -j"$JOBS"
    ctest --test-dir "$build_dir" --output-on-failure
}

echo "=== Sprint 6 final validation ==="
clean_generated_data

echo "=== Debug ==="
configure_build_test build-debug Debug

echo "=== Release ==="
clean_generated_data
configure_build_test build-release Release

if [[ "$RUN_SANITIZERS" -eq 1 ]]; then
    echo "=== AddressSanitizer + UndefinedBehaviorSanitizer ==="
    clean_generated_data
    rm -rf build-san
    cmake -S . -B build-san \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
    cmake --build build-san -j"$JOBS"
    # Los benchmarks de tiempo se excluyen bajo sanitizadores porque su costo
    # se multiplica considerablemente; la aceptación funcional sí se ejecuta.
    ASAN_OPTIONS=detect_leaks=1 \
        ctest --test-dir build-san --output-on-failure -LE benchmark
fi

echo "=== Validation completed successfully ==="
