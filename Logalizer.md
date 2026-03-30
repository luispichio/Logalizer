# Definición del Proyecto: Logalizer

Este archivo `Logalizer.md` sirve como la fuente de verdad para el contexto del proyecto. Debe ser actualizado a medida que el proyecto evoluciona para mantener al equipo y a los asistentes de IA alineados.

## 1. Visión General y Objetivos

- **Descripción**: Aplicación de Escritorio para análisis de logs tipo "json lines" de alto rendimiento.
- **Objetivo Principal**: Crear una herramienta capaz de procesar y visualizar grandes volúmenes de logs eficientemente.
- **Plataforma**: Desktop (Linux, Windows).
- **Repositorio**: https://github.com/luispichio/Logalizer

## 2. Características Funcionales

### 2.1 Procesamiento de Archivos

- Multithreaded: lectura e ingesta en hilos de background (`FileWorker` en `QThread`).
- Capacidad de abrir múltiples archivos de logs concurrentemente (un tab por archivo).
- **Detección automática de formato**: escaneo de hasta 10.000 líneas para detectar el esquema JSONL.
  - Cada campo JSON presente en el ≥80% de las líneas se convierte en una columna indexada.
  - Campos de tipo objeto o array no se indexan (van solo en `raw`).
  - **Saneamiento de nombres**: los caracteres especiales (`@`, `.`, `-`, espacios, etc.) en los nombres de campo se reemplazan por `_` para generar un `sanitizedName` válido para SQLite. Las colisiones tras saneamiento se numeran (`_foo_2`, `_foo_3`, …).
  - Columnas mínimas obligatorias: `raw`, `file_position`, `line_number`.

### 2.2 Base de Datos — Esquema Híbrido

Por cada archivo abierto se crean **dos tablas** en la DB in-memory (`:memory:`):

| Tabla | Tipo SQLite | Contenido | Propósito |
|---|---|---|---|
| `logs_meta_{id}` | Tabla regular | `line_number` (PK/rowid), `file_position`, `raw`, todas las columnas dinámicas | Filtros numéricos/fecha con índice B-tree; JOIN con FTS |
| `logs_fts_{id}` | Virtual FTS5 | `raw` + columnas String/Date | Full-text search con índice invertido |

- **Índices B-tree en meta**: creados automáticamente para columnas tipo `Number` y `Date`. También en `file_position`.
- **JOIN por rowid**: `logs_fts_{id}.rowid = logs_meta_{id}.line_number` → costo cero (line_number es el rowid alias al ser INTEGER PRIMARY KEY).
- **Estrategia de consulta**:
  - Si hay FTS query global → `JOIN + FTS MATCH` → usa índice invertido.
  - Filtros de columna → `WHERE m.col LIKE/=/>/< ?` → usa índices B-tree.
  - Sin FTS → solo tabla meta (no hay JOIN innecesario).
- Al cerrar un archivo → `DROP TABLE` sobre ambas tablas → RAM liberada inmediatamente.
- Búsqueda agregada: si el usuario no filtra por archivo, `searchAll` ejecuta `UNION ALL` sobre todas las tablas meta activas.

### 2.3 UI — Vista de Datos

- **Vista dual**: tabla estructurada ↔ texto plano, con botón de toggle.
  - Vista por defecto: `text` si no hay columnas dinámicas (archivo no JSONL); `table` si las hay.
- **Paginación configurable**:
  - Controles en la barra de estado: `Offset [spinbox]` y `Rows [spinbox]` (default 1000, rango 10–100k).
  - El status bar muestra `Rows X–Y of Z`.
  - Al hacer scroll hasta el final de la tabla o del texto → incrementa offset automáticamente y recarga.
  - Al aplicar filtros → offset se resetea a 0.
- **Vista Tabla**:
  - Ordenamiento ascendente/descendente haciendo clic en el header (`QSortFilterProxyModel`).
  - Columnas ocultables/mostrables: clic derecho en el header → menú contextual con checkboxes. Estado persiste en `m_columnVisibility`.
  - Columna `raw` oculta por defecto cuando hay columnas dinámicas.
  - Selección múltiple de celdas (`ExtendedSelection`).
  - Ctrl+C copia las celdas seleccionadas al portapapeles (tab-separated, newline por fila).
  - Clic en una celda → pre-rellena un filtro con el nombre de columna y valor de la celda.
