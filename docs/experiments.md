# Metodología experimental del Sprint 5

## Objetivo

Comparar `IndexScan` y `SeqScan`, el efecto del tamaño del Buffer Pool y la
sobrecarga de mantener un índice durante inserciones.

## Configuración

- Volúmenes: 1 000 y 10 000 registros.
- Buffer Pool: 3, 10 y 50 frames.
- Diez repeticiones por escenario.
- Consultas frías: se reconstruyen `DiskManager`, `ClockReplacer` y
  `BufferPoolManager` antes de cada ejecución.
- Consultas calientes: una ejecución de calentamiento seguida de repeticiones
  sobre el mismo Buffer Pool.

## Comparaciones

```sql
-- Igualdad indexada
SELECT * FROM users WHERE id = 500;

-- Igualdad no indexada
SELECT * FROM users WHERE name = 'User_500';
```

Se registran media, mediana, mínimo, máximo, desviación estándar, lecturas,
escrituras, hits, misses, páginas y registros examinados.

## Inserción con y sin índice

Para la variante indexada se crea primero un índice vacío y luego cada
`INSERT` pasa por `TableStorage`. De esta forma el tiempo medido corresponde al
costo real de mantener el índice durante la carga; no a construir el índice al
final sobre una tabla ya llena.

## Ejecución

```bash
./scripts/run_benchmarks.sh
```

Resultado:

```text
results/sprint5_experiments_summary.csv
```
