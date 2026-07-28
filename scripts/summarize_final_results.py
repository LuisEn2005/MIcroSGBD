#!/usr/bin/env python3
"""Genera un resumen Markdown reproducible desde el CSV final."""

from __future__ import annotations

import csv
import sys
from pathlib import Path


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def select(
    rows: list[dict[str, str]],
    *,
    experiment: str,
    query_type: str,
    cache_state: str,
    pool_size: str = "3",
) -> list[dict[str, str]]:
    return sorted(
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


def markdown_table(rows: list[dict[str, str]]) -> str:
    lines = [
        "| Registros | Plan | Mediana (ms) | Lecturas | Hits | Examinados | Filas |",
        "|---:|:---:|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        lines.append(
            "| {records} | {plan} | {median_ms} | {avg_disk_reads} | "
            "{avg_buffer_hits} | {avg_records_examined} | {avg_rows_returned} |".format(
                **row
            )
        )
    return "\n".join(lines)


def main() -> int:
    input_path = Path(sys.argv[1] if len(sys.argv) > 1 else "results/sprint6_final_summary.csv")
    output_path = Path(sys.argv[2] if len(sys.argv) > 2 else "results/sprint6_final_report.md")

    rows = read_rows(input_path)
    indexed = select(
        rows,
        experiment="query_comparison",
        query_type="equality_indexed",
        cache_state="cold",
    )
    sequential = select(
        rows,
        experiment="query_comparison",
        query_type="equality_unindexed",
        cache_state="cold",
    )
    inserts_without = select(
        rows,
        experiment="bulk_insert",
        query_type="insert_without_index",
        cache_state="cold",
        pool_size="50",
    )
    inserts_with = select(
        rows,
        experiment="bulk_insert",
        query_type="insert_with_index",
        cache_state="cold",
        pool_size="50",
    )

    report = f"""# Resultados finales del Mini-SGBD

Este archivo se genera automáticamente desde `{input_path}`.

## Igualdad con índice Hash — caché fría, 3 frames

{markdown_table(indexed)}

## Igualdad sin índice — caché fría, 3 frames

{markdown_table(sequential)}

## Inserción masiva sin índice

{markdown_table(inserts_without)}

## Inserción masiva manteniendo índice

{markdown_table(inserts_with)}

## Interpretación

- `IndexScan` debe examinar menos registros y realizar menos lecturas físicas que `SeqScan` para igualdad selectiva.
- La caché caliente debe aumentar los hits y reducir misses/lecturas en consultas repetidas.
- Mantener un índice incrementa el costo de inserción porque cada fila actualiza el `HeapFile` y el bucket Hash correspondiente.
- Los valores son resultados experimentales; deben citarse junto con el tamaño del dataset, frames, estado de caché y número de repeticiones.
"""

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(report, encoding="utf-8")
    print(f"[OK] Markdown report: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
