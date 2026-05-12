# Definición del Proyecto: Logalizer

Este archivo `Logalizer.md` es la fuente de verdad del proyecto.

## 1. Visión General y Objetivos

- **Descripción**: Aplicación de escritorio para análisis de logs de alto rendimiento.
- **Objetivo principal**: abrir logs grandes, buscar texto rápido y navegar por líneas sin configuración previa.
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

### 2.2 Base de Datos

Por cada archivo abierto se crea una tabla FTS5 in-memory:

| Tabla | Tipo SQLite | Contenido | Propósito |
|---|---|---|---|
| `logs_{id}` | Virtual FTS5 | `file_position UNINDEXED`, `raw` | navegación y full-text search |

- `rowid = line_number + 1`.
- `file_position` se guarda como columna `UNINDEXED`.
- Estrategia de consulta:
  - navegación → `WHERE rowid >= ? ORDER BY rowid LIMIT ?`
  - búsqueda → `MATCH` sobre `logs_{id}` y posicionamiento en el primer `rowid` coincidente
- Al cerrar un archivo se hace `DROP TABLE` para liberar RAM.

### 2.3 UI

- Vista única de texto en `QTextBrowser`.
- Búsqueda local dentro del buffer visible:
  - `Ctrl+F`
  - `F3`
  - `Shift+F3`
  - regex opcional
  - case-sensitive opcional
- Búsqueda FTS5 global sobre todo el archivo.
- Navegación por puntero: la vista carga solo las filas necesarias para llenar el visor.

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
