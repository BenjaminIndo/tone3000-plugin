# Tasks

## 1. Motor y proyecto

- [ ] 1.1 Crear `plugin/include/Multitrack.h` (mezcla, grabación sobre pistas, mezcla exportada, proyecto) y `roundTripLatencyMs`; verificar que el CI compila
- [ ] 1.2 Enganchar en `Processor` y `Backend`/`ProcessorBackend`; verificar que el CI compila

## 2. Pantalla

- [ ] 2.1 Crear `MultitrackView`, botón "Tracks >" y takeover en `PluginRoot`; verificar que el CI compila

## 3. Prueba en el iPad

- [ ] 3.1 Grabar la pista 1 (30 s), luego la pista 2 encima; verificar que la 1 suena durante la grabación y que no queda dentro de la 2
- [ ] 3.2 Comprobar la alineación y ajustar el nudge si hace falta; verificar que la mezcla suena sincronizada
- [ ] 3.3 Probar volumen, mute, solo, bucle y posición; verificar que responden en vivo
- [ ] 3.4 Mix down a WAV y a MP3; verificar que se abre la hoja de compartir y el archivo se oye bien
- [ ] 3.5 Cerrar y reabrir la app; verificar que las pistas y sus ajustes siguen
