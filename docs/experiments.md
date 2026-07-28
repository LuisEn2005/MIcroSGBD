# Metodología Experimental y Resultados de Rendimiento (Sprint 4)

Este documento detalla la metodología cuantitativa y los experimentos realizados para evaluar la eficiencia del Mini-SGBD persistente bajo variaciones en el tamaño del Buffer Pool, estado de la caché (fría vs caliente) y tipos de acceso (búsqueda secuencial `SeqScan` vs búsqueda indexada `IndexScan`).

## 1. Diseño Experimental

### Factores Evaluados
1. **Plan de Ejecución**:
   - `HashIndex` (`IndexScan`): Búsqueda por igualdad sobre clave indexada (`id = X`).
   - `SeqScan`: Búsqueda secuencial sobre columna no indexada o consulta de rango (`age > 40`).
2. **Capacidad del Buffer Pool**:
   - `3 frames`: Forzar reemplazos constantes mediante el algoritmo Clock.
   - `10 frames`: Configuración equilibrada.
   - `50 frames`: Capacidad amplia para retener páginas en caché caliente.
3. **Estado de Caché**:
   - **Caché Fría (`cold`)**: Reconstrucción limpia del `BufferPoolManager` y `DiskManager` antes de ejecutar la consulta.
   - **Caché Caliente (`hot`)**: Ejecución consecutiva de la consulta sobre páginas previamente cargadas en el Buffer Pool.

---

## 2. Resultados Obtenidos (Dataset: 10,000 Registros)

Los experimentos fueron ejecutados utilizando la suite automatizada `tests/sprint4_metrics_tests.cpp`, registrando los datos en `results/sprint4_benchmark_results.csv`.

### Comparación Cuantitativa Resumida (Buffer Pool = 3 Frames)

| Estrategia | Plan | Registros Examinados | Lecturas de Disco | Buffer Hits | Buffer Misses | Tiempo Promedio (ms) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Búsqueda por Índice** | `IndexScan` | **1** | **4** | 2 | 4 | **0.16 ms** |
| **Escaneo Secuencial** | `SeqScan` | **10,000** | **147** | 0 | 147 | **3.85 ms** |

---

## 3. Conclusiones Clave

1. **Reducción de I/O por Índice**: `IndexScan` examina solo 1 registro y realiza 4 lecturas físicas frente a las 147 lecturas y 10,000 registros examinados de `SeqScan`.
2. **Comportamiento del Buffer Pool**: En ejecuciones en **Caché Caliente (`hot`)**, las búsquedas por índice registran **0 misses de disco** adicionales y un incremento directo de los `buffer_hits`.
3. **Aislabilidad de Métricas**: Las métricas capturadas pertenecen exclusivamente al delta de la consulta en ejecución (`PopulateDeltaStats`), sin contaminarse con totales acumulados del sistema.
