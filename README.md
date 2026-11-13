# Logalizer

Logalizer es una aplicación de escritorio para analizar logs con foco en abrir archivos grandes, navegar por líneas y buscar texto rápido. Está construida con **C++17**, **Qt6 Widgets** y **SQLite + FTS5** en memoria.

## 🚀 Características Principales

- **Múltiples fuentes**: permite abrir archivos, leer desde `stdin` y ejecutar comandos para analizar su salida.
- **Seguimiento en vivo**: incorpora líneas agregadas a los archivos abiertos y muestra progresivamente la entrada de streams y comandos. Si el archivo se trunca o reemplaza, se recarga sin conservar su contenido anterior.
- **Auto-scroll**: sigue el final mientras esté visible; al navegar hacia arriba se pausa y al regresar al final se reactiva.
- **Búsqueda Full-Text con FTS5**: cada fuente crea una única tabla FTS5 sobre `raw` para búsquedas rápidas.
- **Navegación por línea**: `rowid` representa el número de línea y permite mover el visor por puntero sin usar paginación por offset.
- **Offset de archivo disponible**: cada línea conserva `file_position` como dato no indexado para usos posteriores.
- **Multihilo y UI fluida**: la ingesta ocurre en un `QThread` con inserts por lotes y refresco con debounce para no bloquear la interfaz.
- **Vista de texto enfocada**: el contenido se muestra en `QTextBrowser`, con wrap configurable, números de línea opcionales, búsqueda FTS5 global, búsqueda local dentro del buffer visible (`Ctrl+F`, `F3`, `Shift+F3`) y menú contextual para promover selecciones a filtros.
- **Field Viewer**: formatea campos de JSON, formatos regex detectados y líneas `key=value`; permite filtrar por campo, mostrar formato compacto o solo valores.
- **Historial persistente**: los filtros FTS5, filtros de campos y búsquedas locales se guardan como combos editables.
- **Últimos archivos**: el menú `File > Recent Files` conserva los últimos archivos abiertos sin reabrirlos automáticamente.
- **Aislamiento por pestaña**: cada fuente vive en su propia tabla in-memory y al cerrar la pestaña se libera inmediatamente.

## Uso Básico

- `File > Open...`: abre uno o más archivos de log.
- `File > Recent Files`: muestra los últimos archivos abiertos y permite limpiar la lista.
- `File > Run Command...`: ejecuta un comando y analiza su salida.
- `logalizer -` o `logalizer --stdin`: lee logs desde entrada estándar.
- `Filter`: aplica una expresión FTS5 global sobre todo el contenido indexado.
- `Find`: busca palabras dentro del conjunto filtrado y navega entre coincidencias.
- `Fields`: activa ayuda visual para campos estructurados, con `Compact`, `Only values` y filtro de campos.
- `Format`: permite usar la detección automática, texto plano o fijar un formato cargado para la pestaña. Al cambiarlo, Logalizer reprocesa la metadata en segundo plano.
- Menú contextual del visor: con texto seleccionado permite agregar o excluir la selección del filtro FTS5, agregar reglas de campos cuando `Fields` está activo, copiar la selección o copiar la línea completa.

El seguimiento observa la ruta y comprueba su tamaño periódicamente. Una truncación seguida de una escritura que recupere el tamaño anterior entre dos comprobaciones puede pasar inadvertida; las reescrituras internas y la combinación de archivos rotados no están soportadas. Si una línea final sin salto continúa creciendo, su actualización espera al siguiente salto y provoca una recarga del archivo. Como la lectura de archivos usa `mmap`, un truncado externo simultáneo a una lectura puede provocar un fallo del proceso antes de que se detecte el cambio.

## Configuración Persistente

Logalizer usa `QSettings` para guardar preferencias de usuario. En Linux, Qt guarda esta configuración normalmente en:

```text
~/.config/Logalizer/Logalizer.conf
```

Actualmente se persiste:

- Preferencias de visualización: wrap y números de línea.
- Preferencias del Field Viewer: activación, formato compacto, only values y filtro de campos.
- Historial de filtros FTS5.
- Historial de filtros de campos.
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
| ⏳ | **Autodetección de Timestamps** | Detectar automáticamente los timestamps de cada línea -> columna en la tabla -> permitir ordenamiento / búsqueda / rangos temporales |
| ⏳ | **Autodetección de Log-level** | Detectar automáticamente nivel de log -> filtrado, resaltado, histograma |
| ⏳ | **Resaltado de contenidos** | Resaltado de errores, warnings, cadenas, números, URL's, etc |
| ⏳ | **Autocompletado** |  |
| ⏳ | **Mezcla de archivos** | Mezcla de múltiples archivos en una misma vista (ordenado por timestamp). |
| ⏳ | **Exportación de Resultados** | Exportar las filas filtradas/buscadas a JSONL o CSV. |
| ⏳ | **Exportar Rangos** | Exportar rangos de líneas o resultados de búsqueda a JSONL o CSV. |
| ⏳ | **Tests Unitarios** | Cobertura sobre consultas FTS5 y navegación en `LogDatabase`. |
| ⏳ | **Workspaces** | Guardar sesiones completas: archivos abiertos, posición de navegación y filtros activos. |
| ⏳ | **Archivos comprimidos (.zip, .gz)** | Soporte para apertura de archivos comprimidos. |
| ⏳ | **Detección automática de formatos** | Detectar automáticamente formato de logs -> columnas en tabla -> filtros dinámicos |
| ⏳ | **Histograma** | Estadísticas de tipos de mensajes/líneas (warnings, errores, coincidencias de búsqueda) a lo largo del tiempo |
| ⏳ | **Cliente SFTP** | Conectarse a servidores remotos y descargar logs directamente. |

