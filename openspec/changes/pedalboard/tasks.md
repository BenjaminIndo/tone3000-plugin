# Tasks

## 1. DSP y parámetros

- [ ] 1.1 Crear `plugin/include/Pedals.h` con compresor, delay y reverb; verificar que el CI compila
- [ ] 1.2 Añadir los 16 parámetros (ids 44 a 59), lectura en `processBlock`, `prepare` en `prepareToPlay` y los ids en `presetParameterIds`; verificar que el CI compila

## 2. Pantalla

- [ ] 2.1 Crear `PedalsView` con interruptor y perillas por pedal, botón en `PluginHeader` y takeover en `PluginRoot` (excluyente con afinador y grabadora); verificar que el CI compila

## 3. Prueba en el iPad

- [ ] 3.1 Abrir la pantalla de pedales y comprobar que cabe en la barra superior y que las perillas responden
- [ ] 3.2 Encender el compresor con un umbral bajo y verificar que se oye la compresión; apagarlo y verificar que la señal queda igual y sin clics
- [ ] 3.3 Encender el delay y la reverb; apagarlos mientras suenan y verificar que la cola termina sin cortarse
- [ ] 3.4 Guardar un preset con pedales encendidos, cargar otro y volver; verificar que los valores se restauran
- [ ] 3.5 Medir el uso de CPU con los tres pedales y un modelo NAM estándar; verificar que no hay cortes
