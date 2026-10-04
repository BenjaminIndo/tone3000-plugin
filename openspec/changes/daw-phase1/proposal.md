# Proposal

## Why

El multipista actual graba pistas encima de otras, pero para grabar covers faltan: un metrónomo, poder usar la canción original como pista, recortarla, practicar una frase en bucle y regrabar solo un tramo fallido.

## What Changes

- **Metrónomo**: tempo (BPM), compás, acento en el primer tiempo, volumen, tap tempo y cuenta atrás de 1 o 2 compases antes de grabar. El clic no se graba.
- **Importar audio**: botón "Import" por pista; abre el selector de archivos de iOS, copia el archivo a la carpeta de la app y lo carga como pista (WAV, AIFF y, si iOS los decodifica, MP3 y M4A).
- **Edición no destructiva por pista**: recorte de inicio y final, posición de inicio en la línea de tiempo y paneo (izquierda/derecha).
- **Forma de onda** dibujada en cada pista, con los marcadores de recorte, bucle y punch.
- **Región de bucle** para practicar un tramo repetido.
- **Punch in/out**: grabar solo entre dos marcas sobre una pista existente; el tramo nuevo se funde (5 ms) con el audio anterior.
- La lista de pistas pasa a ser desplazable y la pantalla se reorganiza (barra de transporte y metrónomo arriba, pistas en medio, mezcla abajo).
- El proyecto guarda también tempo, compás, bucle, y por pista origen, recortes, inicio y paneo.

Fuera de alcance: cortar una pista en varios clips (split), arrastrar clips, línea de tiempo con rejilla, deshacer/rehacer, medidores, efectos por pista, cambio de velocidad o de tono, más de 6 pistas, importar audio protegido (por ejemplo Spotify).

## Capabilities

### New Capabilities
- `metronome`: clic sincronizado con el transporte, cuenta atrás y tap tempo.
- `audio-import`: traer archivos de audio del dispositivo como pistas.
- `track-editing`: recorte, inicio, paneo, forma de onda, bucle y punch in/out.

### Modified Capabilities

## Impact

- `plugin/include/Multitrack.h`: modelo de pista ampliado, lectura con recorte/inicio/paneo, metrónomo, bucle, punch, importación y migración del `project.json`.
- `plugin/include/TakeRecorder.h`: compuerta de captura (empezar a grabar en un instante exacto del transporte).
- `plugin/ui/views/MultitrackView.*`: nueva disposición con lista desplazable y componente de forma de onda; selector de archivos.
- Sin dependencias nuevas ni cambios de CMake.
