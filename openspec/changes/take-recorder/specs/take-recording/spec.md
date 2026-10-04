# Spec Delta

## Purpose
Permite grabar la guitarra limpia, volver a tocarla por cualquier amplificador de la cadena y exportar el resultado como audio, sin depender de un DAW dentro del iPad.

## ADDED Requirements

### Requirement: Grabar una toma limpia
La app SHALL grabar la entrada de guitarra sin procesar a un archivo WAV mono de 24 bits cada vez que el usuario inicia una grabación, y SHALL grabar a la vez el sonido procesado en un segundo archivo.

#### Scenario: Grabación normal
- **WHEN** el usuario pulsa Grabar, toca y pulsa Parar
- **THEN** aparecen dos archivos en la carpeta de grabaciones: la toma limpia y la toma con amp, con el mismo nombre base

#### Scenario: El audio no se corta
- **WHEN** se graba durante 10 minutos
- **THEN** el sonido en vivo no tiene cortes causados por la grabación

### Requirement: Reproducir una toma por la cadena
La app SHALL reproducir la toma limpia seleccionada como si fuera la entrada de la guitarra, de modo que pasa por el ruido, la ganancia, la cadena y la salida actuales.

#### Scenario: Cambiar el amplificador en vivo
- **WHEN** una toma suena y el usuario carga otro modelo NAM
- **THEN** la toma sigue sonando y se oye el nuevo amplificador sin reiniciarla

#### Scenario: Bucle y posición
- **WHEN** el usuario activa el bucle o mueve la posición
- **THEN** la toma repite al terminar o salta a la posición elegida

### Requirement: Exportar con amp
La app SHALL exportar la toma seleccionada procesada por la cadena actual a un WAV, a tiempo real, con una cola de 2 segundos.

#### Scenario: Exportación
- **WHEN** el usuario pulsa Exportar con amp
- **THEN** la toma suena una vez de principio a fin y, al terminar la cola, aparece un archivo nuevo "(amp)" y la app lo indica

### Requirement: Archivos accesibles
Las tomas SHALL guardarse en una carpeta visible desde la app Archivos del iPad, con nombres con fecha y hora.

#### Scenario: Importar en GarageBand
- **WHEN** el usuario abre Archivos > TONE3000 > Recordings
- **THEN** puede compartir o abrir los WAV en otra app

### Requirement: Estados claros
La pantalla SHALL mostrar si graba, reproduce o exporta, el tiempo transcurrido y errores de disco o de lectura.

#### Scenario: Error al guardar
- **WHEN** no se puede crear el archivo
- **THEN** se muestra un mensaje y la grabación no queda a medias en silencio

### Requirement: Compartir un WAV
La app SHALL permitir compartir la toma limpia, la toma con amp o la última exportación como WAV mediante la hoja de compartir del sistema, y SHALL abrirla automáticamente al terminar una exportación.

#### Scenario: Exportar y compartir
- **WHEN** termina una exportación con amp
- **THEN** se abre la hoja de compartir con el WAV nuevo

#### Scenario: Compartir una toma existente
- **WHEN** el usuario elige una toma y pulsa "Clean" o "Amped"
- **THEN** se abre la hoja de compartir con ese archivo
