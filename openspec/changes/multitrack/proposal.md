# Proposal

## Why

Hoy solo se graban tomas sueltas. Se quiere grabar una guitarra, luego otra encima mientras suena la primera, y mezclar, como en un DAW sencillo, sin salir de la app.

## What Changes

- Pantalla "Tracks" (se abre desde Recordings): 6 pistas con grabar, volumen, silencio (mute), solo, ajuste de latencia (nudge) y borrar.
- Las pistas suenan juntas; se puede grabar una pista nueva mientras suenan las demás (overdub). La guitarra en vivo pasa por el equipo como siempre.
- Cada pista reproduce el archivo ya procesado ("amp") de su toma, mezclado después del equipo, así que no se mezcla dentro de la toma nueva.
- La pista nueva se alinea con una estimación de la latencia del dispositivo, ajustable por pista.
- Transporte: play, stop, volver al inicio, bucle y posición.
- "Mix down": suma las pistas a un WAV (o MP3) y abre la hoja de compartir.
- El proyecto (qué toma va en cada pista y sus ajustes) se guarda y se reabre.

Fuera de alcance: edición de audio, línea de tiempo, efectos por pista, más de 6 pistas, mezcla más rápida que tiempo real para grabar.

## Capabilities

### New Capabilities
- `multitrack`: pistas simultáneas con grabación encima, mezclador básico y mezcla exportable.

### Modified Capabilities

## Impact

- Nuevo `plugin/include/Multitrack.h`; `IosAudioRoute` gana `roundTripLatencyMs()`.
- `Processor.h/.cpp`: miembro, `prepare` y una llamada al final de `processBlock`.
- `TakeRecorder.h`: accesor `lastRecordedName`; las mezclas no aparecen como tomas.
- UI: `MultitrackView`, botón "Tracks >" en `RecorderView`, takeover en `PluginRoot`.
