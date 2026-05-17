# Logalizer

Logalizer es una aplicación de escritorio para analizar logs con foco en abrir archivos grandes, navegar por líneas y buscar texto rápido. Está construida con **C++17**, **Qt6 Widgets** y **SQLite + FTS5** en memoria.

## 🚀 Características Principales

- **Múltiples fuentes**: permite abrir archivos, leer desde `stdin` y ejecutar comandos para analizar su salida.
- **Búsqueda Full-Text con FTS5**: cada fuente crea una única tabla FTS5 sobre `raw` para búsquedas rápidas.
- **Navegación por línea**: `rowid` representa el número de línea y permite mover el visor por puntero sin usar paginación por offset.
- **Offset de archivo disponible**: cada línea conserva `file_position` como dato no indexado para usos posteriores.
- **Multihilo y UI fluida**: la ingesta ocurre en un `QThread` con inserts por lotes y refresco con debounce para no bloquear la interfaz.
- **Vista de texto enfocada**: el contenido se muestra en `QTextBrowser`, con wrap configurable, números de línea opcionales, búsqueda FTS5 global y búsqueda local dentro del buffer visible (`Ctrl+F`, `F3`, `Shift+F3`).
- **JSON Helper**: permite formatear líneas JSON visibles, filtrar campos por ruta, mostrar formato compacto `key=value` o solo valores.
- **Historial persistente**: los filtros FTS5, filtros de campos JSON y búsquedas locales se guardan como combos editables.
- **Últimos archivos**: el menú `File > Recent Files` conserva los últimos archivos abiertos sin reabrirlos automáticamente.
- **Aislamiento por pestaña**: cada fuente vive en su propia tabla in-memory y al cerrar la pestaña se libera inmediatamente.

## Uso Básico

- `File > Open...`: abre uno o más archivos de log.
- `File > Recent Files`: muestra los últimos archivos abiertos y permite limpiar la lista.
- `File > Run Command...`: ejecuta un comando y analiza su salida.
- `logalizer -` o `logalizer --stdin`: lee logs desde entrada estándar.
- `Filter`: aplica una expresión FTS5 global sobre todo el contenido indexado.
- `Find`: busca palabras dentro del conjunto filtrado y navega entre coincidencias.
- `JSON`: activa ayuda visual para líneas JSON, con `Compact`, `Only values` y filtro de campos.

## Configuración Persistente

Logalizer usa `QSettings` para guardar preferencias de usuario. En Linux, Qt guarda esta configuración normalmente en:

```text
~/.config/Logalizer/Logalizer.conf
```

Actualmente se persiste:

- Preferencias de visualización: wrap y números de línea.
- Preferencias de JSON Helper: activación, formato compacto, only values y filtro de campos.
- Historial de filtros FTS5.
- Historial de filtros de campos JSON.
- Historial de búsqueda local.
- Lista de últimos archivos abiertos.

## 🛠️ Stack Tecnológico

- **Lenguaje**: C++17
- **Framework UI**: Qt6 Widgets
- **Base de Datos**: SQLite + FTS5 en `:memory:`
- **Build System**: CMake + Ninja

## 📋 Roadmap y Funcionalidades Deseadas (ToDo)

| Estado | Característica / Tarea | Descripción |
|:---:|---|---|
| ⏳ | **Exportación de Resultados** | Exportar las filas filtradas/buscadas a JSONL o CSV. |
| ⏳ | **Tests Unitarios** | Cobertura sobre consultas FTS5 y navegación en `LogDatabase`. |
| ⏳ | **Workspaces** | Guardar sesiones completas: archivos abiertos, posición de navegación y filtros activos. |
| ⏳ | **Exportar Rangos** | Exportar rangos de líneas o resultados de búsqueda a JSONL o CSV. |
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
# Debian / Ubuntu (.deb, .rpm, .tar.gz)
sudo apt install dpkg-dev rpm

# Debian / Ubuntu (flujo completo con AppImage)
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-tools-dev qt6-tools-dev-tools libqt6sql6-sqlite libgl1-mesa-dev libxkbcommon-dev libvulkan-dev wget file libfuse2

# Fedora / openSUSE (.rpm)
sudo dnf install rpm-build
```

CPack puede generar paquetes `.rpm` desde Debian/Ubuntu si está instalado el paquete `rpm`. Las dependencias declaradas dentro del RPM deben seguir usando nombres válidos para distribuciones RPM.

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

### Generar todos los artefactos de release

```bash
scripts/package-release.sh
```

El script compila en modo `Release`, genera los paquetes disponibles con CPack, crea el AppImage usando el target `package-appimage`, copia todo a `dist/` y escribe `dist/SHA256SUMS`.

### Crear un release en GitHub desde la maquina local

Requiere GitHub CLI autenticado con `gh auth login`.

```bash
scripts/package-release.sh
scripts/create-github-release.sh v0.2.47
```

### Crear un release con GitHub Actions

```bash
git tag v0.2.47
git push origin v0.2.47
```

El workflow `.github/workflows/release.yml` genera los artefactos Linux y los publica en el release del tag.

## 🤝 Contribuir

Cualquier contribución es bienvenida. Para cambios grandes, por favor abre un issue primero para discutir qué te gustaría cambiar o implementar.

**Repositorio**: [github.com/luispichio/Logalizer](https://github.com/luispichio/Logalizer)
