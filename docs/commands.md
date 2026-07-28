# Comandos soportados

## CREATE TABLE

```sql
CREATE TABLE users (
    id INT,
    name VARCHAR(30),
    score INT,
    active BOOLEAN
);
```

Los nombres de columna deben ser únicos. Los tipos soportados son `INT`,
`INTEGER`, `CHAR(n)`, `VARCHAR(n)`, `BOOL` y `BOOLEAN`.

## INSERT

```sql
INSERT INTO users VALUES (-5, 'O''Brien', NULL, true);
```

- Los `VARCHAR` deben escribirse entre comillas.
- Una comilla dentro de una cadena se escapa duplicándola: `'O''Brien'`.
- Los enteros negativos están soportados.
- `NULL` puede insertarse o asignarse durante un `UPDATE`.

## SELECT

```sql
SELECT id, name
FROM users
WHERE id >= -5 AND active = true;
```

La consola imprime las columnas y filas obtenidas. Los predicados conectados
mediante `AND` se ejecutan como una cadena de operadores `Filter`.

## CREATE INDEX

```sql
CREATE INDEX idx_users_id ON users(id);
```

Un índice Hash se utiliza únicamente para igualdad sobre la columna indexada.
Las consultas de rango siguen usando `SeqScan`.

## UPDATE

```sql
UPDATE users
SET score = 20
WHERE id = -5 AND active = true;
```

La consola informa la cantidad de filas afectadas. Las entradas de todos los
índices se mantienen mediante `TableStorage`.

## DELETE

```sql
DELETE FROM users
WHERE id = -5;
```

## EXPLAIN ANALYZE

```sql
EXPLAIN ANALYZE
SELECT * FROM users WHERE id = -5;
```

`EXPLAIN` requiere la palabra `ANALYZE`, porque el sistema ejecuta la consulta
y reporta métricas reales:

```text
Plan: IndexScan o SeqScan
Execution time
Disk reads / writes
Buffer hits / misses
Pages scanned
Records examined
Rows returned
```

## Benchmarks

```bash
./scripts/run_benchmarks.sh
```

El script crea un build `Release`, ejecuta la aceptación de estabilización y
genera `results/sprint5_experiments_summary.csv`.
