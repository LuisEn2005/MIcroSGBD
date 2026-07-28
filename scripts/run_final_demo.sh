#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target mini-sgbd -j"$JOBS"

rm -f minisgbd.db

echo "=== Primera ejecución: creación y mutaciones ==="
./build-release/mini-sgbd < demo/sprint6_demo_setup.sql

echo "=== Segunda ejecución: comprobación de persistencia ==="
./build-release/mini-sgbd < demo/sprint6_demo_reopen.sql
