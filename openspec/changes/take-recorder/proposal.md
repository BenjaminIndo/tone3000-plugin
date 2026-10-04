# Proposal

## Why

La app independiente de iPad no se puede usar como plugin en GarageBand (no hay AUv3), así que no hay forma de grabar lo que se toca. Se quiere grabar la guitarra y poder cambiar el amplificador después, sin volver a tocar.

## What Changes

- Grabar siempre la guitarra **limpia** (entrada de la interfaz, antes de ganancia y cadena) en un WAV mono. Opcionalmente grabar a la vez el sonido ya procesado (WAV "amp").
- Pantalla "Grabaciones" desde un botón en la barra superior: grabar/parar, lista de tomas, reproducir una toma **a través de la cadena actual** (reamp), repetir en bucle, mover la posición.
- Mientras una toma suena se puede cambiar el modelo NAM, la IR y los parámetros, y se oye el cambio al instante.
- "Exportar con amp": reproduce la toma una vez a tiempo real con la cadena actual y guarda el resultado en un WAV estéreo/mono, con una cola de 2 s para delays y reverbs.
- Los archivos viven en la carpeta Documentos de la app (`Recordings`), visible en la app Archivos del iPad para importarlos en GarageBand.

Fuera de alcance: AUv3, edición de tomas, metrónomo o pista de acompañamiento, exportar más rápido que tiempo real, pedales (cambio aparte).

## Capabilities

### New Capabilities
- `take-recording`: grabación de tomas limpias, reproducción por la cadena (reamp) y exportación con amp.

### Modified Capabilities

## Impact

- Nuevo `plugin/include/TakeRecorder.h` (solo cabecera); enganches en `Processor.h/.cpp` (`prepareToPlay` y dos puntos de `processBlock`).
- UI: `Backend.h`, `ProcessorBackend.*`, nuevo `RecorderView`, botón en `PluginHeader`, takeover en `PluginRoot`.
- Sin dependencias nuevas ni cambios de CMake (todo en cabeceras y los `.cpp` existentes; la UI nueva se añade a la lista de fuentes si no es por glob).
