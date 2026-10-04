# Spec Delta

## Purpose
Ofrece pedales de efectos básicos (compresor, delay y reverb) que se encienden y apagan y se ajustan en vivo, junto a la cadena de amplificadores.

## ADDED Requirements

### Requirement: Compresor antes del ampli
La app SHALL ofrecer un compresor con interruptor, umbral, ratio, ataque, relajación, nivel y mezcla, aplicado antes de la cadena de amplificadores.

#### Scenario: Compresión audible
- **WHEN** el compresor está encendido con umbral bajo y ratio alto
- **THEN** los picos de la guitarra se reducen y el nivel general se vuelve más parejo

#### Scenario: Apagado transparente
- **WHEN** el compresor está apagado
- **THEN** la señal pasa sin cambios

### Requirement: Delay y reverb después del ampli
La app SHALL ofrecer un delay (tiempo, repeticiones, mezcla, tono) y una reverb (tamaño, amortiguación, mezcla), con interruptor cada uno, aplicados después de la cadena y del ecualizador.

#### Scenario: Delay
- **WHEN** el delay está encendido
- **THEN** se oyen repeticiones separadas por el tiempo elegido que decaen según las repeticiones

#### Scenario: Cola al apagar
- **WHEN** el usuario apaga el delay o la reverb mientras suenan
- **THEN** la cola termina de sonar sin cortarse de golpe, sin añadir nueva señal

### Requirement: Sin chasquidos
Encender o apagar un pedal SHALL hacerse con una transición corta, sin chasquidos audibles.

#### Scenario: Interruptor en vivo
- **WHEN** se enciende o apaga un pedal mientras se toca
- **THEN** no hay clics audibles

### Requirement: Pantalla de pedales
La app SHALL mostrar los tres pedales con su interruptor y perillas, que siguen cambios externos (presets), y SHALL permitir restaurar una perilla a su valor por defecto con doble toque.

#### Scenario: Cargar un preset
- **WHEN** se carga un preset con distintos valores de pedales
- **THEN** la pantalla de pedales refleja los valores nuevos

### Requirement: Persistencia
Los parámetros de los pedales SHALL guardarse y restaurarse con los presets y con el estado de la app, con todos los pedales apagados por defecto.

#### Scenario: Guardar preset
- **WHEN** el usuario guarda un preset con el delay encendido y lo carga después
- **THEN** el delay vuelve encendido con los mismos valores
