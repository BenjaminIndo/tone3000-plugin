# Spec Delta

## Purpose
Da al multipista una experiencia de estudio táctil, cercana a la vista de pistas de GarageBand, para grabar y armar covers con el dedo.

## ADDED Requirements

### Requirement: Estudio a pantalla completa
El estudio SHALL ocupar toda la ventana de la app y SHALL ofrecer volver al ampli y a la lista de tomas sin detener la reproducción.

#### Scenario: Volver al ampli
- **WHEN** el usuario pulsa el botón del ampli mientras suena la canción
- **THEN** aparece la pantalla del ampli y la canción sigue sonando

### Requirement: Barra de control y visor
La app SHALL mostrar una barra con retroceder, play/stop, grabar, bucle, metrónomo y cuenta atrás como iconos táctiles de al menos 44 puntos, y un visor con la posición en compás.tiempo y en minutos:segundos, el tempo y el compás.

#### Scenario: Posición musical
- **WHEN** la reproducción pasa por el segundo tiempo del compás 5 a 120 BPM en 4/4
- **THEN** el visor muestra 5.2 y el tiempo correspondiente

#### Scenario: Grabar en la pista seleccionada
- **WHEN** el usuario selecciona la pista 3 y pulsa grabar
- **THEN** se graba en la pista 3 y su carril muestra una región roja que crece

### Requirement: Cabeceras de pista
Cada pista SHALL mostrar color, nombre, duración, mute, solo, volumen, un medidor de nivel y acceso a sus ajustes; la pista seleccionada SHALL destacarse.

#### Scenario: Medidor
- **WHEN** una pista suena
- **THEN** su medidor sube con el nivel, y la pista seleccionada muestra también el nivel de la guitarra en vivo

### Requirement: Línea de tiempo editable con el dedo
La línea de tiempo SHALL mostrar una regla con compases, la rejilla, las regiones con su forma de onda y el cabezal, y SHALL permitir mover una región arrastrándola y recortarla arrastrando sus bordes, con ajuste opcional a los tiempos.

#### Scenario: Recortar la intro
- **WHEN** el usuario arrastra el borde izquierdo de la región de la canción hacia la derecha
- **THEN** la región se acorta por la izquierda, el audio restante queda en su lugar y el cambio se guarda al soltar

#### Scenario: Mover con ajuste a la rejilla
- **WHEN** el ajuste a la rejilla está activo y el usuario arrastra una región
- **THEN** su inicio queda alineado con el tiempo más cercano

### Requirement: Bucle y punch en la regla
La app SHALL permitir definir y ajustar el bucle arrastrando en una franja amarilla bajo la regla, y el punch en una franja roja, activándolos al definirlos.

#### Scenario: Definir un bucle
- **WHEN** el usuario arrastra en la franja amarilla entre los compases 9 y 13
- **THEN** el bucle queda activo en ese tramo y la reproducción lo repite

### Requirement: Desplazamiento y zoom
La línea de tiempo SHALL desplazarse arrastrando el fondo, acercarse o alejarse con un pellizco o con botones, y seguir al cabezal durante la reproducción.

#### Scenario: Seguir el cabezal
- **WHEN** el cabezal llega al borde derecho de la vista durante la reproducción
- **THEN** la vista avanza para mantenerlo visible

### Requirement: Paneles flotantes
Los ajustes de la canción, de la pista y de la mezcla SHALL abrirse en paneles flotantes que se cierran al tocar fuera.

#### Scenario: Ajustes de pista
- **WHEN** el usuario toca el botón de ajustes de una pista
- **THEN** se abre un panel con volumen, paneo, compensación de latencia, importar y borrar para esa pista
