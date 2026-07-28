#!/usr/bin/env bash
# Script de automatización de benchmarks del Mini-SGBD
set -e

echo "=== Compilando Mini-SGBD para Benchmarks ==="
mkdir -p build results data
cmake -S . -B build
cmake --build build --config Release

echo "=== Generando Datasets Deterministas ==="
python3 scripts/generate_dataset.py 10000 data/dataset_10k.sql

echo "=== Ejecutando Benchmark de Métricas del Sprint 4 ==="
./build/sprint4_metrics_tests

echo "=== Benchmarks completados con exito ==="
echo "Resultados exportados a: results/sprint4_benchmark_results.csv"
