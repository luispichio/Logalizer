# AGENTS.md — Guía para Agentes de IA

Este archivo define el contexto operativo para asistentes de IA que colaboren en **Logalizer**.

## 🧭 Qué es Logalizer

Logalizer es una aplicación de escritorio para análisis de logs con foco en:

- abrir archivos grandes rápido
- buscar texto con FTS5
- navegar por líneas usando un puntero de primera fila visible
- mantener una tabla FTS5 in-memory por archivo

Repositorio: https://github.com/luispichio/Logalizer

## 🛠️ Stack

| Componente | Tecnología | Notas |
|---|---|---|
| Lenguaje | **C++17** | Sin excepciones, RAII estricto |
| UI | **Qt6 Widgets** | UI programática, sin `.ui` |
| DB | **SQLite + FTS5** | `:memory:` por archivo |
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
├── logdatabase.h/cpp
├── linerecord.h
├── CMakeLists.txt
├── Logalizer.md
├── README.md
└── AGENTS.md
```

## 🏛️ Arquitectura

```text
MainWindow
  └── QTabWidget
        └── LogWidget (1 por archivo)
              ├── FileWorker (QThread)
              │     └── ingesta en chunks de 5.000 líneas
              │           └── inserta filas en FTS5
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

## 🧾 Persistencia por Línea

- `raw`
- `file_position`
- `line_number`

## 🧱 UI Actual

- Vista única de texto en `QTextBrowser`.
- Búsqueda full-text FTS5 global sobre todo el archivo.
- Navegación por puntero: el visor carga solo las filas necesarias para llenar el viewport.
- Búsqueda local dentro del buffer visible con `Ctrl+F`, `F3`, `Shift+F3`.

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
| Llamar `refreshData()` en cada chunk | usar debounce |

## 📌 Roadmap Prioritario

1. Resaltado de resultados FTS en el visor de texto.
2. Tests unitarios para navegación FTS5 y `LogDatabase`.
3. Exportación de resultados a JSONL o CSV.
4. Soporte de `.gz` y `.zip`.
5. Workspaces o sesiones persistentes.
