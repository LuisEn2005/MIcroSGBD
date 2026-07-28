# Arquitectura del Mini-SGBD

```text
SQL
 |
 v
Tokenizer -> Parser -> QueryExecutor
                         |
                         +-> SeqScan -> HeapFile -> BufferPool -> DiskManager
                         |
                         +-> IndexScan -> HashIndex -> RecordID
                                           |
                                           v
                                       HeapFile
```

## Propiedad de objetos

- `DiskManager` vive más que `BufferPoolManager`.
- `BufferPoolManager` vive más que `CatalogManager` y `QueryExecutor`.
- `CatalogManager` es propietario de los índices mediante `std::unique_ptr<HashIndex>`.
- `SeqScanOperator` e `IndexScanOperator` son propietarios de su `HeapFile`.
- Los operadores hijos se enlazan mediante `std::unique_ptr<AbstractOperator>`.

Esto evita fugas de memoria y punteros colgantes al destruir un plan Volcano.

## Catálogo persistente

La página física `PageId = 0` contiene:

- tablas;
- esquemas;
- primera página de cada `HeapFile`;
- nombre de cada índice;
- tabla y columna indexadas;
- `header_page_id` de cada índice Hash.

Al abrir la base, `CatalogManager(BufferPoolManager*)` recupera esos metadatos y
abre cada `HashIndex` persistente.

## Regla para usar el índice Hash

El planificador utiliza `IndexScanOperator` únicamente cuando existe una
condición de igualdad sobre una columna indexada:

```sql
SELECT * FROM usuarios WHERE id = 100;
```

Los predicados de rango continúan utilizando `SeqScanOperator`.

## Sprint 4: mutaciones consistentes de tablas

Las operaciones de modificación pasan por `TableStorage`:

```text
INSERT / UPDATE / DELETE
          |
          v
     TableStorage
       /      \
      v        v
  HeapFile   HashIndex
      \        /
       v      v
   BufferPoolManager
          |
          v
     DiskManager
```

`TableStorage` valida el esquema, serializa mediante `RecordCodec`, comprueba que
el `RecordID` pertenezca al `HeapFile` de la tabla y mantiene todas las entradas
de índice asociadas.

### Orden de las mutaciones

- `INSERT`: valida y codifica claves, inserta en HeapFile y luego en índices.
  Si un índice falla, elimina las entradas ya insertadas y borra el registro.
- `DELETE`: elimina primero las entradas de índice y después el registro. Si el
  borrado físico falla, restaura las entradas del índice.
- `UPDATE`: lee el registro anterior, calcula claves antiguas y nuevas, conserva
  el mismo RID y modifica únicamente los índices cuyas claves cambiaron. Ante
  un fallo intenta restaurar tanto los índices como el registro anterior.
- Los valores `NULL` no se insertan en el índice Hash.

## Estabilización posterior al Sprint 5

### Resultados de consulta

`QueryExecutor::Execute()` puede recibir un `QueryResult`. Un `SELECT` normal
materializa columnas y valores tipados para que la consola muestre filas reales.
`EXPLAIN ANALYZE` ejecuta el mismo plan, pero muestra las métricas del plan.

### Literales SQL tipados

El parser conserva la clase del literal (`NUMBER`, `STRING`, `IDENTIFIER` o
`NULL`). `ConvertLiteral()` valida el tipo de la columna antes de construir un
`FieldValue`; por ejemplo, un entero escrito como `'10'` ya no se convierte de
forma silenciosa.

### Mutaciones por sentencia

`UPDATE` y `DELETE` primero materializan los `RecordID` candidatos y después
modifican el almacenamiento. Esto evita alterar la cadena de páginas mientras
el operador Volcano todavía la está recorriendo. `rows_returned` se reserva
para `SELECT`; `rows_affected` se usa para `INSERT`, `UPDATE` y `DELETE`.

### Orden de persistencia DDL

Al crear una tabla o índice, sus páginas se escriben antes de publicar los
metadatos en la página de catálogo. Si la creación falla, se eliminan las
páginas reservadas para evitar objetos huérfanos.

### Reutilización de páginas

`DiskManager` reconstruye al abrir el archivo una lista de páginas libres a
partir de páginas completamente vacías. `AllocatePage()` reutiliza primero el
menor `PageId` libre y solo aumenta el archivo cuando no existe una página
reutilizable.

## Sprint 6: cierre y aceptación final

La prueba `sprint6_final_acceptance_tests` valida el sistema integrado con:

- 10 000 registros;
- Buffer Pool de tres frames;
- dos tablas y dos índices;
- inserción, actualización y eliminación;
- transición entre NULL y una clave indexada;
- reutilización de slot y PageId;
- cierre y reapertura;
- equivalencia entre IndexScan y SeqScan;
- ausencia de páginas fijadas al finalizar.

`BufferPoolManager::GetPinnedPageCount()` y
`GetResidentPageCount()` son métodos de introspección destinados a pruebas y
no alteran la política de reemplazo.
