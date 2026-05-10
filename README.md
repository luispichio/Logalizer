# Logalizer

Logalizer es una aplicación de escritorio para analizar archivos de logs con foco en dos tareas: buscar texto rápido y recortar por tiempo sin configuración previa. Está construida con **C++17**, **Qt6** y **SQLite + FTS5** en memoria.

## 🚀 Características Principales

- **Búsqueda Full-Text con FTS5**: cada archivo abierto crea un índice FTS5 sobre `raw` para búsquedas rápidas sin full-scan en la mayoría de los casos.
- **Metadatos temporales fijos**: además del índice de texto, cada línea guarda `line_number`, `file_position` y un timestamp detectado opcional para filtrar por rango temporal y ordenar por fecha.
- **Detección automática de timestamp**: prioriza campos JSON comunes como `@timestamp`, `timestamp`, `time` o `datetime`; si no encuentra uno, intenta detectar fechas directamente en el texto crudo.
- **Multihilo y UI fluida**: la ingesta ocurre en un `QThread` con inserts por lotes y refresco con debounce para no bloquear la interfaz.
- **Vista de texto enfocada**: el contenido se muestra en `QTextBrowser`, con wrap configurable y búsqueda dentro del buffer visible (`Ctrl+F`, `F3`, `Shift+F3`).
- **Filtro temporal simple**: permite combinar búsqueda FTS con rango `From/To`, opción `Only with timestamp` y orden por línea o por timestamp.
- **Aislamiento por pestaña**: cada archivo vive en sus propias tablas in-memory y al cerrar la pestaña se liberan inmediatamente.

## 🛠️ Stack Tecnológico

- **Lenguaje**: C++17
- **Framework UI**: Qt6 Widgets
- **Base de Datos**: SQLite + FTS5 en `:memory:`
- **Build System**: CMake + Ninja

## 📋 Roadmap y Funcionalidades Deseadas (ToDo)

| Estado | Característica / Tarea | Descripción |
|:---:|---|---|
| ⏳ | **Resaltado de Búsqueda** | Integrar snippets o resaltado alineado a resultados FTS5. |
| ⏳ | **Exportación de Resultados** | Exportar las filas filtradas/buscadas a JSONL o CSV. |
| ⏳ | **Tests Unitarios** | Cobertura sobre detección de timestamps y consultas en `LogDatabase`. |
| ⏳ | **Guardar/Cargar Workspaces** | Guardar sesiones: archivos abiertos y filtros temporales activos. |
| ⏳ | **Gráficos de Frecuencia** | Mini-histograma de volumen de logs en función del tiempo detectado. |
| ⏳ | **Parser Genérico de Texto** | Soporte para logs no-JSON mediante expresiones regulares. |
| ⏳ | **Cliente SFTP** | Conectarse a servidores remotos y descargar logs directamente. |
| ⏳ | **Archivos comprimidos (.zip, .gz)** | Soporte para apertura de archivos comprimidos. |

## 🚀 Instalación y Compilación

### Prerrequisitos

```bash
# Debian / Ubuntu
sudo apt install cmake ninja-build qt6-base-dev libqt6sql6-sqlite

# Fedora
sudo dnf install cmake ninja-build qt6-qtbase-devel
```

### Compilar desde fuentes

```bash
git clone https://github.com/luispichio/Logalizer.git
cd Logalizer

cmake -B build-release -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build-release --parallel

./build-release/Logalizer
```

*(El proyecto se soporta y testea habitualmente con **Qt Creator** utilizando Ninja como generador).*

## 📦 Packaging

El sistema de packaging está integrado en CMake vía **CPack**. La versión del paquete se genera automáticamente a partir de `MAJOR.MINOR.BUILD`, donde `BUILD` se incrementa con cada commit de Git.

### Prerrequisitos adicionales

```bash
# Debian / Ubuntu (.deb)
sudo apt install dpkg-dev

# Fedora / openSUSE (.rpm)
sudo dnf install rpm-build
```

### Generar `.deb`

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build-release --parallel
cd build-release
cpack -G DEB
```

### Generar `.rpm`

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build-release --parallel
cd build-release
cpack -G RPM
```

### Generar `.tar.gz`

```bash
cd build-release
cpack -G TGZ
```

## 🤝 Contribuir

Cualquier contribución es bienvenida. Para cambios grandes, por favor abre un issue primero para discutir qué te gustaría cambiar o implementar.

**Repositorio**: [github.com/luispichio/Logalizer](https://github.com/luispichio/Logalizer)
