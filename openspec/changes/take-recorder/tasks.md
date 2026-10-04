# Tasks

## 1. Motor de grabación y reproducción

- [x] 1.1 Crear `plugin/include/TakeRecorder.h` con escritor WAV en hilo de fondo, FIFO, reproducción de tomas y exportación con cola; verificar que el CI compila el plugin
- [x] 1.2 Enganchar en `Processor.h/.cpp` (`prepareToPlay`, `processBlock` entrada y salida); verificar en el iPad que el audio en vivo sigue igual sin usar el grabador

## 2. Backend y pantalla

- [x] 2.1 Añadir `getRecorderState` y `recorderCommand` a `Backend.h` e implementarlos en `ProcessorBackend`; verificar que compila en el CI
- [x] 2.2 Crear `RecorderView` (grabar, parar, lista, reproducir, bucle, posición, exportar, borrar), botón en `PluginHeader` y takeover en `PluginRoot`; verificar en el iPad que se abre y cierra sin tapar la cadena

## 3. Prueba en el iPad

- [x] 3.1 Grabar 30 s, comprobar en Archivos que existen la toma limpia y la "(amp)"; verificar que se abren en GarageBand
- [x] 3.2 Reproducir la toma y cambiar de modelo NAM en vivo; verificar que el sonido cambia sin cortes
- [x] 3.3 Exportar con amp y verificar que el archivo nuevo incluye la cola del delay o reverb

## 4. Compartir como WAV

- [ ] 4.1 Botones "Clean", "Amped" y "Export" que abren la hoja de compartir de iOS con el WAV; al terminar una exportación se abre sola; verificar en el iPad que se puede enviar a Archivos o a GarageBand

## 5. MP3 y exportación rápida

- [ ] 5.1 Vendorizar shine (LGPL-2.0) y añadir el conmutador "Share as MP3"; verificar que el CI compila y que en el iPad el MP3 se abre en otra app
- [ ] 5.2 Exportación rápida (varias pasadas por callback según la carga) y cola adaptativa; verificar en el iPad que una toma de 30 s se exporta en menos de 30 s y que el WAV queda igual de bien