## 🚀 Instalación y Compilación

### Prerrequisitos

```bash
# Debian / Ubuntu
sudo apt install cmake ninja-build qt6-base-dev libqt6sql6-sqlite

# Fedora
sudo dnf install cmake ninja-build qt6-qtbase-devel
```

### Compilar desde fuentes sin publicar

```bash
git clone https://github.com/luispichio/Logalizer.git
cd Logalizer

scripts/build-release.sh

ctest --test-dir build-release --output-on-failure

./build-release/Logalizer
```

*(El proyecto se soporta y testea habitualmente con **Qt Creator** utilizando Ninja como generador).*

`Release` significa que la aplicación se compila con optimizaciones. Este comando
no crea tags ni publica nada en GitHub. Para configurar y compilar manualmente:

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build-release --parallel
```

## 📦 Packaging

Hay tres formas de trabajar según lo que necesites:

| Objetivo | Comando | Resultado | ¿Publica en GitHub? |
|---|---|---|---|
| Compilar y ejecutar localmente | `scripts/build-release.sh` | Ejecutable `build-release/Logalizer` | No |
| Generar instaladores para probar o distribuir manualmente | `scripts/package-release.sh` | Paquetes en `dist/` | No |
| Publicar una nueva versión | `scripts/release.sh patch` | Tag, paquetes y release de GitHub | **Sí** |

Los scripts se ejecutan desde la raíz del repositorio. Solo el último crea un tag
y publica en GitHub. Los paquetes se generan con **CPack**.

### Prerrequisitos adicionales

```bash
# Debian / Ubuntu (.deb, .rpm, .tar.gz)
sudo apt install dpkg-dev rpm

# Debian / Ubuntu (flujo completo con AppImage)
sudo apt install build-essential cmake ninja-build qt6-base-dev libqt6sql6-sqlite libgl1-mesa-dev libxkbcommon-dev libvulkan-dev wget file libfuse2

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

### Generar instaladores sin publicar

```bash
scripts/package-release.sh
```

El script compila en modo `Release` y genera `.deb`, `.rpm`, `.tar.gz` y AppImage.
Copia los resultados a `dist/` junto con `SHA256SUMS` y metadata de la compilación.
**No crea tags ni publica nada.** Requiere las herramientas de empaquetado indicadas
arriba; para omitir el AppImage:

```bash
WITH_APPIMAGE=0 scripts/package-release.sh
```

El directorio de destino debe estar vacío. Para conservar paquetes anteriores,
elegí otro directorio:

```bash
DIST_DIR="$PWD/dist/prueba-01" scripts/package-release.sh
```

El script elimina paquetes anteriores en la raíz de `build-release/` antes de
generar los nuevos. La carpeta de destino seleccionada no se borra.

### Versiones y releases

Los tags Git `vMAJOR.MINOR.PATCH` son la fuente de la versión. Compilar o ejecutar
`package-release.sh` **no incrementa la versión**: usa el estado actual de Git.
En **About**, por ejemplo:

| Estado del código | Versión mostrada en About |
|---|---|
| En el tag `v0.2.62`, sin cambios locales | `0.2.62 (build 62 · abc123…)` |
| Dos commits después de ese tag | `0.2.62-dev.2+abc123… (build 64 · abc123…)` |
| Con cambios locales sin commitear | Agrega `(dirty)` a la versión |

El hash y el contador de commits son datos de diagnóstico, no el número de patch.
Sin Git se usa `0.0.0-dev`; sin tags se agrega también el hash si está disponible.
Los paquetes de desarrollo usan **solo la versión numérica del último tag**: por
ejemplo, el nombre del paquete puede indicar `0.2.62` mientras About muestra
`0.2.62-dev.2+…`. No deben publicarse como releases estables.

La cabecera de versión se actualiza en cada compilación; para actualizar también
la versión numérica de CPack es necesario volver a configurar CMake. El script de
empaquetado lo hace automáticamente.

### Publicar una nueva versión en GitHub

Para compilar y publicar un release, con GitHub CLI autenticado (`gh auth login`),
historial completo y árbol de trabajo limpio:

```bash
scripts/release.sh patch   # o minor / major
```

Por ejemplo, si el último tag alcanzable es `v0.2.62`, `patch` crea `v0.2.63`,
`minor` crea `v0.3.0` y `major` crea `v1.0.0`. La versión estable del nuevo tag
aparece tanto en About como en los paquetes.

El script obtiene los tags de `origin`, incrementa la versión más alta alcanzable
desde HEAD, crea un tag anotado, compila en un directorio nuevo bajo
`build-release/`, verifica los artefactos y publica el tag y el release en GitHub.
No modifica releases existentes. HEAD debe ser un commit nuevo que todavía no tenga
un tag de versión. `WITH_APPIMAGE=0` omite el AppImage.

Si falla después de crear el tag, este se conserva. Corregí el problema y, con el
mismo commit limpio y el tag todavía en HEAD, volvé a generar los paquetes en un
directorio vacío y publicalos:

```bash
BUILD_DIR="$PWD/build-release/retry" DIST_DIR="$PWD/build-release/retry/dist" scripts/package-release.sh
DIST_DIR="$PWD/build-release/retry/dist" scripts/create-github-release.sh v0.2.63
```

El script de publicación comprueba el tag, el commit y `SHA256SUMS`; no acepta
artefactos sin metadata o que no correspondan al checkout actual. Si el tag ya se
subió pero falló la creación del release, el mismo comando permite reintentar.

## 🤝 Contribuir

Cualquier contribución es bienvenida. Para cambios grandes, por favor abre un issue primero para discutir qué te gustaría cambiar o implementar.

**Repositorio**: [github.com/luispichio/Logalizer](https://github.com/luispichio/Logalizer)
