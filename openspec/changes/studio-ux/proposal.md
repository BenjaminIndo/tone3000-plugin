# Proposal

## Why

El multipista funciona, pero su pantalla es pequeña, está llena de botones del mismo peso y se edita con deslizadores lejos de la pista. Para grabar covers con comodidad debe sentirse como la vista de pistas de GarageBand: grande, visual y manejada con el dedo sobre el audio.

## What Changes

- **Pantalla completa**: el estudio ocupa toda la ventana (sin barra superior ni faceplate), con un botón para volver al ampli y otro a las tomas. El transporte sigue sonando si se vuelve al ampli.
- **Barra de control** al estilo GarageBand: retroceder, play/stop, grabar (rojo, grande), bucle, metrónomo y cuenta atrás como iconos; un **visor** (LCD) con compás.tiempo, minutos:segundos, tempo y compás, que abre los ajustes de la canción.
- **Cabeceras de pista** a la izquierda: color propio, nombre, duración, mute y solo grandes, volumen, medidor y botón de ajustes de pista. La pista seleccionada es la que se graba.
- **Línea de tiempo** a la derecha: regla con compases, rejilla de compases y tiempos, regiones de color con su forma de onda y nombre, cabezal de reproducción y región que crece en rojo mientras se graba.
- **Edición directa**: arrastrar una región para moverla, arrastrar sus bordes para recortarla, con ajuste a la rejilla opcional; tocar la regla para mover el cabezal; arrastrar en la franja amarilla para definir el bucle y en la roja para el punch.
- **Desplazamiento y zoom**: arrastrar el fondo para desplazar, pellizcar o botones +/− para el zoom, y seguimiento automático del cabezal.
- **Medidores** por pista y de la entrada en vivo en la pista seleccionada.
- **Paneles flotantes** en lugar de filas fijas: ajustes de la canción (tempo, tap, compás, cuenta atrás, volumen del clic, ajuste a la rejilla), ajustes de pista (volumen, paneo, compensación de latencia, importar, borrar) y mezcla (WAV, MP3, compartir).
- Las seis pistas se ven siempre, a la altura que permita la pantalla.

Fuera de alcance: más de 6 pistas, varias regiones por pista, cortar (split), deshacer, efectos por pista, renombrar pistas.

## Capabilities

### New Capabilities
- `studio-ux`: experiencia de estudio a pantalla completa con transporte, línea de tiempo editable con el dedo, cabeceras de pista con medidores y paneles flotantes.

### Modified Capabilities

## Impact

- `plugin/ui/views/MultitrackView.*`: reescritura completa (barra de control, visor, cabeceras, línea de tiempo, paneles).
- `plugin/include/Multitrack.h`: medidores por pista y en vivo, nombre de pista, comandos `region`, `loopRange` y `punchRange`.
- `plugin/ui/views/PluginRoot.cpp`: el estudio cubre toda la ventana.
- Sin dependencias nuevas ni cambios de CMake.
