# Definición del Proyecto: Logalizer

Este archivo `Logalizer.md` sirve como la fuente de verdad para el contexto del proyecto. Debe ser actualizado a medida que el proyecto evoluciona para mantener al equipo y a los asistentes de IA alineados.

## 1. Visión General y Objetivos
- **Descripción**: Aplicación de Escritorio para análisis de logs tipo "json lines" de alto rendimiento.
- **Objetivo Principal**: Crear una herramienta capaz de procesar y visualizar grandes volúmenes de logs eficientemente.
- **Plataforma**: Desktop (Linux, Windows).
- **Características**:
    - Utilización de SQLite + FTS5 para indexación y búsqueda de logs.
    - Bases de datos en RAM.
    - Procesamiento de archivos
        - Multithreaded.
        - Capacidad de abrir múltiples archivos de logs.
        - Detección automática de formato de logs
        - Escaneo de las primeras 1000 líneas para detectar el formato.
            - Cada archivo analizado se carga en su propia tabla SQLite FTS5 dedicada (ej. `logs_1`, `logs_2`) para respetar esquemas dispares.
            - Al cerrar un archivo, se elimina su tabla (DROP TABLE), liberando RAM instantáneamente sin afectar a otros.
            - Cada campo de JSON que esté en un porcentaje a definir (por defecto 80%) de las líneas será una columna y se indexará de forma separada por archivo.
            - Los campos de tipo objeto o array no se indexan (solo funciones de búsqueda de texto).
            - Cómo mínimo los campos / columnas: raw, file_position.
            - Posibilidad de buscar de manera agregada: si el usuario no filtra por archivo específico, `search_logs` procesa un UNION ALL transparente de todas las tablas activas.
        - Visualización de logs en formato texto o tabla.
        - Un tab por archivo de logs.
        - Filtros dinámicos de múltiples filas por columna (panelflexible):
            - Cada fila de filtro: `[Logic▼] [Columna▼] [Operador▼] [Valor] [×]`
            - Operadores: `contains`, `=`, `!=`, `>`, `<`.
            - Operadores lógicos: AND, OR, NOT entre filas.
            - Se pueden agregar N filtros para la misma columna.
            - Al hacer clic en una celda de la tabla se pre-rellena un filtro con columna + valor.
            - Búsqueda por palabras clave (FTS5 MATCH).
            - Opción "Filter only" → muestra solo las líneas que cumplen los filtros.
        - Vista tabla:
            - Ordenamiento ascendente/descendente haciendo clic en el header de columna.
            - Columnas ocultables/mostrables con clic derecho en el header.
            - La columna `raw` está oculta por defecto cuando hay columnas dinámicas.
            - Selección múltiple de celdas (ExtendedSelection).
            - Copiar selección al portapapeles con Ctrl+C (tab-separated, newline por fila).
        - Vista de texto: vista por defecto cuando el archivo no tiene columnas dinámicas (no es JSONL).
        - Posibilidad de analizar múltiples archivos de logs concurrentemente.
        - Al cerrar un archivo de logs -> se deben liberar los recursos asociados utilizando `DROP TABLE`.
        - Visualización parcial instantánea: permite ver la tabla mientras los bytes restantes del archivo se ingieren asincrónicamente usando chunks/transactions cada ~5000 líneas.
            - Debounce de 1.5 segundos: el refresh de la UI se dispara máximo cada 1.5 segundos durante la ingesta para no bloquear el hilo de UI.
        - El scroll permite (en un futuro) ir a una posición específica del archivo: Evita tener que cargar todo el archivo en memoria.

## 2. Stack Tecnológico
- QT6

## 3. Arquitectura y Estructura
>ToDo

## 4. Convenciones de Desarrollo
### Reglas Generales
- **Rendimiento**: Prioridad absoluta. Evitar bloqueos en el hilo de UI (Main Thread).
- **Testing**: Tests unitarios obligatorios para los parsers de logs.

## 5. Instrucciones para la IA
- **Contexto**: Al generar código, verificar primero si se ha decidido el stack final.
- **Optimización**: Sugerir estructuras de datos eficientes para manejo de strings y búsqueda (ej. no cargar todo el archivo en RAM si no es necesario).
- **UI**: Priorizar usabilidad/UX para filtrado y búsqueda de datos complejos.
