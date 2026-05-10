# Definición del Proyecto: Logalizer

Este archivo `Logalizer.md` es la fuente de verdad del proyecto.

## 1. Visión General y Objetivos

- **Descripción**: Aplicación de escritorio para análisis de logs de alto rendimiento.
- **Objetivo principal**: abrir logs grandes, buscar texto rápido y navegar resultados por tiempo sin configuración previa.
- **Plataforma**: Desktop (Linux, Windows).
- **Repositorio**: https://github.com/luispichio/Logalizer

## 2. Características Funcionales

### 2.1 Procesamiento de Archivos

- Ingesta multihilo: `FileWorker` corre en un `QThread` dedicado.
- Múltiples archivos abiertos concurrentemente, un tab por archivo.
- Cada línea persiste siempre:
  - `raw`
  - `file_position`
  - `line_number`
  - `timestamp_text` opcional
  - `timestamp_unix_ms` opcional
  - `timestamp_source` opcional
- La detección de timestamp sigue esta prioridad:
  - campos JSON comunes: `@timestamp`, `timestamp`, `time`, `ts`, `datetime`, `date`
  - cualquier string JSON que parezca fecha
  - regex sobre la línea cruda

### 2.2 Base de Datos

Por cada archivo abierto se crean dos tablas in-memory:

| Tabla | Tipo SQLite | Contenido | Propósito |
|---|---|---|---|
| `logs_meta_{id}` | Tabla regular | `line_number`, `file_position`, `raw`, `timestamp_text`, `timestamp_unix_ms`, `timestamp_source` | navegación, filtro temporal, orden |
| `logs_fts_{id}` | Virtual FTS5 | `raw` | full-text search |

- Índices B-tree en `logs_meta_{id}`:
  - `file_position`
  - `timestamp_unix_ms`
- JOIN por `rowid`:
  - `logs_fts_{id}.rowid = logs_meta_{id}.line_number`
- Estrategia de consulta:
  - FTS → `MATCH` sobre `logs_fts_{id}`
  - rango temporal → filtro sobre `timestamp_unix_ms`
  - orden → por `line_number` o `timestamp_unix_ms`
- Al cerrar un archivo se hace `DROP TABLE` sobre meta y FTS para liberar RAM.

### 2.3 UI

- Vista única de texto en `QTextBrowser`.
- Búsqueda local dentro del buffer visible:
  - `Ctrl+F`
  - `F3`
  - `Shift+F3`
  - regex opcional
  - case-sensitive opcional
- Filtros globales simples:
  - búsqueda FTS
  - `From`
  - `To`
  - `Only with timestamp`
  - orden por línea o timestamp
- Paginación mediante buffer deslizante configurable.

### 2.4 Rendimiento

- Inserts por lotes de 5.000 líneas dentro de transacciones SQLite.
- `onChunkInserted` usa debounce de 1.5 s para evitar refrescos agresivos.
- El hilo de UI nunca espera lecturas de archivo ni parseo de disco.

## 3. Stack Tecnológico

| Componente | Tecnología |
|---|---|
| Lenguaje | **C++17** |
| UI Framework | **Qt6 Widgets** |
| Base de Datos | **SQLite + FTS5** en `:memory:` |
| Build System | **CMake + Ninja** |

## 4. Estructura de Archivos

```text
Logalizer/
├── main.cpp
├── mainwindow.h/cpp
├── logwidget.h/cpp
├── fileworker.h/cpp
├── logdatabase.h/cpp
├── linerecord.h
├── CMakeLists.txt
├── Logalizer.md
├── README.md
└── AGENTS.md
```

## 5. Convenciones de Desarrollo

- Rendimiento primero: nunca bloquear el hilo de UI.
- Toda operación pública de `LogDatabase` usa `QMutexLocker`.
- Código fuente en inglés; documentación interna puede estar en español.
- Logging con `qInfo()`, `qDebug()`, `qWarning()`, `qCritical()`.

## 6. Instrucciones Para IA

1. Leer este archivo antes de diseñar cambios importantes.
2. No agregar dependencias externas sin aprobación explícita.
3. Mantener UI programática; no usar `.ui`.
4. No crear conexiones SQLite alternativas fuera de `LogDatabase`.
5. Si se agregan o eliminan archivos fuente, actualizar `CMakeLists.txt`.
