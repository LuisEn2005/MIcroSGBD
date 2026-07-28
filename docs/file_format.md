# Formato de Archivos en Disco (`file_format.md`)

El almacenamiento del mini-SGBD utiliza un esquema de **Páginas de Tamaño Fijo** de 4096 bytes ($4\,\text{KB}$). Todo el archivo de base de datos (`.db`) se divide en páginas contiguas indexadas por un `PageId` (comenzando en 0).

---

## 1. Disposición General del Archivo (`.db`)

```text
+-------------------+-------------------+-------------------+---
|   Header Page     |      Data Page    |      Data Page    |  ...
|   (PageId = 0)    |    (PageId = 1)   |    (PageId = 2)   |  
+-------------------+-------------------+-------------------+---
```

* **Header Page (PageId 0):** Almacena metadatos globales del archivo de la base de datos (p. ej., cantidad de páginas asignadas, puntero a lista de páginas libres).
* **Data Pages (PageId $\ge$ 1):** Páginas organizadas mediante el formato **Slotted Page** para almacenar registros de longitud variable.

---

## 2. Formato de Slotted Page

Cada página de datos de $4096\text{ bytes}$ utiliza una arquitectura slotted-page dividida en tres secciones: **Header**, **Slot Directory** (crece hacia abajo) y **Record Storage** (crece hacia arriba).

```text
+-----------------------------------------------------------------------+
|  Page Header (16 bytes)                                               |
|  [ PageID (4B) | SlotCount (2B) | FreeSpacePointer (2B) | NextPageID (4B) | LSN (4B) ]  |
+-----------------------------------------------------------------------+
|  Slot Directory                                                       |
|  [ Slot 0: Offset(2B), Length(2B) ]                                   |
|  [ Slot 1: Offset(2B), Length(2B) ]                                   |
|  [ Slot 2: Offset(2B), Length(2B) ] -> ...                            |
+-----------------------------------------------------------------------+
|                                                                       |
|                          ESPACIO LIBRE                                |
|                                                                       |
+-----------------------------------------------------------------------+
|                                   ... <- [ Registro 2 ]               |
|                                   ... <- [ Registro 1 ]               |
|                                   ... <- [ Registro 0 ]               |
+-----------------------------------------------------------------------+
```

### Encabezado de Página (16 Bytes)
| Campo | Tipo | Tamaño | Descripción |
| :--- | :--- | :--- | :--- |
| `page_id` | `int32_t` | 4 bytes | Identificador único de la página en disco. |
| `slot_count` | `uint16_t` | 2 bytes | Número de slots (activos o eliminados) en el directorio. |
| `free_space_pointer` | `uint16_t` | 2 bytes | Offset desde el inicio de la página donde empieza el espacio libre para registros. Inicialmente 4096. |
| `next_page_id` | `int32_t` | 4 bytes | Siguiente página del mismo HeapFile. Vale `INVALID_PAGE_ID` cuando no existe otra página. |
| `lsn` | `uint32_t` | 4 bytes | Log Sequence Number (reservado para recuperación/concurrencia). |

### Directorio de Slots (Slot Entry - 4 Bytes cada uno)
| Campo | Tipo | Tamaño | Descripción |
| :--- | :--- | :--- | :--- |
| `offset` | `uint16_t` | 2 bytes | Posición en bytes donde inicia el registro dentro de la página. `0` si el slot está borrado. |
| `length` | `uint16_t` | 2 bytes | Longitud en bytes del registro. `0` si el slot está vació o borrado. |

---

## 3. Direccionamiento mediante RecordID (RID)

Cada registro en la base de datos es identificado globalmente de manera única por una estructura `RecordID`:

```cpp
struct RecordID {
    PageId page_id;  // Número de página física en el archivo
    SlotId slot_id;  // Índice en el Directorio de Slots de dicha página
};
```



---

## 4. Formato del índice Hash persistente

El índice Hash utiliza páginas del mismo archivo binario y todas sus lecturas y
escrituras pasan por el `BufferPoolManager`.

### Header Page del índice

```text
+------------------------------------------------------------------+
| Magic (4B) | Version (2B) | BucketCount (2B) | MaxKeyLen (2B)    |
| Reserved (6B)                                                   |
+------------------------------------------------------------------+
| Directory: PageId bucket[0], bucket[1], ...                     |
+------------------------------------------------------------------+
```

Los buckets se asignan de forma perezosa. Una entrada del directorio vale
`INVALID_PAGE_ID` hasta que se inserta la primera clave que corresponde a ese
bucket.

### Bucket Page y Overflow Page

```text
+------------------------------------------------------------------+
| Magic (4B) | EntryCount (2B) | Capacity (2B)                    |
| OverflowPageId (4B) | Reserved (4B)                              |
+------------------------------------------------------------------+
| Entry 0 | Entry 1 | ... | Entry N                               |
+------------------------------------------------------------------+
```

Cada entrada contiene una clave de hasta 64 bytes y un `RecordID`. Las páginas
de desbordamiento utilizan el mismo formato físico que las páginas bucket y se
enlazan mediante `OverflowPageId`.

La función Hash persistente es FNV-1a de 64 bits. No se utiliza
`std::hash<std::string>` porque su representación persistente no forma parte del
contrato del estándar de C++.
