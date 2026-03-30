# Logalizer

Logalizer es una aplicación de escritorio de alto rendimiento diseñada específicamente para el análisis y búsqueda eficiente de archivos de logs en formato "JSON Lines" (JSONL). Construida con **C++17**, **Qt6** y apuntalada por bases de datos in-memory **SQLite + FTS5** (Full-Text Search) con esquema híbrido, Logalizer está pensada para manejar grandes volúmenes de datos sin comprometer la fluidez de la interfaz de usuario.

La idea es crear una alternativa "más simple de usar por el público general" a la aplicación de terminal [The Logfile Navigator](https://lnav.org/), intentando mantener un balance entre las funcionalidades (las justas) y la simplicidad de uso.

## 🚀 Características Principales

- **Auto-Detección Inteligente de Esquemas**: Escanea las primeras líneas (hasta 10.000) de cada archivo de log para entender su estructura. Los campos JSON presentes en al menos el 80% de las líneas se convierten automáticamente en columnas indexadas. Los nombres de campos con caracteres especiales (`@`, `.`, `-`, espacios, etc.) se sanean automáticamente para compatibilidad con SQLite.
- **Esquema Híbrido SQLite (meta + FTS5)**: Por cada archivo se crean dos tablas en memoria:
  - `logs_meta_{id}`: Tabla regular con **índices B-tree** sobre columnas `Number` y `Date` → rangos numéricos y temporales ultrarrápidos.
  - `logs_fts_{id}`: Tabla virtual **FTS5** con el texto (`raw` + columnas `String`/`Date`) → full-text search con índice invertido.
  - Las consultas combinan ambas con `JOIN por rowid`, usando cada índice según el tipo de filtro.
- **Búsqueda Ultrarrápida**: Filtros de texto usan `FTS5 MATCH` (índice invertido); filtros numéricos/fecha usan el índice B-tree del meta. Sin full-scans para operaciones comunes.
- **Multihilo & Asincronismo**: La lectura e inserción de logs ocurre en hilos de background (chunks de 5.000 líneas). Un debounce de 1.5 segundos garantiza que la UI permanezca fluida durante la carga.
- **Gestión Eficiente de Memoria**: Aislamiento total de recursos. Cada log abierto vive en sus propias tablas temporales. Al cerrar la pestaña, `DROP TABLE` libera la RAM instantáneamente.
- **Búsqueda Cruzada (Aggregate Search)**: `UNION ALL` transparente sobre las tablas meta de todos los archivos cargados.
- **Filtros Dinámicos Múltiples**: Panel de filas ilimitadas. Cada fila: selector de columna, operador (`contains`, `=`, `!=`, `>`, `<`) y lógica (AND/OR/NOT). Clic en celda → pre-rellena el filtro automáticamente.
- **Vista Dual Inteligente**: Alterna entre tabla estructurada y texto plano.
  - **Tabla**: ordenamiento por header (click), visibilidad de columnas (clic derecho en header), columna `raw` oculta por defecto, selección múltiple de celdas, Ctrl+C para copiar.
  - **Texto**: vista predeterminada para archivos sin estructura JSON.
- **Paginación Configurable**: Controles de `Offset` y `Rows` en la barra de estado. Al hacer scroll hasta el final → avanza automáticamente al siguiente bloque de filas.

## 🛠️ Stack Tecnológico

- **Lenguaje**: C++17
- **Framework UI**: Qt6 (Widgets)
- **Base de Datos**: SQLite (módulo FTS5 integrado) — esquema híbrido meta + FTS5
- **Build System**: CMake + Ninja (vía Qt Creator)

## 📋 Roadmap y Funcionalidades Deseadas (ToDo)

| Estado | Característica / Tarea | Descripción |
|:---:|---|---|
| ⏳ | **Resaltado de Búsqueda** | Implementar resaltado visual de coincidencias FTS5 en `QTextBrowser` (snippets). |
| ⏳ | **Exportación de Resultados** | Exportar las filas filtradas/buscadas a un nuevo archivo JSONL o CSV. |
| ⏳ | **Tests Unitarios** | Cobertura sobre `SchemaDetector` (parseo JSON, heurística ISO-8601) y `LogDatabase` (híbrido). Usar `QtTest`. |
| ⏳ | **Guardar/Cargar Workspaces** | Guardar sesiones: archivos abiertos, filtros activos, visibilidad de columnas. |
| ⏳ | **Gráficos de Frecuencia** | Mini-histograma de volumen de logs en función del tiempo (campos `Date`). |
| ⏳ | **Parser Genérico de Texto** | Soporte para logs no-JSON mediante expresiones regulares (Grok-like). |
| ⏳ | **Cliente SFTP** | Conectarse a servidores remotos y descargar logs directamente. |
| ⏳ | **Apertura múltiple (misma tabla)** | Seleccionar múltiples archivos para abrirlos en la misma tabla (merge de schemas). |
| ⏳ | **Archivos comprimidos (.zip, .gz)** | Soporte para apertura de archivos comprimidos (logrotate). |
| ⏳ | **Ordenamiento DB-side** | `ORDER BY` en SQL al hacer clic en el header (complementa el sort en memoria actual). |

## 🚀 Instalación y Compilación

```bash
# Clonar el repositorio
git clone https://github.com/luispichio/Logalizer.git
cd Logalizer

# Crear directorio de build
mkdir build && cd build

# Configurar con CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# Construir
make -j$(nproc)
```

*(Nota: El proyecto se soporta y testea habitualmente con **Qt Creator** utilizando Ninja como generador).*

## 🤝 Contribuir

Cualquier contribución es bienvenida. Para cambios grandes, por favor abre un issue primero para discutir qué te gustaría cambiar o implementar.

**Repositorio**: [github.com/luispichio/Logalizer](https://github.com/luispichio/Logalizer)
