# Mini-SGBD v1.0.0

Mini sistema gestor de bases de datos persistente implementado en C++17 para el
curso de Base de Datos II.

## Características

- archivos binarios divididos en páginas de 4096 bytes;
- Slotted Pages para registros de longitud variable;
- HeapFile multipágina y RecordID estable;
- Buffer Pool con reemplazo Clock, pinning y páginas sucias;
- catálogo persistente en la página 0;
- índice Hash persistente con buckets y overflow;
- reutilización de slots y PageId;
- `CREATE TABLE`, `CREATE INDEX`, `INSERT`, `SELECT`, `UPDATE` y `DELETE`;
- ejecución Volcano con `SeqScan`, `IndexScan`, `Filter` y `Projection`;
- `EXPLAIN ANALYZE` con métricas de E/S y Buffer Pool;
- pruebas integrales, benchmarks reproducibles y sanitizadores.

## Arquitectura

```text
SQL
 |
 v
Tokenizer -> Parser -> QueryExecutor
                         |
                         +-> SeqScan -> HeapFile -> BufferPool -> DiskManager
                         |
                         +-> IndexScan -> HashIndex -> RecordID -> HeapFile
```

Las mutaciones pasan por `TableStorage`, que mantiene sincronizados el HeapFile
y todos los índices asociados a la tabla.

## Compilación

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

## Pruebas

```bash
ctest --test-dir build --output-on-failure
```

La validación final completa:

```bash
./scripts/run_final_validation.sh --sanitizers
```

## Consola

```bash
./build/mini-sgbd
```

Ejemplo:

```sql
CREATE TABLE users (id INT, name VARCHAR(30), age INT, active BOOLEAN);
CREATE INDEX idx_users_id ON users(id);
INSERT INTO users VALUES (1, 'Ana', 25, true);
SELECT * FROM users WHERE id = 1;
EXPLAIN ANALYZE SELECT * FROM users WHERE id = 1;
```

## Demostración reproducible

```bash
./scripts/run_final_demo.sh
```

El script ejecuta una primera sesión con DDL/DML y una segunda sesión para
comprobar persistencia.

## Benchmarks

Prueba rápida:

```bash
./scripts/run_final_benchmarks.sh quick
```

Experimento final:

```bash
./scripts/run_final_benchmarks.sh
```

Genera:

```text
results/sprint6_final_results.csv
results/sprint6_final_summary.csv
results/sprint6_final_report.md
```

Y, cuando `matplotlib` está disponible:

```text
results/sprint6_index_vs_seqscan.png
results/sprint6_disk_reads.png
results/sprint6_insert_cost.png
```

## Estructura principal

```text
include/    Interfaces públicas
src/        Implementaciones
storage/    Páginas, HeapFile, registros y TableStorage
buffer/     Buffer Pool y Clock
index/      HashIndex persistente
catalog/    Catálogo persistente
query/      Parser, executor y operadores Volcano
tests/      Pruebas unitarias, integración y aceptación
scripts/    Validación, demo y benchmarks
docs/       Arquitectura, formato, artículo y defensa
```

## Limitaciones

- no existe WAL ni recuperación automática ante caídas;
- no hay transacciones `BEGIN/COMMIT/ROLLBACK`;
- el catálogo está limitado a una página;
- no se implementa B+ Tree ni optimización completa basada en costos;
- la concurrencia está limitada;
- los resultados de SELECT se materializan en memoria.

Estas limitaciones se consideran trabajo futuro y no forman parte del alcance de
la versión 1.0.0.
