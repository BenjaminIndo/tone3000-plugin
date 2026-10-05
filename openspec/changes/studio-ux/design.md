# Design

## Context

`MultitrackView` es hoy una pila de filas con widgets estándar dentro de la franja entre la barra superior y el faceplate (1024 × 406 en el espacio de diseño). El motor (`Multitrack.h`) ya tiene pistas con inicio, recorte, nudge, paneo, bucle, punch, metrónomo, importación y mezcla, y se controla con comandos y un estado JSON sondeado a 12 Hz. La app escala todo el espacio de diseño (1024 × 578 más franjas) a la pantalla del iPad; en un iPad de 9.ª generación en horizontal la escala es ~1, así que 44 px de diseño ≈ 44 pt.

## Goals / Non-Goals

**Goals:** sensación de GarageBand, edición directa, objetivos táctiles de 44 pt, una sola compilación.

**Non-Goals:** varias regiones por pista, split, deshacer, efectos por pista, más de 6 pistas.

## Decisions

- **Pantalla completa**: `PluginRoot` da al estudio `getLocalBounds()` (como Ajustes). Volver al ampli solo cierra la vista; el motor sigue sonando.
- **Componentes propios dentro de `MultitrackView.cpp`**: `GlyphButton` (iconos dibujados: play, stop, grabar, retroceder, bucle, metrónomo, cuenta atrás, +, −, ajustes), `Lcd`, `Headers`, `Timeline` y paneles (`SongPanel`, `TrackPanel`, `MixPanel`) con un `Scrim` que los cierra al tocar fuera. Las clases anidadas leen un modelo de vista común (`Model` + `TrackModel`) que `apply()` rellena desde el estado.
- **Altura fija de carril**: las 6 pistas se reparten el alto disponible (mínimo 64 px), así no hay desplazamiento vertical que compita con los gestos.
- **Geometría de la línea de tiempo**: `x = (t - scrollSec) * pxPerSec`. La regla (30 px) y la franja de marcas (20 px: mitad superior bucle, inferior punch) van arriba; los carriles debajo. Rejilla con líneas de compás y de tiempo cuando caben (más de 12 px).
- **Gestos con estado local**: al tocar se decide el modo (cabezal, banda de bucle/punch, mover región, recortar borde izquierdo/derecho con margen de 16 px, desplazar). Durante el arrastre solo cambia la vista previa local; al soltar se envía un único comando (`region`, `loopRange`, `punchRange`, `seekSec`), lo que evita escribir el proyecto en cada movimiento. Recortar a la izquierda mueve también el inicio para que el audio quede en su sitio. El ajuste a la rejilla redondea el borde visible al tiempo más cercano.
- **Pellizco**: se siguen los toques por `MouseInputSource::getIndex()`; con dos toques activos se cancela la edición en curso y el zoom sigue la distancia entre dedos, manteniendo fijo el instante bajo el punto medio. También `mouseMagnify` y la rueda para pruebas de escritorio.
- **Medidores**: el motor guarda el pico por pista en `mixSegment` y el pico de la salida en vivo al entrar en `processOutputMix`; `getState` los entrega y los pone a cero (`exchange`). La vista suaviza la caída.
- **Grabar**: la pista seleccionada es el destino; el botón grande envía `record` con ese índice o `stop` si ya graba. La región roja va del inicio de captura (0 o el punch in) al cabezal.
- **Paneles**: se crean al abrirse y se destruyen al cerrarse; mientras están abiertos, `apply()` les pasa el modelo para refrescar valores sin pisar un deslizador en uso.
- **Estética**: fondo negro, carriles gris oscuro, paleta de 6 colores por pista, tarjetas redondeadas, fuentes de la app (`Fonts::sans`).

## Risks / Trade-offs

- [Muchos gestos en un solo componente] → modos explícitos y vista previa local; si un gesto falla en el iPad se ajusta su umbral.
- [Pellizco en JUCE con varios toques] → también botones +/−, que siempre funcionan.
- [Pantalla completa oculta el ampli] → botón visible para volver; el audio no se detiene.
- [Compilación sin depuración local] → solo APIs de JUCE ya usadas en el proyecto; una sola compilación.
