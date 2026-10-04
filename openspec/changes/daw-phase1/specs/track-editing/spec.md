# Spec Delta

## Purpose
Da las herramientas mínimas de edición para armar y practicar un cover: recortar, desplazar, panear, ver la forma de onda, repetir un tramo y regrabar solo una parte.

## ADDED Requirements

### Requirement: Recorte no destructivo
La app SHALL permitir recortar el inicio y el final de una pista sin modificar su archivo, y SHALL aplicar el recorte en la reproducción y en la mezcla exportada.

#### Scenario: Saltar una intro
- **WHEN** el usuario recorta el inicio de la canción importada
- **THEN** la pista suena desde ese punto y la parte recortada no se oye ni se mezcla

### Requirement: Posición de inicio y paneo
Cada pista SHALL tener una posición de inicio en la línea de tiempo y un paneo izquierda-derecha, aplicados en la reproducción y en la mezcla.

#### Scenario: Entrar tarde
- **WHEN** el usuario fija el inicio de una pista a 8 segundos
- **THEN** la pista empieza a sonar a los 8 segundos del transporte

### Requirement: Forma de onda
La app SHALL dibujar la forma de onda de cada pista cargada, con la posición de reproducción y los marcadores de recorte, bucle y punch.

#### Scenario: Ver el audio
- **WHEN** una pista tiene audio
- **THEN** se ve su forma de onda a escala de toda la duración del proyecto

### Requirement: Región de bucle
La app SHALL permitir definir una región de bucle y repetirla mientras el bucle esté activo.

#### Scenario: Practicar un tramo
- **WHEN** el bucle está activo con una región de 4 segundos
- **THEN** la reproducción vuelve al inicio de la región al llegar al final, sin pausa audible

### Requirement: Punch in/out
La app SHALL permitir grabar solo entre dos marcas sobre una pista que ya tiene audio, conservando el resto, con un fundido corto en los bordes del tramo nuevo.

#### Scenario: Regrabar un compás
- **WHEN** el usuario marca punch in y punch out y graba sobre la pista 2
- **THEN** la pista 2 conserva su audio fuera de las marcas y queda con el tramo nuevo dentro, sin clics en los bordes
