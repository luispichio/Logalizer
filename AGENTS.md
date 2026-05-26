# AGENTS.md — Guía para Agentes de IA

Este archivo define el contexto operativo para asistentes de IA que colaboren en **Logalizer**.

## 🧭 Qué es Logalizer

Logalizer es una aplicación de escritorio para análisis de logs con foco en:

- abrir archivos grandes rápido
- buscar texto con FTS5
- navegar por líneas usando un puntero de primera fila visible
- mantener una tabla FTS5 in-memory por archivo
- analizar archivos, `stdin` y salida de comandos
- conservar preferencias simples, historiales de filtros y últimos archivos abiertos

Repositorio: https://github.com/luispichio/Logalizer

## 🛠️ Stack

| Componente | Tecnología | Notas |
|---|---|---|
| Lenguaje | **C++17** | Sin excepciones, RAII estricto |
| UI | **Qt6 Widgets** | UI programática, sin `.ui` |
| DB | **SQLite + FTS5** | `:memory:` por archivo |
| Config | **QSettings** | Preferencias de UI, historiales y recientes |
| Build | **CMake + Ninja** | Usualmente vía Qt Creator |

> [!IMPORTANT]
> El stack es fijo. No proponer migraciones a otros frameworks sin discusión previa explícita.

## 🗂️ Estructura del Proyecto

```text
Logalizer/
├── main.cpp
├── mainwindow.h/cpp
├── logwidget.h/cpp
├── fileworker.h/cpp
├── streamworker.h/cpp
├── processworker.h/cpp
├── logdatabase.h/cpp
├── linerecord.h
├── version.h.in
├── CMakeLists.txt
├── pkg/
├── scripts/
├── Logalizer.md
├── README.md
└── AGENTS.md
```

## 🏛️ Arquitectura

```text
MainWindow
  ├── menú File/Open/Recent Files/Run Command
  ├── QSettings: últimos archivos abiertos
  └── QTabWidget
        └── LogWidget (1 por fuente)
              ├── FileWorker | StreamWorker | ProcessWorker (QThread)
              │     └── ingesta en chunks
              │           └── inserta filas en FTS5 por lotes
              ├── QSettings: preferencias e historiales de filtros
              └── LogDatabase (singleton)
                    └── logs_{id}
                          ├── rowid = line_number + 1
                          ├── file_position UNINDEXED
                          └── raw
```

## 🔩 Reglas Clave

- `LogDatabase` es thread-safe y toda operación pública usa `QMutexLocker`.
- No crear conexiones SQLite adicionales.
- El hilo de UI nunca debe bloquear en disco o DB.
- La UI se construye siempre en C++ programático.
- Cada `LogWidget` es autónomo: al destruirse detiene su worker y hace `DROP TABLE`.
- Usar `QSettings("Logalizer", "Logalizer")` para preferencias simples, historiales y lista de recientes.
- No persistir contenido de logs ni resultados FTS en configuración.
- La lista de recientes guarda solo rutas de archivos; no reabrir sesiones automáticamente salvo requerimiento explícito.
- Tener extremo cuidado al registrar `eventFilter` en `QTextBrowser` o en su `viewport()`: `Resize`, `Wheel` y cambios de HTML pueden retroalimentarse y bloquear la UI. Para menú contextual u otras acciones puntuales, preferir señales específicas como `customContextMenuRequested` antes que ampliar el alcance del `eventFilter`.

## 🧾 Persistencia por Línea

- `raw`
- `file_position`
- `line_number`

## 🧱 UI Actual

- Vista única de texto en `QTextBrowser`.
- Búsqueda full-text FTS5 global sobre toda la fuente, con combo editable e historial persistente.
- Navegación por puntero: el visor carga solo las filas necesarias para llenar el viewport.
- Búsqueda local dentro del conjunto filtrado con `Ctrl+F`, `F3`, `Shift+F3`, combo editable e historial persistente.
- JSON Helper para líneas visibles: `Compact`, `Only values` y filtro de campos por ruta (`level,msg,user.id,-metadata.*`).
- Barra inferior con tamaño, líneas, estados de búsqueda/filtro y progreso.
- Menú `File > Recent Files` con últimos archivos abiertos y acción para limpiar la lista.

## ⚙️ Persistencia de Usuario

- Implementada con `QSettings`.
- En Linux, Qt guarda normalmente en `~/.config/Logalizer/Logalizer.conf`.
- Se persisten preferencias de vista, opciones de JSON Helper, historiales de filtros y últimos archivos.
- No se persisten tablas SQLite, buffers de texto ni contenido de logs.

## ✅ Instrucciones para la IA

1. Leer `Logalizer.md` primero.
2. No agregar dependencias externas sin aprobación.
3. Mantener la arquitectura de threading.
4. Actualizar `CMakeLists.txt` si se agregan o eliminan archivos fuente.
5. Código fuente en inglés; documentación interna puede estar en español.

## 🚫 Anti-Patrones

| ❌ No hacer | ✅ Alternativa |
|---|---|
| Bloquear el hilo de UI con lecturas de archivo | `QThread` + `FileWorker` |
| Cargar todo el archivo en memoria | leer línea a línea con `QTextStream` |
| Abrir conexiones SQLite adicionales | usar `LogDatabase::instance()` |
| Reintroducir `.ui` o Qt Designer | mantener UI programática |
| Llamar `refreshData()` en cada chunk | render inicial si el buffer está vacío y luego usar debounce |
| Registrar `eventFilter` extra en `QTextBrowser::viewport()` para acciones simples | usar señales específicas (`customContextMenuRequested`, shortcuts, slots) y no mezclarlo con lógica de resize/render |
| Persistir contenido de logs en `QSettings` | persistir solo preferencias, historiales y rutas recientes |

## 📌 Roadmap Prioritario

1. Resaltado de resultados FTS en el visor de texto.
2. Tests unitarios para navegación FTS5 y `LogDatabase`.
3. Exportación de resultados a JSONL o CSV.
4. Soporte de `.gz` y `.zip`.
5. Workspaces o sesiones persistentes completas.
