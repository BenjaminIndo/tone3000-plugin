# Design

## Context

`TakeRecorder` ya graba la toma limpia y la procesada y escribe WAV sin bloquear el audio. `processBlock` termina con el nivel de salida y la captura del grabador. Los modelos NAM y la cadena no se tocan.

## Goals / Non-Goals

**Goals:** overdub fiable y mezcla simple, reutilizando el grabador. **Non-Goals:** edición, línea de tiempo, efectos por pista.

## Decisions

- **Las pistas reproducen el archivo "(amp)"**: cada pista es una toma ya procesada. Así no hay un equipo por pista (la cadena es una sola) y la mezcla no necesita pasar por la cadena. Cambiar el sonido de una pista = reprocesar su toma limpia (función de reamp ya existente) y volver a grabar.
- **Mezcla después del equipo**: `Multitrack::processOutputMix` se llama al final de `processBlock`, después de `takeRecorder.processOutput`, para que las pistas de fondo no entren en la toma nueva. El nivel de salida general no afecta a las pistas.
- **Estructura**: `Multitrack.h` solo cabecera; punteros atómicos a buffers estéreo inmutables, ganancias y desplazamientos atómicos por pista. Los buffers retirados se conservan unas cargas más.
- **Grabar**: `Multitrack` fuerza "grabar también con amp" y llama a `TakeRecorder` ("record" y "stop"); al parar toma `lastRecordedName`, carga el archivo "(amp)" y lo asigna. La reproducción de fondo arranca justo después de empezar la grabación (diferencia de un bloque como máximo, absorbida por el nudge).
- **Latencia**: `IosAudioRoute::roundTripLatencyMs()` = latencia de entrada + de salida + dos buffers de E/S; es el nudge inicial de una pista nueva (30 ms fuera de iOS). La pista se lee por delante de la posición ese tiempo.
- **Mezcla exportada**: suma en memoria en el hilo de mensajes (instantánea, sin la cadena), normaliza si pasa de 0,999 y escribe WAV de 24 bits; MP3 con el codificador existente. Los archivos "Mix ..." no se listan como tomas.
- **Proyecto**: `Recordings/project.json` con la toma, volumen, mute, solo y nudge de cada pista.
- **UI**: `MultitrackView` con widgets estándar; acceso desde "Tracks >" en la pantalla de grabaciones; sin botón nuevo en la barra superior (no hay espacio).

## Risks / Trade-offs

- [La alineación depende de la estimación de latencia] → nudge manual por pista.
- [Memoria: 6 pistas completas en RAM] → límite de 10 min por pista; pensado para tomas cortas.
- [Compilación sin depuración local] → una sola compilación al terminar; solo APIs estables de JUCE.
- [Cada pista fija su sonido al grabarse] → aceptado; reamp y volver a grabar si se quiere cambiar.
