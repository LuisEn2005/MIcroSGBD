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
