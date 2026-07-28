# Guía de defensa técnica

## Almacenamiento

- Una página mide 4096 bytes y el archivo es una secuencia de páginas.
- La Slotted Page separa directorio de slots y área de registros.
- El RID combina `page_id` y `slot_id`.
- La compactación mueve bytes, pero conserva slots y RIDs.
- El HeapFile enlaza páginas mediante `next_page_id`.

## Buffer Pool

- Un hit no realiza lectura física.
- Un miss requiere cargar una página.
- Una página con `pin_count > 0` no puede ser expulsada.
- Clock entrega segunda oportunidad mediante un bit de referencia.
- Las páginas sucias se escriben antes de reemplazarse.

## Índice Hash

- FNV-1a produce un hash persistente estable.
- La igualdad puede usar HashIndex; los rangos requieren SeqScan.
- Las colisiones se resuelven en buckets y páginas overflow.
- El índice guarda `key -> RecordID`, no la fila completa.

## Consultas

- Tokenizer produce tokens.
- Parser construye el AST.
- QueryExecutor crea un plan Volcano.
- Cada operador implementa `Open`, `Next` y `Close`.
- Projection reconstruye el registro de salida.

## Métricas

- `pages_scanned` no es igual a `disk_reads`.
- Caché fría exige reconstruir BufferPool/DiskManager.
- Reiniciar contadores no enfría la caché.
- La mediana reduce el efecto de valores atípicos.

## Limitaciones

- No existe atomicidad ante una caída abrupta.
- No hay WAL ni transacciones.
- El catálogo ocupa una página.
- No existe B+ Tree.
- No hay optimizador de costos completo.
