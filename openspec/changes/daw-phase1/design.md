# Design

## Context

`Multitrack.h` guarda por pista una toma (archivo "(amp)"), volumen, mute, solo y nudge, y mezcla estéreo en `processOutputMix` con un puntero atómico a un buffer inmutable. `TakeRecorder` abre los escritores al recibir "record" y empieza a escribir de inmediato. La interfaz son seis filas de widgets estándar de JUCE en un área de 1024 por 352 px. Los archivos que elige el usuario en iOS llegan como URLs con permiso (ya se usa para los `.nam`); el módulo de formatos de JUCE en Apple decodifica también MP3 y M4A.

## Goals / Non-Goals

**Goals:** metrónomo exacto, importar canciones, edición no destructiva simple, bucle y punch, compilando una sola vez con APIs estables.

**Non-Goals:** clips múltiples, arrastrar, rejilla, deshacer, medidores, efectos por pista, estiramiento de tiempo.

## Decisions

- **Modelo de pista ampliado**: `source` (ruta relativa a la carpeta de grabaciones: el archivo "(amp)" de una toma o `Imports/<archivo>`), `trimStart`, `trimEnd`, `start` (posición en la línea de tiempo) y `pan`. La lectura en el hilo de audio es `índice = posición - start + trimStart`, válida solo dentro de `[trimStart, trimEnd)`; el paneo usa una ley de potencia constante. El `project.json` migra los campos que falten con valores por defecto, así que los proyectos actuales siguen abriendo.
- **Metrónomo en el hilo de audio**: contador de muestras ligado a la posición del transporte; el clic cae en múltiplos de `muestrasPorTiempo = frecuencia * 60 / BPM`, con un estallido corto de seno (1,5 kHz el primer tiempo, 1 kHz el resto) y caída exponencial de unos 30 ms, sintetizado muestra a muestra sin reservar memoria. Se suma después de la captura del grabador, así que nunca se graba. Tempo, compás, volumen y encendido son atómicos.
- **Cuenta atrás con posición negativa**: al pedir REC con cuenta atrás, el transporte empieza en `-compases * muestrasPorCompás`; las pistas ignoran índices negativos y el clic suena. La grabación empieza exactamente al cruzar la posición cero mediante una **compuerta de captura** en `TakeRecorder` (`setCaptureGate`): los escritores se abren al pulsar REC, pero el hilo de audio solo empuja datos mientras la compuerta esté abierta. Alternativa descartada: abrir los escritores en el instante del cruce, que exigiría trabajo de archivos en el hilo de audio.
- **Punch in/out con la misma compuerta**: la compuerta se abre al cruzar `punchIn` y se cierra en `punchOut`, de modo que el archivo grabado contiene solo el tramo. Al parar, en el hilo de mensajes se combina con el audio anterior de la pista: se copia el buffer anterior, se superpone el tramo nuevo con fundidos de 5 ms en los bordes y se escribe un archivo "(amp)" nuevo; el archivo limpio queda como el tramo grabado (el reamp sobre un punch se documenta como limitación).
- **Bucle**: región `[loopStart, loopEnd)` en muestras; al llegar al final con el bucle activo la posición salta al inicio dentro del mismo bloque, sin pausa. Grabando con bucle activo se desactiva el salto.
- **Importar**: `FileChooser` con resultados en URL (como "Load File" de los modelos); la URL se lee con `createInputStream` y se copia a `Recordings/Imports/`; la carga reutiliza el decodificador de formatos, remuestrea a la frecuencia del motor y limita a 10 minutos. Si no se puede decodificar (incluido DRM) se muestra un error y la pista queda como estaba.
- **Forma de onda**: al cargar una pista se calculan los picos mínimo y máximo por ventana; la pantalla los pide una vez por pista con un comando y los guarda, y un componente propio los dibuja con los marcadores. Evita mandar datos grandes en cada sondeo del estado.
- **Tap tempo en la interfaz**: promedia los últimos cuatro intervalos entre pulsaciones y descarta pausas de más de dos segundos.
- **Pantalla**: arriba, barra de transporte y metrónomo; en medio, `juce::Viewport` desplazable con una fila de 64 px por pista (controles y forma de onda); abajo, bucle, punch y mezcla. Se mantienen widgets estándar de JUCE.

## Risks / Trade-offs

- [La reorganización de la pantalla es el mayor riesgo de aspecto y de uso táctil] → widgets estándar y prueba en el iPad antes de pulir.
- [Punch con archivo limpio parcial] → limitación documentada; el reamp de esa pista usa solo el tramo.
- [MP3 y M4A pueden no decodificar en algún caso] → mensaje claro; WAV y AIFF siempre.
- [Memoria con canciones de varios minutos] → límite de 10 minutos por pista.
- [Compilación sin depuración local] → una sola compilación, APIs estables de JUCE, igual que en los cambios anteriores.
- [Exactitud del clic y la cuenta atrás dependen del contador de muestras] → se prueba grabando una pista contra otra y escuchando la deriva en 2 minutos.
