# Spec Delta

## Purpose
Permite construir una pieza con varias guitarras grabadas una encima de otra y exportar la mezcla, dentro de la app.

## ADDED Requirements

### Requirement: Grabar una pista encima de otras
La app SHALL permitir grabar una pista mientras suenan las demás, y SHALL guardar la grabación como toma (limpia y procesada) asignada a esa pista.

#### Scenario: Overdub
- **WHEN** la pista 1 tiene una grabación y el usuario pulsa REC en la pista 2
- **THEN** la pista 1 suena mientras se graba la 2, y al parar la 2 queda asignada y lista para sonar

#### Scenario: Reemplazar una pista
- **WHEN** se graba sobre una pista que ya tiene audio
- **THEN** esa pista no suena durante la grabación y su audio se reemplaza al terminar

### Requirement: Alineación por latencia
La pista grabada SHALL alinearse con las demás compensando la latencia de entrada y salida estimada del dispositivo, y SHALL permitir ajustarla manualmente por pista entre -300 y +300 ms.

#### Scenario: Ajuste fino
- **WHEN** el usuario mueve el nudge de una pista
- **THEN** la pista suena desplazada ese tiempo respecto a las demás

### Requirement: Mezclador básico
Cada pista SHALL tener volumen, silencio y solo, aplicados en vivo y en la mezcla exportada.

#### Scenario: Solo
- **WHEN** el usuario activa solo en una pista
- **THEN** solo suenan las pistas con solo activo

### Requirement: Transporte
La app SHALL ofrecer reproducir, parar, volver al inicio, bucle y mover la posición.

#### Scenario: Bucle
- **WHEN** el bucle está activo y la reproducción llega al final
- **THEN** vuelve al inicio sin pausa audible

### Requirement: Exportar la mezcla
La app SHALL sumar las pistas audibles a un archivo WAV (o MP3) sin recortes por saturación, y SHALL abrir la hoja de compartir al terminar.

#### Scenario: Mix down
- **WHEN** el usuario pulsa Mix down
- **THEN** se crea un archivo de mezcla con la duración de la pista más larga y se abre la hoja de compartir

### Requirement: Proyecto persistente
La asignación de tomas a pistas y sus volúmenes, mute, solo y nudges SHALL conservarse al cerrar y reabrir la app.

#### Scenario: Reabrir
- **WHEN** el usuario cierra la app y la vuelve a abrir
- **THEN** las pistas siguen asignadas con sus ajustes
