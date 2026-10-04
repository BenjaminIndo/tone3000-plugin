# Design

## Context

`TONE3000Processor::processBlock` (Processor.cpp) pliega la entrada (modo mono/estéreo), aplica ganancia y puerta, ejecuta la cadena NAM/IR y termina en el nivel de salida. JUCE es 9.0.3: se evita la API de lectores y escritores de formato que pudo cambiar y se usan solo piezas estables. No hay compilador local de C++ en el entorno de desarrollo; la verificación es el CI y el iPad, así que se prioriza código simple.

## Goals / Non-Goals

**Goals:**
- Capturar la señal limpia y la procesada sin afectar al hilo de audio.
- Reproducir una toma en lugar de la entrada, antes de la ganancia, para que todo lo demás actúe igual.
- Compilar una sola vez: pocas APIs, widgets estándar de JUCE en la UI.

**Non-Goals:** AUv3, edición, exportación más rápida que tiempo real, pedales.

## Decisions

- **`TakeRecorder.h` solo cabecera**, un miembro del procesador. Evita tocar CMake.
- **Puntos de enganche**: `recorder.processInput(buffer)` justo después del pliegue de entrada y antes de la ganancia (reemplaza la señal al reproducir; captura el canal 0 al grabar); `recorder.processOutput(buffer)` al final de `processBlock` (captura la salida final).
- **Escritura de WAV propia** (cabecera de 44 bytes + PCM 24 bits) con `juce::FileOutputStream`, en un hilo de fondo (`juce::Thread`) alimentado por `juce::AbstractFifo`. El hilo de audio solo copia a la FIFO (sin reservar ni bloquear); si la FIFO se llena se cuenta una pérdida. Alternativa descartada: `AudioFormatWriter::ThreadedWriter`, por incertidumbre de API en JUCE 9.
- **Lectura de tomas** con `AudioFormatManager` (mismo patrón que ya usa el repo), a mono; si la frecuencia difiere de la del motor se remuestrea linealmente al cargar. La toma completa vive en memoria (3 min mono ≈ 35 MB en float a 48 kHz; se avisa y limita a 10 min).
- **Publicación de la toma al hilo de audio** con un puntero atómico a un objeto inmutable; las tomas retiradas se liberan después en el hilo de mensajes.
- **Exportar con amp** = reproducir una vez + capturar la salida; al llegar al final se sigue capturando 2 s. Un `juce::Timer` en el hilo de mensajes cierra los archivos y publica el estado.
- **Carpeta**: `userDocumentsDirectory/Recordings`. En iOS es el Documents de la app, ya visible en Archivos (`FILE_SHARING_ENABLED`).
- **UI**: `Backend` gana dos métodos no puros con valor por defecto (`getRecorderState`, `recorderCommand`) para no tocar el backend simulado del testbed. Pantalla `RecorderView` como el afinador (takeover), con botones, deslizador y lista estándar de JUCE.

## Risks / Trade-offs

- [Fallo de compilación por APIs de JUCE 9] → usar solo tipos estables y revisar nombres con el código existente.
- [Tomas largas consumen memoria] → límite de 10 minutos al cargar y aviso.
- [Cabecera de la barra superior apretada con el botón nuevo] → separación pequeña; si desborda, ajustar con una prueba visual en el iPad.
- [Grabar mientras se reproduce] → no permitido; se desactiva el botón.
