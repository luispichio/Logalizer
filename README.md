# Logalizer

Logalizer es una aplicación de escritorio de alto rendimiento diseñada específicamente para el análisis y búsqueda eficiente de archivos de logs en formato "JSON Lines" (JSONL). Construida con **C++17**, **Qt6** y apuntalada por bases de datos in-memory **SQLite + FTS5** (Full-Text Search), Logalizer está pensada para manejar grandes volúmenes de datos sin comprometer la fluidez de la interfaz de usuario.

La idea es crear una alternativa "mas simple de usar por el público general" a la aplicación de terminal [The Logfile Navigator](https://lnav.org/), intentando mantener un balance entra las funcionalidades (las justas) y la simplicidad de uso.

## 🚀 Características Principales

- **Auto-Detección Inteligente de Esquemas**: Escanea las primeras líneas (hasta 10.000) de cada archivo de log para entender su estructura. Los campos JSON presentes en al menos el 80% de las líneas se convierten automáticamente en columnas indexadas, adaptándose a estructuras heterogéneas.
- **Búsqueda Ultrarrápida**: Utiliza tablas virtuales de SQLite (FTS5) en memoria RAM (`:memory:`) para ofrecer respuestas casi instantáneas, incluso en búsquedas complejas full-text.
- **Multihilo & Asincronismo**: La lectura e inserción de logs ocurre en hilos de background (chunks/transactions), permitiendo una **visualización parcial instantánea** de los datos en la interfaz sin esperar a que cargue todo el archivo.
- **Gestión Eficiente de Memoria**: Aislamiento total de recursos. Cada log abierto vive en su propia tabla temporal. Al cerrar la pestaña del archivo, la tabla se descarta inmediatamente (`DROP TABLE`), liberando la memoria RAM sin afectar a otros archivos.
- **Búsqueda Cruzada (Aggregate Search)**: Posibilidad de realizar consultas de búsqueda transparentes, ejecutando un `UNION ALL` en background sobre múltiples archivos cargados.
- **Filtros Dinámicos de Interfaz**: Construcción automática de widgets de filtrado (texto, numérico, fechas, booleanos) basados en el esquema detectado, con soporte para operadores lógicos (AND, OR, NOT).
- **Vista Dual**: Alterna fácilmente entre una vista jerárquica de tabla estructurada o el log puro de texto plano.

## 🛠️ Stack Tecnológico

- **Lenguaje**: C++17
- **Framework UI**: Qt6 (Widgets)
- **Base de Datos**: SQLite (módulo FTS5 integrado)
- **Build System**: CMake

## 📋 Roadmap y Funcionalidades Deseadas (ToDo)

En la siguiente tabla se detallan las tareas pendientes, optimizaciones y futuras características planificadas para el proyecto:

| Estado | Característica / Tarea | Descripción |
|:---:|---|---|
| ⏳ | **Resaltado de Búsqueda** | Implementar resaltado visual (highlighting) de coincidencias dentro del FTS5 en la vista de texto (`QTextBrowser`). |
| ⏳ | **Paginación en Disco (Virtual Scroll)** | Optimizar la carga evitando mantener el string entero del archivo en RAM, basando el cursor visual directamente en el `file_position` (offset) del archivo físico. |
| ⏳ | **Exportación de Resultados** | Permitir exportar las filas filtradas/buscadas a un nuevo archivo JSONL o CSV. |
| ⏳ | **Tests Unitarios** | Cobertura imprescindible sobre el parseo JSON, detección heurística de fechas (ISO-8601) y reglas lógicas. |
| ⏳ | **Guardar/Cargar Workspaces** | Capacidad de guardar las sesiones de vista, archivos que están abiertos y sus filtros activos para recuperar el entorno más tarde. |
| ⏳ | **Gráficos de Frecuencia** | Mini-histograma o gráfico de líneas en la UI marcando el volumen de logs en función del tiempo transcurrido o campos tipo `Date/Timestamp`. |
| ⏳ | **Parser Genérico de Texto** | Ampliar la ingesta más allá del JSON Lines soportando configuraciones mediante expresiones regulares (Grok-like) para logs raw comunes. |
| ⏳ | **Cliente SFTP** | Implementar cliente SFTP para conectarse a servidores remotos y descargar logs. |
| ⏳ | **Apertura de múltiples archivos (misma tabla)** | El diálogo de apertura de archivos debería permitir la selección múltiple de archivos para abrirlos en la misma tabla. |
| ⏳ | **Apertura de archivos comprimidos (.zip, .gz)** | Soporte apertura de archivos comprimidos (logrotate) |


## 🚀 Instalación y Compilación

*(Instrucciones genéricas para la compilación manual. Requiere entorno configurado con CMake y Qt6)*

```bash
# Clonar el repositorio
git clone https://github.com/tu-usuario/Logalizer.git
cd Logalizer

# Crear directorio de build
mkdir build && cd build

# Configurar con CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# Construir (ejemplo usando make)
make -j$(nproc)
```
*(Nota: El proyecto se soporta y testea habitualmente con **Qt Creator** utilizando Ninja como generador).*

## 🤝 Contribuir

Cualquier contribución es bienvenida. Para cambios grandes, por favor abre un issue primero para discutir qué te gustaría cambiar o implementar. ¡Asegúrate de mantener y actualizar los tests de forma consecuente a los cambios!
