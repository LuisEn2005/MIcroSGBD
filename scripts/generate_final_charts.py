#!/usr/bin/env python3
"""Genera gráficos PNG a partir del resumen final de benchmarks."""

from __future__ import annotations

import csv
import sys
from pathlib import Path

import matplotlib

# Backend sin interfaz gráfica. Permite generar PNG en terminal,
# Wayland, SSH, CI y entornos donde GTK no está disponible.
matplotlib.use("Agg", force=True)

import matplotlib.pyplot as plt


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def values(
    rows: list[dict[str, str]],
    query_type: str,
    metric: str,
    *,
    cache_state: str = "cold",
    pool_size: str = "3",
    experiment: str = "query_comparison",
) -> tuple[list[int], list[float]]:
    selected = sorted(
        [
            row
            for row in rows
            if row["experiment"] == experiment
            and row["query_type"] == query_type
            and row["cache_state"] == cache_state
            and row["pool_size"] == pool_size
        ],
        key=lambda row: int(row["records"]),
    )
    return (
        [int(row["records"]) for row in selected],
        [float(row[metric]) for row in selected],
    )


def save_index_vs_seq(rows: list[dict[str, str]], output_dir: Path) -> None:
    x_index, y_index = values(rows, "equality_indexed", "median_ms")
    x_seq, y_seq = values(rows, "equality_unindexed", "median_ms")

    plt.figure()
    plt.plot(x_index, y_index, marker="o", label="IndexScan")
    plt.plot(x_seq, y_seq, marker="o", label="SeqScan")
    plt.xlabel("Registros")
    plt.ylabel("Mediana de tiempo (ms)")
    plt.title("IndexScan vs SeqScan — caché fría, 3 frames")
    plt.legend()
    plt.tight_layout()
    plt.savefig(output_dir / "sprint6_index_vs_seqscan.png", dpi=180)
    plt.close()


def save_disk_reads(rows: list[dict[str, str]], output_dir: Path) -> None:
    x_index, y_index = values(rows, "equality_indexed", "avg_disk_reads")
    x_seq, y_seq = values(rows, "equality_unindexed", "avg_disk_reads")

    plt.figure()
    plt.plot(x_index, y_index, marker="o", label="IndexScan")
    plt.plot(x_seq, y_seq, marker="o", label="SeqScan")
    plt.xlabel("Registros")
    plt.ylabel("Lecturas físicas promedio")
    plt.title("Costo de E/S — caché fría, 3 frames")
    plt.legend()
    plt.tight_layout()
    plt.savefig(output_dir / "sprint6_disk_reads.png", dpi=180)
    plt.close()


def save_insert_cost(rows: list[dict[str, str]], output_dir: Path) -> None:
    x_no, y_no = values(
        rows,
        "insert_without_index",
        "median_ms",
        pool_size="50",
        experiment="bulk_insert",
    )
    x_idx, y_idx = values(
        rows,
        "insert_with_index",
        "median_ms",
        pool_size="50",
        experiment="bulk_insert",
    )

    plt.figure()
    plt.plot(x_no, y_no, marker="o", label="Sin índice")
    plt.plot(x_idx, y_idx, marker="o", label="Manteniendo índice")
    plt.xlabel("Registros insertados")
    plt.ylabel("Mediana de tiempo (ms)")
    plt.title("Costo de inserción")
    plt.legend()
    plt.tight_layout()
    plt.savefig(output_dir / "sprint6_insert_cost.png", dpi=180)
    plt.close()


def main() -> int:
    input_path = Path(sys.argv[1] if len(sys.argv) > 1 else "results/sprint6_final_summary.csv")
    output_dir = Path(sys.argv[2] if len(sys.argv) > 2 else "results")
    output_dir.mkdir(parents=True, exist_ok=True)

    rows = read_rows(input_path)
    save_index_vs_seq(rows, output_dir)
    save_disk_reads(rows, output_dir)
    save_insert_cost(rows, output_dir)
    print(f"[OK] Charts written to {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
