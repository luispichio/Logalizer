# AGENTS.md — Guía para Agentes de IA

Este archivo define el contexto, las convenciones y las instrucciones operativas para asistentes de IA que colaboren en el desarrollo del proyecto **Logalizer**.

---

## 🧭 ¿Qué es Logalizer?

Logalizer es una aplicación de escritorio para **análisis de logs en formato JSON Lines (JSONL)**. Su filosofía es ser una alternativa simple y accesible al CLI [lnav](https://lnav.org/), priorizando la usabilidad sobre la cantidad de features.

**Criterio de diseño central**: Antes de proponer cualquier feature nueva, preguntarse: *¿aporta valor real al usuario que quiere analizar logs rápidamente, sin configurar nada?*

**Repositorio**: https://github.com/luispichio/Logalizer

---

## 🛠️ Stack Tecnológico

| Componente | Tecnología | Notas |
|---|---|---|
| Lenguaje | **C++17** | Sin excepciones, RAII estricto |
| UI Framework | **Qt6 Widgets** | UI programática (sin `.ui` files) |
| Base de Datos | **SQLite híbrido** (`:memory:`) | meta (B-tree) + FTS5 por archivo |
| Build | **CMake + Ninja** | Vía Qt Creator |

> [!IMPORTANT]
> El stack es **fijo**: C++17 + Qt6 + SQLite en RAM. No proponer migración a otros frameworks (Rust, Tauri, Electron, etc.) sin discusión previa explícita del propietario.

---

## 🗂️ Estructura del Proyecto

```
Logalizer/
├── main.cpp                  # Entry point
├── mainwindow.h/cpp          # Ventana principal, gestión de tabs, open dialog, About
├── logwidget.h/cpp           # Widget por pestaña/archivo: UI, filtros, paginación, vista
├── fileworker.h/cpp          # Hilo de ingesta: schema detection + chunked insert
├── schemadetector.h/cpp      # Detección de columnas + sanitizedName
├── logdatabase.h/cpp         # Singleton: esquema híbrido meta+FTS5, thread-safe
├── linerecord.h              # Struct LineRecord: fields (sanitizedName→val) + raw + pos
├── CMakeLists.txt            # Build config
├── Logalizer.md              # Fuente de verdad del proyecto (LEER SIEMPRE)
├── README.md                 # Descripción pública (GitHub)
└── AGENTS.md                 # Este archivo
```

> [!NOTE]
> Los archivos `safequeue.*`, `logdb.*`, `mainwindow.ui` y `logwidget.ui` fueron eliminados durante la refactorización. Si aparecen en el historial de git, no reinstanciarlos.

---

## 🏛️ Arquitectura y Flujo de Datos

```
MainWindow
  └── QTabWidget
        └── LogWidget (1 por archivo abierto)
              ├── FileWorker (QThread)
              │     ├── Fase 1: SchemaDetector (escanea hasta 10.000 líneas)
              │     │           → sanitiza nombres → emite schemaReady(fileId, columns)
              │     └── Fase 2: ingesta en chunks de 5.000 líneas
              │                 → parseLine() usa sanitizedName como key en fields
              │                 → LogDatabase::insertBatch() → meta + FTS5
              │                 → emite chunkInserted() → debounce timer 1.5s
              └── LogDatabase (singleton, compartido)
                    ├── createTable(fileId, columns)
                    │     → CREATE TABLE logs_meta_{id} (line_number PK, ...)
                    │     → CREATE INDEX ON logs_meta_{id}(Number/Date cols)
                    │     └── CREATE VIRTUAL TABLE logs_fts_{id} USING fts5(raw, Strings...)
                    ├── insertBatch(fileId, records) → transacción sobre ambas tablas
                    ├── queryRows(fileId, offset, limit, filters, fts)
                    │     → si fts: JOIN meta+fts ON fts.rowid=meta.line_number
                    │     → filtros de columna: WHERE m.col ... (usa B-tree)
                    ├── searchAll(...) → UNION ALL sobre las tablas meta activas
                    └── dropTable(fileId) → DROP TABLE meta + DROP TABLE fts
```

**Reglas de threading**:
- `LogDatabase` es accedido desde múltiples hilos. Toda operación pública usa `QMutexLocker`.
- `FileWorker` corre en un `QThread` propio y se comunica con `LogWidget` exclusivamente vía señales Qt (cross-thread).
- El hilo de UI nunca espera ni bloquea en operaciones de disco o DB.

---

## 📐 Convenciones de Código

### General
- **Rendimiento es prioridad #1**: nunca bloquear el hilo de UI.
- Preferir `QString` para texto en Qt, `std::string` solo cuando la API lo exija.
- Usar `QFileInfo`, `QFile`, `QTextStream` para operaciones de archivo.
- Logging con `qInfo()`, `qDebug()`, `qWarning()`, `qCritical()` — no `std::cout`.

### Base de Datos — Esquema Híbrido
- Cada archivo abierto obtiene un `int fileId` único (contador incremental en `MainWindow`).
- Se crean **dos tablas** por archivo:
  - `logs_meta_{fileId}`: tabla regular SQLite con `line_number INTEGER PRIMARY KEY` (= rowid alias), `file_position`, `raw`, y todas las columnas dinámicas con sus tipos nativos (`REAL` para Number, `TEXT` para String/Date, `INTEGER` para Bool).
  - `logs_fts_{fileId}`: FTS5 virtual table con `raw` + columnas `String` y `Date`. Insertadas con `rowid = line_number`.
- **Índices B-tree** creados automáticamente en meta para columnas `Number`, `Date` y `file_position`.
- **JOIN** en las consultas: `logs_fts_{id} f INNER JOIN logs_meta_{id} m ON f.rowid = m.line_number`.
- Los campos JSON de tipo **object** o **array** no se indexan como columnas — van solo en `raw`.
- El umbral de presencia por defecto para crear una columna es **80%** (configurable en `SchemaDetector`).

### Saneamiento de Nombres de Columnas
- `ColumnDef` tiene dos campos: `name` (original JSON) y `sanitizedName` (SQLite-safe).
- `ColumnDef::sanitize()`: reemplaza todo carácter que no sea `[a-zA-Z0-9_]` por `_`, colapsa dobles `__`, prefija `col_` si empieza en dígito.
- Ejemplos: `@timestamp` → `_timestamp`, `service.name` → `service_name`, `@msg` → `_msg`.
- `SchemaDetector::detect()` deduplica `sanitizedName` añadiendo sufijo numérico si hay colisión.
- **Regla crítica**: `record.fields` usa **`sanitizedName` como clave** (no el `name` original). `LogDatabase` resuelve filter.column (original) → sanitizedName al generar SQL.

### UI
- Toda la UI se construye programáticamente en C++. **No usar Qt Designer / archivos `.ui`**.
- Cada `LogWidget` es autónomo: cuando se destruye, detiene su `FileWorker` y hace `DROP TABLE` en ambas tablas.
- Señales relevantes: `chunkInserted` → debounce timer; `finished` → refresh final.

---

## 🔑 Clases Clave — Referencia Rápida

### `ColumnDef` (en schemadetector.h)
- `name`: nombre original del campo JSON (para UI/headers).
- `sanitizedName`: nombre SQLite-safe (para SQL). Generado por `ColumnDef::sanitize()`.
- `type`: `String | Number | Bool | Date`.
- Regla de tabla: `Number`/`Bool`/`Date` → solo meta (con índice para Number/Date). `String`/`Date` → también en FTS5.

### `SchemaDetector`
- Recibe líneas raw una a una via `feedLine(line)`.
- Límite: `SCAN_LINES = 10000`.
- `detect()` retorna `QVector<ColumnDef>` con los campos que superan el umbral (80%), con `sanitizedName` asignado y deduplicado.
- Tipos detectados: `String`, `Number`, `Bool`, `Date` (heurística ISO-8601).

### `LogDatabase` (singleton)
- Acceso: `LogDatabase::instance()`.
- Thread-safe en todas sus APIs públicas.
- `createTable()` crea `logs_meta_{id}` + `logs_fts_{id}` + índices. Debe llamarse **antes** de `insertBatch()`.
- `insertBatch()`: inserta en ambas tablas en la misma transacción. Usa `col.sanitizedName` para leer de `record.fields`.
- `dropTable()`: hace `DROP TABLE` en ambas tablas, liberando la RAM inmediatamente.
- `queryRows(fileId, offset, limit, filters, ftsQuery, ...)`:
  - Si `ftsQuery` no está vacío → `INNER JOIN logs_fts_{id} ON rowid=line_number WHERE fts MATCH ?`.
  - Filtros de columna → `WHERE m.sanitizedCol LIKE/=/>/< ?` en la tabla meta.
  - Retorna `outHeaders` con nombres **originales** (`col.name`) para mostrar en UI.
- `sanitizedName(fileId, originalName)`: resuelve nombre original → sanitizedName (método privado).

### `FileWorker`
- Constructor recibe `fileName` y `fileId`.
- `start()` es el slot del hilo — no llamarlo desde el hilo de UI directamente.
- Emite: `schemaReady`, `progressUpdate`, `chunkInserted`, `finished`, `error`.
- `stop()` setea `m_stopRequested = true` (el worker verifica en el loop).
- `parseLine()` usa **`col.sanitizedName`** como clave en `record.fields`.

### `LogWidget`
- Constructor recibe `filePath` y `fileId`.
- Crea y posee el `FileWorker` y su `QThread`.
- **Debounce**: `onChunkInserted` reinicia un `QTimer` (1.5s single-shot). `refreshData()` se llama solo al dispararse el timer. `onFinished` lo cancela y hace el refresh final.
- **Paginación**: `QSpinBox* m_offsetSpin` (offset de inicio) y `QSpinBox* m_limitSpin` (filas por página, default 1000). Al aplicar filtros → offset se resetea a 0.
- **Scroll automático**: `verticalScrollBar::valueChanged` en `QTableView` y `QTextBrowser` → si llega al máximo y `offset + limit < totalCount`, incrementa offset y llama `refreshData()`.
- **Filtros**: Panel de filas dinámicas `FilterRow` (Logic + Column + Operator + Value + Remove). Ilimitadas. `addFilterRow()` / `removeFilterRow()`. La columna en el filter es el nombre **original** (`col.name`).
- **Visibilidad de columnas**: clic derecho en header → `QMenu` con checkboxes. Estado persiste en `m_columnVisibility`. La columna `raw` está oculta por defecto cuando hay columnas dinámicas.
- **Ordenamiento**: `QSortFilterProxyModel` con `setSortingEnabled(true)`.
- **Copia**: Ctrl+C → clipboard tab-separated, newline por fila.
- **Cell-to-filter**: clic en celda → busca FilterRow vacío para esa columna o crea uno nuevo.
- **Vista por defecto**: `text` si no hay columnas dinámicas; `table` si las hay.
- **Status bar**: `Size | Lines | Offset [spin] | Rows [spin] | ... | Rows X–Y of Z | [progress]`.

---

## ✅ Instrucciones para la IA

1. **Leer `Logalizer.md` primero**: Es la fuente de verdad del proyecto. Siempre verificar si una feature está especificada ahí antes de diseñar una implementación.

2. **Verificar el stack antes de proponer**: Si una solución require agregar una dependencia externa, plantearlo explícitamente y esperar aprobación.

3. **No romper la arquitectura de threading**: Cualquier código que toque `LogDatabase` desde un hilo secundario debe usar los métodos públicos thread-safe del singleton. No crear conexiones SQLite adicionales.

4. **UI programática siempre**: No generar código que cree archivos `.ui` o use Qt Designer.

5. **CMakeLists.txt**: Siempre actualizar `CMakeLists.txt` si se añaden o eliminan archivos fuente.

6. **Coherencia `name` vs `sanitizedName`**:
   - SQL siempre usa `sanitizedName`.
   - UI (headers, filtros, menús) siempre muestra `name` original.
   - `record.fields` usa `sanitizedName` como clave.
   - No mezclar: si se cambia dónde se usa uno, actualizar todos los puntos de contacto.

7. **Tests unitarios**: `SchemaDetector` y `LogDatabase` son candidatos prioritarios. Usar `QtTest`.

8. **Compilación**: El proyecto se compila con Qt Creator (CMake + Ninja). Si el agente no tiene `cmake` en el PATH, documentar los cambios y avisar al usuario.

9. **Idioma**: Documentación interna en español es aceptable. El código fuente (clases, métodos, variables) debe estar en **inglés**.

---

## 🚫 Anti-Patrones a Evitar

| ❌ No hacer | ✅ Alternativa |
|---|---|
| Bloquear el hilo de UI con lecturas de archivo | Usar `QThread` + `FileWorker` |
| Cargar todo el archivo en un `QString` / `QByteArray` | Leer línea a línea con `QTextStream` |
| Abrir conexiones SQLite adicionales fuera de `LogDatabase` | Usar el singleton `LogDatabase::instance()` |
| Crear columnas FTS5 para campos de tipo object/array/Number/Bool | Solo String y Date van en FTS5; el resto, solo en meta |
| Usar `col.name` como clave en `record.fields` | Usar `col.sanitizedName` |
| Recrear archivos `.ui` eliminados | UI siempre programática |
| Agregar `std::cout` o `printf` | Usar `qInfo()` / `qDebug()` |
| Hardcodear rutas de archivo | Usar `QFileDialog` y configuración persistente |
| Llamar `refreshData()` directamente en `onChunkInserted` | Usar el debounce timer de 1.5s |

---

## 📌 Features Pendientes (Roadmap)

Ver tabla completa en [`README.md`](./README.md#-roadmap-y-funcionalidades-deseadas-todo). Las prioritarias para la próxima iteración son:

1. **Resaltado de búsqueda** en `QTextBrowser` con FTS5 `snippet()`.
2. **Tests unitarios** (`QtTest`) para `SchemaDetector` (sanitize, detect, dedup) y `LogDatabase` (híbrido, filtros, JOIN).
3. **Ordenamiento DB-side**: pasar `ORDER BY col ASC/DESC` a `queryRows` al hacer clic en el header (en lugar del sort in-memory actual).
4. **Soporte de archivos comprimidos** (`.gz`, `.zip`) — descompresión en memoria antes de ingestar.
5. **Apertura de múltiples archivos en una misma tabla** (merge de schemas).
