# Spec Delta

## Purpose
Da un pulso de referencia sincronizado con las pistas para grabar a tiempo, incluidos covers de canciones con tempo conocido.

## ADDED Requirements

### Requirement: Clic sincronizado
La app SHALL generar un clic con el tempo (30 a 300 BPM) y el compás elegidos, acentuando el primer tiempo, alineado con la posición del transporte, con volumen ajustable y activable o no.

#### Scenario: Reproducir con clic
- **WHEN** el metrónomo está activo y se reproduce o se graba
- **THEN** el clic suena en cada tiempo exacto del compás, sin deriva

#### Scenario: El clic no se graba
- **WHEN** se graba una pista con el metrónomo activo
- **THEN** el clic no aparece en la toma limpia ni en la toma con amp, ni en la mezcla exportada

### Requirement: Cuenta atrás
La app SHALL poder reproducir 1 o 2 compases de clic antes de que empiece la grabación y SHALL empezar a grabar exactamente en el primer tiempo posterior.

#### Scenario: Grabar con cuenta atrás
- **WHEN** el usuario pulsa REC con cuenta atrás de un compás
- **THEN** suena un compás de clic y la grabación empieza justo al terminar, junto con las demás pistas

### Requirement: Tap tempo
La app SHALL fijar el tempo a partir de golpes del usuario sobre un botón, promediando los últimos intervalos.

#### Scenario: Marcar el tempo
- **WHEN** el usuario pulsa el botón al ritmo de una canción al menos cuatro veces
- **THEN** el tempo se actualiza al promedio de los intervalos, redondeado a un decimal
