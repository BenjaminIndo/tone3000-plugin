# Tasks

## 1. Motor de grabación y reproducción

- [ ] 1.1 Crear `plugin/include/TakeRecorder.h` con escritor WAV en hilo de fondo, FIFO, reproducción de tomas y exportación con cola; verificar que el CI compila el plugin
- [ ] 1.2 Enganchar en `Processor.h/.cpp` (`prepareToPlay`, `processBlock` entrada y salida); verificar en el iPad que el audio en vivo sigue igual sin usar el grabador

## 2. Backend y pantalla

- [ ] 2.1 Añadir `getRecorderState` y `recorderCommand` a `Backend.h` e implementarlos en `ProcessorBackend`; verificar que compila en el CI
- [ ] 2.2 Crear `RecorderView` (grabar, parar, lista, reproducir, bucle, posición, exportar, borrar), botón en `PluginHeader` y takeover en `PluginRoot`; verificar en el iPad que se abre y cierra sin tapar la cadena

## 3. Prueba en el iPad

- [ ] 3.1 Grabar 30 s, comprobar en Archivos que existen la toma limpia y la "(amp)"; verificar que se abren en GarageBand
- [ ] 3.2 Reproducir la toma y cambiar de modelo NAM en vivo; verificar que el sonido cambia sin cortes
- [ ] 3.3 Exportar con amp y verificar que el archivo nuevo incluye la cola del delay o reverb
