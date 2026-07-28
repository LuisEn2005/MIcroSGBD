# Guía de Comandos y EXPLAIN ANALYZE

El Mini-SGBD soporta la ejecución interactiva de sentencias SQL y la inspección cuantitativa del plan de ejecución mediante `EXPLAIN ANALYZE`.

## Comandos Soportados

### 1. Definición de Tablas (`CREATE TABLE`)
```sql
CREATE TABLE users (id INT, name CHAR(30), age INT, active BOOLEAN);
```

### 2. Creación de Índices Persistentes (`CREATE INDEX`)
```sql
CREATE INDEX idx_users_id ON users(id);
```

### 3. Inserción de Registros (`INSERT`)
```sql
INSERT INTO users VALUES (1, 'Ana', 25, true);
```

### 4. Consultas con Métricas (`SELECT` / `EXPLAIN ANALYZE`)
```sql
EXPLAIN ANALYZE SELECT * FROM users WHERE id = 1;
```

#### Salida Formateada de `EXPLAIN ANALYZE`:
```text
-> Plan: IndexScan
   Execution time: 0.162 ms
   Disk reads: 4
   Disk writes: 0
   Buffer hits: 2
   Buffer misses: 4
   Pages scanned: 0
   Records examined: 1
   Rows returned: 1
```

## Ejecución de Benchmarks Automatizados
```bash
./scripts/run_benchmarks.sh
```
Exporta la matriz completa de resultados a `results/sprint4_benchmark_results.csv`.
