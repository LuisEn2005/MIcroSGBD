# Formato de Registros / Tuplas (`record_format.md`)

Este documento especifica cómo se codifican en memoria y disco los datos de una fila o tupla individual dentro del mini-SGBD.

---

## 1. Disposición del Registro en Memoria/Disco

Un registro consta de un **Header de Registro** seguido de un arreglo de valores serializados de columnas.

```text
+---------------------------------------------------------------------------------------+
|                          HEADER DEL REGISTRO                                          |
|  [ NumColumns (2B) | NullBitmap (Ceil(NumCols/8) B) | ColumnOffsets (NumCols * 2B) ]   |
+---------------------------------------------------------------------------------------+
|                          CUERPO DE DATOS (PAYLOAD)                                    |
|  [ Col 0 (Fix/Var) ] [ Col 1 (Fix/Var) ] ... [ Col N (Fix/Var) ]                      |
+---------------------------------------------------------------------------------------+
```

---

## 2. Detalle de Campos del Registro

### A. Encabezado (Header)
1. **NumColumns (`uint16_t` - 2 bytes):** Cantidad de columnas almacenadas en la tupla.
2. **Bitmap de Nulos (`NullBitmap`):** Un mapa de bits donde cada bit representa si una columna es `NULL` (`1`) o tiene valor (`0`).
   * *Tamaño:* $\lceil \text{NumColumns} / 8 \rceil$ bytes.
3. **Arreglo de Offsets (`ColumnOffsets`):** Para permitir acceso en tiempo constante $\mathcal{O}(1)$ a cualquier columna sin escanear bytes previos.
   * *Tamaño:* $\text{NumColumns} \times 2\text{ bytes}$ (`uint16_t`). Cada entrada indica la posición relativa (offset en bytes) donde inicia el dato de la columna respectiva dentro del payload.

### B. Serialización de Tipos de Datos (Payload)
Los datos se almacenan de manera secuencial y continua en formato Little-Endian:

| Tipo (`TypeId`) | Representación en Bytes | Tamaño |
| :--- | :--- | :--- |
| `INTEGER` | `int32_t` | 4 bytes fijos |
| `BOOLEAN` | `uint8_t` | 1 byte (0 = false, 1 = true) |
| `VARCHAR` | Arreglo de caracteres (`char[]`) | Variable (sin marcador `\0` final, su longitud se calcula con los Offsets). |

---

## 3. Ejemplo Ilustrativo

Dada la tabla `Student(id INT, name VARCHAR, active BOOL)` con los datos `(101, "Alice", true)`:

```text
Header:
- NumColumns: 3
- NullBitmap: 0b00000000 (Ningún campo es NULL)
- ColumnOffsets: [0, 4, 9]

Payload:
- Offset 0..3 : [ 101 ]             (INTEGER, 4B)
- Offset 4..8 : [ 'A','l','i','c','e' ] (VARCHAR, 5B)
- Offset 9    : [ 1 ]               (BOOLEAN, 1B)

Tamaño total en disco: 2 (Cols) + 1 (NullBytes) + 6 (Offsets) + 10 (Payload) = 19 bytes.
```
