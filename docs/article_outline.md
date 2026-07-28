# Esqueleto del artículo científico

## 1. Título

**Diseño e implementación de un Mini-SGBD persistente con Buffer Pool e índice Hash**

## 2. Resumen

Indicar problema, arquitectura, metodología experimental, resultado principal y
limitaciones. No incluir citas en el resumen.

## 3. Palabras clave

Mini-SGBD; almacenamiento paginado; Buffer Pool; índice Hash; modelo Volcano.

## 4. Introducción

- Motivación.
- Problema abordado.
- Objetivo general.
- Contribución del prototipo.

## 5. Trabajos relacionados

- Organización por páginas.
- Slotted Pages.
- Buffer replacement.
- Índices Hash.
- Ejecución Volcano.

## 6. Arquitectura propuesta

Presentar:

```text
SQL -> Parser -> QueryExecutor -> Operators
                         |-> HeapFile -> BufferPool -> DiskManager
                         |-> HashIndex -> RecordID -> HeapFile
```

## 7. Diseño del sistema

- Página de 4 KB.
- Slotted Page.
- RecordID.
- HeapFile.
- Catálogo.
- HashIndex y overflow.
- Clock.
- Mantenimiento de índices.

## 8. Implementación

- C++17 y CMake.
- Persistencia binaria.
- Propiedad con `unique_ptr`.
- Parser y tipos.
- Métricas por consulta.

## 9. Experimentos y resultados

Usar únicamente datos de:

- `results/sprint6_final_results.csv`;
- `results/sprint6_final_summary.csv`;
- `results/sprint6_final_report.md`.

Incluir:

1. IndexScan frente a SeqScan.
2. 3, 10 y 50 frames.
3. Caché fría frente a caliente.
4. Inserción con y sin índice.
5. 1 000, 10 000 y 50 000 registros.

## 10. Discusión

Explicar por qué cambian lecturas, hits, misses, registros examinados y tiempo.
No afirmar que existe un optimizador basado en costos completo.

## 11. Conclusiones

Responder al objetivo y resumir los hallazgos medidos.

## 12. Trabajo futuro

- WAL y recuperación.
- BEGIN/COMMIT/ROLLBACK.
- B+ Tree.
- Optimizador basado en costos.
- Catálogo multipágina.
- Concurrencia.
- Resultados por streaming.

## 13. Referencias

Usar formato consistente y citar fuentes primarias o libros reconocidos.
