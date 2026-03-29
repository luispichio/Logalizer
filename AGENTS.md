# AGENTS.md — Guía para Agentes de IA

Este archivo define el contexto, las convenciones y las instrucciones operativas para asistentes de IA que colaboren en el desarrollo del proyecto **Logalizer**.

---

## 🧭 ¿Qué es Logalizer?

Logalizer es una aplicación de escritorio para **análisis de logs en formato JSON Lines (JSONL)**. Su filosofía es ser una alternativa simple y accesible al CLI [lnav](https://lnav.org/), priorizando la usabilidad sobre la cantidad de features.

**Criterio de diseño central**: Antes de proponer cualquier feature nueva, preguntarse: *¿aporta valor real al usuario que quiere analizar logs rápidamente, sin configurar nada?*

---

## 🛠️ Stack Tecnológico

| Componente | Tecnología | Notas |
|---|---|---|
| Lenguaje | **C++17** | Sin excepciones, RAII estricto |
| UI Framework | **Qt6 Widgets** | UI programática (sin `.ui` files) |
| Base de Datos | **SQLite FTS5** (`:memory:`) | Tablas por archivo, en RAM |
| Build | **CMake + Ninja** | Vía Qt Creator |

> [!IMPORTANT]
> El stack es **fijo**: C++17 + Qt6 + SQLite en RAM. No proponer migración a otros frameworks (Rust, Tauri, Electron, etc.) sin discusión previa explícita del propietario.

---

## 🗂️ Estructura del Proyecto

```
Logalizer/
├── main.cpp                  # Entry point
├── mainwindow.h/cpp          # Ventana principal, gestión de tabs, open dialog
├── logwidget.h/cpp           # Widget por pestaña/archivo: UI, filtros, vista
├── fileworker.h/cpp          # Hilo de ingesta: schema detection + chunked insert
├── schemadetector.h/cpp      # Detección automática de columnas JSON
├── logdatabase.h/cpp         # Singleton: FTS5, createTable/dropTable/insert/query
├── linerecord.h              # Struct de registro: fields map + raw + filePosition
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
              │     │           → emite schemaReady(fileId, columns)
              │     └── Fase 2: ingesta en chunks de 5.000 líneas
              │                 → llama LogDatabase::insertBatch()
              │                 → emite chunkInserted() → UI refresca parcialmente
              └── LogDatabase (singleton, compartido)
                    ├── createTable(fileId, columns) → CREATE VIRTUAL TABLE fts5(...)
                    ├── insertBatch(fileId, records)
                    ├── queryRows(fileId, filters, fts, offset, limit)
                    ├── searchAll(...) → UNION ALL sobre todas las tablas activas
                    └── dropTable(fileId) → al destruir LogWidget
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
- Usar `QFileInfo`, `QFile`, `QTextStream` para operaciones de archivo — no mezclar con `std::filesystem` sin motivo.
- Logging con `qInfo()`, `qDebug()`, `qWarning()`, `qCritical()` — no `std::cout`.

### Base de Datos
- Cada archivo abierto obtiene un `int fileId` único (contador incremental en `MainWindow`).
- La tabla se llama `logs_{fileId}` (ej. `logs_1`, `logs_2`).
- Columnas mínimas obligatorias en toda tabla: `raw`, `file_position UNINDEXED`, `line_number UNINDEXED`.
- Los campos JSON de tipo **object** o **array** no se indexan como columnas — van solo en `raw`.
- El umbral de presencia por defecto para crear una columna es **80%** (configurable en `SchemaDetector`).

### UI
- Toda la UI se construye programáticamente en C++. **No usar Qt Designer / archivos `.ui`**.
- Cada `LogWidget` es autónomo: cuando se destruye, detiene su `FileWorker` y hace `DROP TABLE`.
- Las señales `chunkInserted` y `progressUpdate` son los ganchos para refrescar la UI durante la carga.

---

## 🔑 Clases Clave — Referencia Rápida

### `SchemaDetector`
- Recibe líneas raw una a una via `feedLine(line)`.
- Límite: `SCAN_LINES = 10000`.
- `detect()` retorna `QVector<ColumnDef>` con los campos que superan el umbral.
- Los tipos detectados son: `String`, `Number`, `Bool`, `Date` (heurística ISO-8601).

### `LogDatabase` (singleton)
- Acceso: `LogDatabase::instance()`.
- Thread-safe en todas sus APIs públicas.
- `createTable()` debe llamarse **antes** de cualquier `insertBatch()` para ese `fileId`.
- `dropTable()` libera la RAM inmediatamente.

### `FileWorker`
- Constructor recibe `fileName` y `fileId`.
- `start()` es el slot del hilo — no llamarlo desde el hilo de UI directamente.
- Emite: `schemaReady`, `progressUpdate`, `chunkInserted`, `finished`, `error`.
- `stop()` setea `m_stopRequested = true` (el worker verifica en el loop).

### `LogWidget`
- Constructor recibe `filePath` y `fileId`.
- Crea y posee el `FileWorker` y su `QThread`.
- `refreshData()` hace la query paginada y actualiza el `QStandardItemModel` a través de un `QSortFilterProxyModel`.
- **Debounce**: `onChunkInserted` reinicia un `QTimer` (1.5s single-shot). `refreshData()` solo se llama al finalizar el timer, no en cada chunk. Esto evita bloquear el hilo de UI durante la ingesta.
- **Filtros**: Panel de filas dinámicas `FilterRow` (Logic + Column + Operator + Value + Remove). Ilimitadas por columna. Se agrega con `addFilterRow()`, se elimina con `removeFilterRow()`.
- **Visibilidad de columnas**: Clic derecho en el header abre un menú para mostrar/ocultar columnas. El estado persiste en `m_columnVisibility`. La columna `raw` está oculta por defecto cuando hay columnas dinámicas.
- **Ordenamiento**: `QSortFilterProxyModel` con `setSortingEnabled(true)` en el `QTableView`.
- **Copia**: Ctrl+C copia las celdas seleccionadas al clipboard (tab-separated, newline por fila).
- **Cell-to-filter**: Clic en una celda pre-rellena un filtro para esa columna y valor.
- **Vista por defecto**: `text` si el archivo no tiene columnas dinámicas (no JSONL); `table` si las tiene.
- **Scroll/paginación**: `m_currentPage * PAGE_SIZE` (PAGE_SIZE = 10000 filas).

---

## ✅ Instrucciones para la IA

1. **Leer `Logalizer.md` primero**: Es la fuente de verdad del proyecto. Siempre verificar si una feature está especificada ahí antes de diseñar una implementación.

2. **Verificar el stack antes de proponer**: Si una solución require agregar una dependencia externa (librería third-party, cambio de ORM, etc.), plantearlo explícitamente y esperar aprobación.

3. **No romper la arquitectura de threading**: Cualquier código que toque `LogDatabase` desde un hilo secundario debe usar los métodos públicos thread-safe del singleton. No crear conexiones SQLite adicionales.

4. **UI programática siempre**: No generar código que cree archivos `.ui` o use `Qt Designer`.

5. **CMakeLists.txt**: Siempre actualizar `CMakeLists.txt` si se añaden o eliminan archivos fuente.

6. **Tests unitarios**: Los parsers (`SchemaDetector`) y la lógica de DB (`LogDatabase`) son candidatos prioritarios para tests. Usar `QtTest` como framework de testing.

7. **Compilación y verificación**: El proyecto se compila con Qt Creator (CMake + Ninja). Si el entorno en que corre el agente no tiene `cmake` en el PATH, documentar los cambios y avisar al usuario para que compile en Qt Creator.

8. **Idioma**: La documentación interna (`Logalizer.md`, `AGENTS.md`, comentarios de código relevantes) puede estar en español. El código fuente (nombres de clases, métodos, variables) debe estar en **inglés**.

---

## 🚫 Anti-Patrones a Evitar

| ❌ No hacer | ✅ Alternativa |
|---|---|
| Bloquear el hilo de UI con lecturas de archivo | Usar `QThread` + `FileWorker` |
| Cargar todo el archivo en un `QString` / `QByteArray` | Leer línea a línea con `QTextStream` |
| Abrir conexiones SQLite adicionales fuera de `LogDatabase` | Usar el singleton `LogDatabase::instance()` |
| Crear columnas FTS5 para campos de tipo object/array | Solo indexar primitivos; el resto va en `raw` |
| Recrear archivos `.ui` eliminados | UI siempre programática |
| Agregar `std::cout` o `printf` | Usar `qInfo()` / `qDebug()` |
| Hardcodear rutas de archivo | Usar `QFileDialog` y configuración persistente |

---

## 📌 Features Pendientes (Roadmap)

Ver tabla completa en [`README.md`](./README.md#-roadmap-y-funcionalidades-deseadas-todo). Las prioritarias para la próxima iteración son:

1. **Resaltado de búsqueda** en `QTextBrowser` con FTS5 snippets.
2. **Tests unitarios** (`QtTest`) para `SchemaDetector` y `LogDatabase`.
3. **Soporte de archivos comprimidos** (`.gz`, `.zip`) vía descompresión en memoria antes de ingestar.
4. **Apertura de múltiples archivos en una misma tabla** (merge de schemas).
