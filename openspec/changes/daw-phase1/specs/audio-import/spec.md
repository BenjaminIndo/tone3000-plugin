# Spec Delta

## Purpose
Permite usar audio que el usuario ya tiene, como la canción original de un cover, como una pista más del proyecto.

## ADDED Requirements

### Requirement: Importar un archivo como pista
La app SHALL permitir elegir un archivo de audio del dispositivo, copiarlo a su carpeta y cargarlo en una pista como audio estéreo a la frecuencia del motor, con un máximo de 10 minutos.

#### Scenario: Importar una canción
- **WHEN** el usuario pulsa Import en una pista y elige un MP3, M4A, WAV o AIFF
- **THEN** la pista muestra la forma de onda y suena con el resto, con volumen, mute, solo y paneo

#### Scenario: Archivo no soportado
- **WHEN** el archivo no se puede leer o decodificar
- **THEN** se muestra un mensaje claro y la pista conserva su contenido anterior

#### Scenario: Audio protegido
- **WHEN** el archivo está protegido por DRM y no se puede decodificar
- **THEN** se informa que ese archivo no se puede usar

### Requirement: Reutilizar la copia
Al reabrir el proyecto, la pista importada SHALL cargarse desde la copia guardada, sin volver a pedir el archivo original.

#### Scenario: Reabrir
- **WHEN** el usuario cierra y reabre la app
- **THEN** la pista importada sigue en su lugar con sus ajustes
