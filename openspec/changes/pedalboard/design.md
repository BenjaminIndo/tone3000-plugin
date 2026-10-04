# Design

## Context

La cadena es una lista de bloques NAM/IR con muchas piezas ligadas (historial, presets, interfaz de galería, oversampling). Antes de ella ya hay etapas de entrada fijas (puerta de ruido, pitch shift) que se controlan con parámetros del procesador, no con bloques. Las etapas de salida (ecualizador de tono, nivel) también son fijas.

## Goals / Non-Goals

**Goals:** pedales útiles con poco riesgo de integración, guardados en presets, sin chasquidos.

**Non-Goals:** orden libre, varias instancias, más tipos de pedal, MIDI.

## Decisions

- **Etapas fijas, no bloques de la cadena**: el compresor va después de puerta y pitch y antes de la cadena; delay y reverb van después del ecualizador de tono y antes del nivel de salida. Así se evita tocar `ChainBlock`, el historial de la cadena, la galería y la serialización de bloques. Alternativa descartada: un tipo de bloque `PEDAL` arrastrable; es más flexible pero toca muchas piezas y no se puede depurar localmente.
- **Parámetros del procesador** (ids 44 a 59, unidades reales dB/ms/Hz) por el mismo camino que puerta y pitch: se guardan con el estado y, añadidos a `presetParameterIds`, con los presets.
- **DSP propio en `Pedals.h`**: compresor de alimentación hacia adelante con codo suave y envolvente en dB, mezcla paralela; delay con línea circular, interpolación lineal, suavizado del tiempo y filtro pasabajos en la realimentación; reverb con `juce::Reverb` (Freeverb) solo húmeda, sumada a la señal seca.
- **Transiciones**: el compresor hace un fundido de 10 ms al encender o apagar. Delay y reverb siguen corriendo con entrada silenciosa tras apagarse (8 s y 10 s) para que la cola termine, y luego se limpian y quedan inactivos.
- **Lectura de parámetros** en `processBlock` directamente de los atómicos, sin caché adicional.
- **Interfaz**: `PedalsView` con widgets estándar de JUCE (perillas rotativas y botones), ligada a `Backend::parameter()` con gestos de cambio para el anfitrión, y un sondeo a 10 Hz que sigue cambios externos. Botón nuevo en la barra, takeover como el afinador y la grabadora, excluyentes entre sí.

## Risks / Trade-offs

- [Orden fijo limita el uso] → Aceptado en esta versión; el orden libre sería un cambio posterior.
- [Interfaz sin pulir] → Widgets estándar; se pule después de probar en el iPad.
- [CPU del A13] → Costo bajo (un par de funciones transcendentes por muestra en el compresor); medir con el indicador de CPU de la app.
- [Barra superior apretada] → Quedan unos 45 px de margen calculado; verificar en el iPad.
- [Errores de compilación por JUCE 9] → Solo APIs estables (`Reverb`, `Slider`, `TextButton`); una sola compilación junto con el compartir WAV.
