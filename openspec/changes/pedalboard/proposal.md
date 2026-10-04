# Proposal

## Why

La cadena actual solo tiene bloques NAM e IR, más puerta de ruido y afinador. Para acercarse a un "NAM Amp & pedalboard" (como ToneRig) faltan los pedales típicos: compresor antes del ampli, y delay y reverb después.

## What Changes

- Tres pedales con interruptor (on/off) y sus perillas:
  - **Compresor** (antes del ampli): umbral, ratio, nivel, ataque, relajación y mezcla (compresión paralela).
  - **Delay** (después del ampli): tiempo, repeticiones, mezcla y tono.
  - **Reverb** (después del ampli): tamaño, amortiguación y mezcla.
- Pantalla "Pedales" desde un botón de la barra superior.
- Los pedales se guardan en los presets y en el estado de la app; todo apagado por defecto.
- Al apagar delay o reverb, su cola termina de sonar (no se corta de golpe).
- La grabadora de tomas también captura los pedales (van antes de la salida).

Fuera de alcance: orden libre o varios pedales del mismo tipo, más tipos (overdrive, chorus), arrastrar pedales como los bloques de la cadena, mapeo MIDI.

## Capabilities

### New Capabilities
- `pedalboard`: compresor, delay y reverb con interruptor y controles, integrados en la señal y en los presets.

### Modified Capabilities

## Impact

- Nuevo `plugin/include/Pedals.h` (DSP, solo cabecera).
- `Processor.h/.cpp`: parámetros nuevos (ids 44 a 59), lectura en `processBlock`, preparación en `prepareToPlay`; `ProcessorPresets.cpp`: ids guardados en presets.
- UI: nueva `PedalsView`, botón en `PluginHeader`, takeover en `PluginRoot`.
- Sin dependencias nuevas ni cambios de CMake.
