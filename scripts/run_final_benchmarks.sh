#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"
MODE="${1:-full}"

mkdir -p data results
rm -f results/sprint6_final_results.csv \
      results/sprint6_final_summary.csv \
      results/sprint6_final_report.md

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release \
    --target sprint6_final_benchmarks \
    -j"$JOBS"

if [[ "$MODE" == "quick" || "$MODE" == "--quick" ]]; then
    ./build-release/sprint6_final_benchmarks --quick
else
    ./build-release/sprint6_final_benchmarks
fi

python3 scripts/summarize_final_results.py \
    results/sprint6_final_summary.csv \
    results/sprint6_final_report.md

if python3 -c "import matplotlib" >/dev/null 2>&1; then
    python3 scripts/generate_final_charts.py \
        results/sprint6_final_summary.csv \
        results

else
    echo "[WARN] matplotlib no está instalado; se omiten los gráficos PNG."
    echo "       Los CSV y el reporte Markdown sí fueron generados."
fi

test -s results/sprint6_final_results.csv
test -s results/sprint6_final_summary.csv
test -s results/sprint6_final_report.md

echo "=== Final benchmarks completed ==="
echo "Per-run: results/sprint6_final_results.csv"
echo "Summary: results/sprint6_final_summary.csv"
echo "Report:  results/sprint6_final_report.md"