- **Vista Texto**: muestra el campo `raw` de cada fila en `QTextBrowser`; wrap configurable.

### 2.4 UI — Panel de Filtros

- Filas de filtro ilimitadas (no hay límite por columna).
- Cada fila: `[Logic▼] [Columna▼] [Operador▼] [Valor...] [×]`
  - Logic: AND / OR / NOT
  - Operadores: `contains`, `=`, `!=`, `>`, `<`
- Botón **＋ Add Filter** agrega filas; `[×]` las elimina individualmente.
- Al aplicar filtros o presionar Enter en el valor → offset se resetea a 0.
- Búsqueda global FTS5: campo de texto separado arriba → `MATCH` sobre `logs_fts_{id}`.
- Opción "Filter only": cuando está activa solo se muestran las filas que cumplen los filtros.

### 2.5 Rendimiento

- **Ingesta**: `FileWorker` en `QThread`. Chunks de 5.000 líneas con transacciones SQLite → minimiza lock de mutex.
- **Debounce**: `onChunkInserted` reinicia un `QTimer` de 1.5 segundos. `refreshData()` solo se llama al finalizar el timer (no en cada chunk). `onFinished` cancela el timer y hace el refresh final.
- **Consultas**: el hilo de UI ejecuta `queryRows()` que adquiere el mutex solo el tiempo de la query. Los índices B-tree y FTS garantizan consultas sublineales en la mayoría de los casos.

## 3. Stack Tecnológico

| Componente | Tecnología |
|---|---|
| Lenguaje | **C++17** (sin excepciones, RAII estricto) |
| UI Framework | **Qt6 Widgets** (UI 100% programática, sin `.ui` files) |
| Base de Datos | **SQLite** con módulo **FTS5** — esquema híbrido en `:memory:` |
| Build System | **CMake + Ninja** (vía Qt Creator) |

> El stack es **fijo**. No proponer migración a otros frameworks sin discusión previa del propietario.

## 4. Arquitectura y Estructura de Archivos

```
Logalizer/
├── main.cpp                  # Entry point
├── mainwindow.h/cpp          # Ventana principal, gestión de tabs, open dialog, About
├── logwidget.h/cpp           # Widget por pestaña: UI, filtros, vista dual, paginación
├── fileworker.h/cpp          # Hilo de ingesta: schema detection + chunked insert
├── schemadetector.h/cpp      # Detección automática de columnas + sanitizedName
├── logdatabase.h/cpp         # Singleton: esquema híbrido meta+FTS5, thread-safe
├── linerecord.h              # Struct LineRecord: fields (sanitizedName→value), raw, pos
├── CMakeLists.txt            # Build config
├── Logalizer.md              # Fuente de verdad del proyecto (este archivo)
├── README.md                 # Descripción pública (GitHub)
└── AGENTS.md                 # Guía para asistentes de IA
```

> Los archivos `safequeue.*`, `logdb.*`, `mainwindow.ui` y `logwidget.ui` fueron eliminados. No reinstanciarlos.

## 5. Convenciones de Desarrollo

- **Rendimiento**: Prioridad absoluta. Nunca bloquear el hilo de UI.
- **Threading**: `LogDatabase` usa `QMutexLocker` en todas sus APIs públicas. No crear conexiones SQLite adicionales.
- **Naming**: código fuente en inglés. Documentación interna puede ser en español.
- **Logging**: `qInfo()`, `qDebug()`, `qWarning()`, `qCritical()`. No usar `std::cout`.
- **Testing**: Tests unitarios obligatorios para parsers (`SchemaDetector`) y lógica de DB. Framework: `QtTest`.

## 6. Instrucciones para la IA

1. **Leer este archivo primero** antes de diseñar cualquier implementación.
2. **Verificar el stack** antes de proponer dependencias externas — requiere aprobación del propietario.
3. **No romper threading**: toda operación sobre `LogDatabase` desde un hilo secundario debe usar los métodos thread-safe del singleton.
4. **UI programática siempre**: sin archivos `.ui` ni Qt Designer.
5. **Actualizar `CMakeLists.txt`** si se añaden/eliminan archivos fuente.
6. **Mantener coherencia sanitizedName ↔ DB**: `record.fields` usa `sanitizedName` como clave; los headers de UI muestran `name` (original). `LogDatabase` traduce de nombre original a sanitizedName al construir SQL.
