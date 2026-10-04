# Tasks

## 1. Modelo de pista y mezcla

- [ ] 1.1 Ampliar la pista (`source`, recorte, inicio, paneo), la lectura en el hilo de audio y la mezcla exportada, y migrar `project.json`; verificar que el CI compila y que un proyecto existente abre con las pistas intactas
- [ ] 1.2 Comandos de recorte, inicio y paneo en el backend; verificar que el CI compila y en el iPad que una pista recortada suena desde el punto elegido

## 2. Metrónomo y cuenta atrás

- [ ] 2.1 Generador de clic en el hilo de audio, con tempo, compás, acento y volumen; verificar que el CI compila y que en el iPad el clic no deriva en 2 minutos
- [ ] 2.2 Compuerta de captura en `TakeRecorder` y cuenta atrás con posición negativa; verificar en el iPad que la grabación empieza justo en el primer tiempo y que el clic no queda en el archivo
- [ ] 2.3 Tap tempo en la pantalla; verificar que cuatro pulsaciones al ritmo fijan un tempo cercano

## 3. Importar audio

- [ ] 3.1 Selector de archivos con URLs, copia a `Recordings/Imports/` y carga como pista; verificar en el iPad con WAV, MP3 y M4A, y con un archivo no soportado que se muestra el error
- [ ] 3.2 Reabrir el proyecto con una pista importada; verificar que se carga de la copia

## 4. Bucle y punch

- [ ] 4.1 Región de bucle y salto sin pausa; verificar en el iPad que repetir 4 segundos no tiene pausa ni clic audible
- [ ] 4.2 Punch in/out con la compuerta y combinación con fundidos de 5 ms; verificar en el iPad que fuera del tramo la pista queda igual y los bordes no hacen clic

## 5. Pantalla

- [ ] 5.1 Componente de forma de onda y cálculo de picos al cargar; verificar que se dibuja para una toma y para una canción importada
- [ ] 5.2 Nueva disposición (transporte y metrónomo arriba, pistas en `juce::Viewport`, mezcla abajo); verificar en el iPad que todo cabe y se maneja con el dedo

## 6. Prueba integral en el iPad

- [ ] 6.1 Importar una canción, recortar la intro, fijar el tempo con tap tempo, grabar una guitarra con cuenta atrás, regrabar un compás con punch y hacer Mix down; verificar que el resultado suena sincronizado y sin clics
