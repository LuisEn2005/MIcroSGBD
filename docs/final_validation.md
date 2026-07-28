# Validación final — Sprint 6

## Alcance congelado

La versión final incluye:

- páginas físicas de 4096 bytes;
- Slotted Pages y registros variables;
- HeapFile multipágina;
- Buffer Pool con Clock;
- catálogo persistente;
- índice Hash persistente con overflow;
- `CREATE TABLE`, `CREATE INDEX`, `INSERT`, `SELECT`, `UPDATE`, `DELETE`;
- operadores Volcano `SeqScan`, `IndexScan`, `Filter` y `Projection`;
- `EXPLAIN ANALYZE` y métricas físicas;
- mantenimiento de índices durante mutaciones;
- reutilización persistente de páginas.

No deben agregarse funcionalidades grandes durante este sprint. WAL, transacciones,
B+ Tree y concurrencia completa se documentan como trabajo futuro.

## Matriz de validación

| Validación | Comando | Criterio |
|---|---|---|
| Debug | `scripts/run_final_validation.sh` | Todas las pruebas pasan |
| Release | `scripts/run_final_validation.sh` | Todas las pruebas pasan |
| Sanitizadores | `scripts/run_final_validation.sh --sanitizers` | Pruebas funcionales sin ASan/UBSan; benchmark excluido |
| Aceptación integral | `./build-debug/sprint6_final_acceptance_tests` | 10 000 registros, 3 frames |
| Benchmarks rápidos | `scripts/run_final_benchmarks.sh quick` | CSV y reporte generados |
| Benchmarks finales | `scripts/run_final_benchmarks.sh` | 1k, 10k y 50k |
| Demostración | `scripts/run_final_demo.sh` | Persistencia tras reapertura |

## Criterios de cierre

- El repositorio está limpio.
- `git diff --check` no reporta errores.
- CTest reconoce 13 pruebas.
- Debug, Release y sanitizadores pasan.
- El archivo `.db` es múltiplo de 4096 bytes.
- No quedan páginas fijadas al finalizar la aceptación.
- Los resultados indexados y secuenciales son equivalentes.
- `IndexScan` reduce registros examinados y lecturas para igualdad selectiva.
- Los CSV se generaron automáticamente.
- El README, artículo, video y enlace final están actualizados.
