# Máquina Traga-Monedas — Arduino UNO R3

## CI-0155 Sistemas Empotrados de Tiempo Real — Universidad de Costa Rica

---

## 1. Descripción General

Este proyecto implementa una máquina traga-monedas digital sobre un Arduino UNO R3 (ATmega328P). El sistema simula tres "tambores" virtuales que giran y se detienen mostrando números del 1 al 7 en un LCD 2004. El jugador interactúa mediante botones físicos, y el sistema mantiene un premio acumulado (jackpot) persistente en EEPROM que crece con cada partida perdida.

---

## 2. Reglas del Juego

### 2.1 Números y Tambores

Cada tambor genera un número aleatorio entre 1 y 7. Los números del 1 al 6 tienen mayor probabilidad de aparecer que el 7, que es el número especial para el jackpot.

### 2.2 Tabla de Premios

| Combinación | Ejemplo | Tipo de Premio | Valor |
|---|---|---|---|
| Secuencia consecutiva | 1-2-3, 3-4-5, 5-6-7 | Premio pequeño | 5 puntos |
| Dos números iguales | 1-2-2, 4-1-1, 3-3-1 | Premio pequeño | 5 puntos |
| Tres números iguales (no 7) | 3-3-3, 5-5-5, 1-1-1 | Premio grande | 20 puntos |
| Tres sietes | 7-7-7 | JACKPOT | Valor acumulado |
| Cualquier otra cosa | 1-3-6, 2-5-1 | Sin premio | 0 puntos |

**Nota:** Los premios pequeño y grande son valores fijos e independientes del jackpot acumulado. Solo el 7-7-7 otorga el jackpot.

### 2.3 Detección de Secuencias

Una "secuencia consecutiva" se define como tres números que forman una escalera ascendente (diferencia de +1 entre cada par consecutivo). Para detectarla, se ordenan los tres tambores y se verifica que formen una escalera en orden ascendente.

**Decisión de diseño:** Se evalúa en este orden de prioridad (de mayor a menor):
1. Jackpot (7-7-7) — verificar primero porque también sería "tres iguales"
2. Tres iguales (no 7) — verificar antes que dos iguales
3. Secuencia consecutiva — verificar antes que dos iguales para evitar que 2-3-3 se detecte como secuencia en vez de par
4. Dos iguales
5. Sin premio

### 2.4 Jackpot Acumulado

El jackpot es un contador persistente almacenado en EEPROM que sigue estas reglas:
- Inicia en 0 (o el valor que tenga de partidas anteriores).
- Cada vez que un jugador juega y NO gana ningún premio, el jackpot se incrementa en 1.
- Si alguien gana el jackpot (7-7-7), el acumulado se resetea a 0.
- Los premios pequeños y grandes NO afectan el jackpot (no lo resetean ni lo reducen).

### 2.5 Probabilidad Dinámica del 7

Solo la probabilidad de obtener un 7 en cada tambor aumenta conforme crece el jackpot. Los premios que no involucran al 7 (secuencias de 1-6, pares de 1-6, triples de 1-6) mantienen sus probabilidades relativas constantes.

---

## 3. Arquitectura de Hardware

### 3.1 Componentes Utilizados

| Componente | Cantidad | Función |
|---|---|---|
| Arduino UNO R3 | 1 | Microcontrolador principal |
| LCD 2004 (20x4 caracteres) | 1 | Pantalla principal del juego |
| Potenciómetro 10kΩ | 1 | Control de contraste del LCD |
| Botón pulsador | 3 | Navegación: ARRIBA, ABAJO, ACCIÓN |
| LED verde | 1 | Indicador de premio pequeño |
| LED amarillo | 1 | Indicador de premio grande |
| LED RGB | 1 | Animación del jackpot (parpadeo multicolor) |
| Buzzer pasivo | 1 | Efectos de sonido |
| Resistencias 10kΩ | 3 | Pull-down para botones |
| Resistencias 220Ω | 4 | Limitadoras para LEDs |

### 3.2 Asignación de Pines

El LCD 2004 en modo 4-bit consume 6 pines digitales. Se planifica la asignación así:

```
LCD 2004 (modo 4-bit):
  RS  → Pin 12
  EN  → Pin 11
  D4  → Pin 5
  D5  → Pin 4
  D6  → Pin 3
  D7  → Pin 2
  RW  → GND (solo escritura)
  VO  → Potenciómetro (contraste)
  VSS → GND
  VDD → 5V
  A   → 5V (backlight ánodo)
  K   → GND (backlight cátodo)

Botones (con pull-down a GND, lectura HIGH al presionar):
  BTN_ARRIBA  → Pin 8
  BTN_ABAJO   → Pin 9
  BTN_ACCION  → Pin 10

LEDs:
  LED_VERDE    → Pin 6 (PWM, para fade si se desea)
  LED_AMARILLO → Pin 7
  LED_RGB_R    → Pin A0 (usado como digital)
  LED_RGB_G    → Pin A1 (usado como digital)
  LED_RGB_B    → Pin A2 (usado como digital)

Buzzer:
  BUZZER       → Pin 13

Libre:
  A3, A4, A5   → Sin usar (reservados para expansión futura)
```

**Decisión de diseño — LCD sin I2C:** Aunque un módulo I2C ahorraría pines (solo SDA/SCL en A4/A5), se usa conexión directa porque el enunciado indica que se trabaja con el kit provisto y el LCD 2004 no tiene adaptador I2C. La librería `LiquidCrystal` estándar es suficiente y no agrega dependencias externas.

**Decisión de diseño — Pines analógicos como digitales:** Los pines A0-A2 se configuran como `OUTPUT` digital para el LED RGB. Esto es perfectamente válido en el ATmega328P y libera pines digitales para componentes que los necesitan más (LCD, botones).

**Decisión de diseño — Botones con pull-down:** Se usan resistencias externas de 10kΩ a GND. Alternativamente se podrían usar los pull-ups internos del ATmega328P (`INPUT_PULLUP`) y leer lógica invertida (LOW = presionado), lo cual elimina las resistencias externas. La implementación puede elegir cualquiera de los dos enfoques — si se usa `INPUT_PULLUP`, se debe invertir la lógica de lectura en el código.

### 3.3 Consumo de Pines — Resumen

| Recurso | Pines usados | Detalle |
|---|---|---|
| LCD 2004 | 6 digitales | D2, D3, D4, D5, D11, D12 |
| Botones | 3 digitales | D8, D9, D10 |
| LEDs simples | 2 digitales | D6, D7 |
| LED RGB | 3 analógicos | A0, A1, A2 |
| Buzzer | 1 digital | D13 |
| **Total** | **15 de 20 disponibles** | Quedan A3, A4, A5 libres |

---

## 4. Máquina de Estados

El sistema se modela como una máquina de estados finita (FSM) con 6 estados:

![diagrama de estados](/media/slot_machine_state_diagram.svg)

### 4.1 Descripción de cada Estado

**STANDBY:** Estado de reposo. El LCD muestra el backlight apagado (o un mensaje estático mínimo). El sistema espera la pulsación de cualquier botón para despertar y pasar a MENU. Opcionalmente puede entrar en modo de bajo consumo (`sleep_mode`).

**MENU:** Pantalla principal con tres opciones navegables con los botones ARRIBA/ABAJO y seleccionables con ACCIÓN:
```
====================
  TRAGA-MONEDAS
> Jugar
  Ver Premio
  Salir
====================
```
El cursor `>` se mueve entre las opciones. El LCD 2004 de 20x4 caracteres permite mostrar las tres opciones a la vez con un título.

**GIRANDO:** Animación de los tres tambores. Cada tambor "gira" mostrando números cambiantes rápidamente, y se detiene de izquierda a derecha con un retardo entre cada uno (simula el efecto mecánico de una máquina real). La animación utiliza `millis()` para temporización no bloqueante.

**EVALUANDO:** Estado transitorio (no visible al usuario, dura microsegundos). Aquí se evalúa la combinación resultante según la tabla de premios, se actualiza el jackpot en EEPROM si corresponde, y se determina qué animación de LEDs/sonido reproducir.

**RESULTADO:** Muestra el resultado en el LCD durante ~3 segundos con la animación correspondiente:
- Sin premio: LEDs apagados, tono corto grave.
- Premio pequeño: LED verde encendido, melodía corta ascendente.
- Premio grande: LED amarillo parpadeando, melodía más larga.
- JACKPOT: LED RGB cicla colores, melodía festiva, muestra el valor ganado.

Tras los ~3 segundos, regresa automáticamente a MENU.

**VER_PREMIO:** Muestra el valor actual del jackpot acumulado leído de EEPROM. Cualquier botón regresa a MENU.
```
====================
  PREMIO ACUMULADO
      JP: 42
  [Presione un boton]
====================
```

### 4.2 Enum de Estados

```cpp
enum Estado : uint8_t {
    EST_STANDBY,
    EST_MENU,
    EST_GIRANDO,
    EST_EVALUANDO,
    EST_RESULTADO,
    EST_VER_PREMIO
};
```

Se usa `uint8_t` como tipo base para ahorrar 1 byte respecto al `int` por defecto.

---

## 5. Sistema de Probabilidades

### 5.1 Tabla de Pesos Base

Cada tambor genera un número independiente usando una tabla de pesos ponderados. Los números 1-6 tienen peso igual y mayor que el 7:

| Número | Peso Base |
|---|---|
| 1 | 10 |
| 2 | 10 |
| 3 | 10 |
| 4 | 10 |
| 5 | 10 |
| 6 | 10 |
| 7 | 3 |

**Probabilidades base:**
- Cada número 1-6: 10/63 ≈ 15.9%
- Número 7: 3/63 ≈ 4.8%
- Probabilidad base de 7-7-7: (3/63)³ ≈ 0.011% (1 en ~9,261 jugadas)

### 5.2 Ajuste Dinámico por Jackpot

El peso del 7 se incrementa proporcionalmente al valor del jackpot acumulado (`J`):

```
peso_7 = PESO_BASE_7 + (J / DIVISOR_JACKPOT)
```

Donde `DIVISOR_JACKPOT` es una constante de escalamiento (propuesta: 5). Esto significa:

| Jackpot (J) | Peso del 7 | Prob. del 7 por tambor | Prob. de 7-7-7 |
|---|---|---|---|
| 0 | 3 | 4.8% | 0.011% |
| 10 | 5 | 7.7% | 0.045% |
| 25 | 8 | 11.6% | 0.157% |
| 50 | 13 | 17.1% | 0.500% |
| 100 | 23 | 25.6% | 1.674% |
| 150 | 33 | 31.7% | 3.194% |

**Decisión de diseño — Tope máximo:** Se establece un peso máximo para el 7 (propuesta: `PESO_MAX_7 = 40`) para evitar que domine completamente. Con peso 40, la probabilidad del 7 por tambor sería 40/100 = 40%, y la de 7-7-7 sería 6.4%. Esto da un tope razonable.

**Decisión de diseño — Aritmética entera:** Todo el cálculo usa `uint16_t`. No se usa `float` en ningún momento. La generación del número usa `random(0, pesoTotal)` y recorre la tabla acumulada para determinar qué número salió.

La razón se reduce a cómo funciona el ATmega328P (el chip del Arduino UNO R3): no tiene hardware para operaciones con decimales (Microchip Technology Inc., 2018). Lo que hace el compilador es inyectar una librería de software que simula esas operaciones paso a paso usando instrucciones enteras.

El resultado es que una sola multiplicación con float puede tomar entre 5 y 10 veces más ciclos de CPU que la misma operación con enteros, y además esa librería de emulación ocupa espacio extra en Flash (~1-2 KB).

### 5.3 Algoritmo de Generación (Pseudocódigo)

```
función generarNumero(jackpot):
    pesos[7] = {10, 10, 10, 10, 10, 10, calcularPeso7(jackpot)}
    pesoTotal = sumar(pesos)
    r = random(0, pesoTotal)

    acumulado = 0
    para i = 0 hasta 6:
        acumulado += pesos[i]
        si r < acumulado:
            retornar i + 1    // números van de 1 a 7

función calcularPeso7(jackpot):
    peso = PESO_BASE_7 + (jackpot / DIVISOR_JACKPOT)
    si peso > PESO_MAX_7:
        peso = PESO_MAX_7
    retornar peso
```

### 5.4 Semilla del Generador Aleatorio

Se usa `randomSeed(analogRead(A5))` en `setup()` leyendo un pin analógico no conectado (A5 está libre) para obtener ruido eléctrico como semilla. Esto garantiza secuencias diferentes en cada encendido.

**Decisión de diseño — A5 flotante:** El pin A5 no está conectado a ningún componente, lo cual lo hace ideal para lectura de ruido. Si en el futuro se conecta algo a A5, se deberá usar A4 o A3.

---

## 6. Persistencia en EEPROM

### 6.1 Layout de Direcciones

El ATmega328P tiene 1024 bytes de EEPROM. Se usa un layout mínimo:

```
Dirección 0x00-0x01: Firma de validación (2 bytes)
    Valor esperado: 0xCA, 0xFE
    Propósito: detectar si la EEPROM fue inicializada.
    Si no coincide, se asume primera ejecución y se inicializa todo a 0.

Dirección 0x02-0x03: Jackpot acumulado (uint16_t, 2 bytes)
    Rango: 0 – 65,535
    Se incrementa en +1 por cada partida perdida.
    Se resetea a 0 al ganar el jackpot (7-7-7).

Dirección 0x04-0x07: Total de partidas jugadas (uint32_t, 4 bytes)
    Rango: 0 – 4,294,967,295
    Contador estadístico, solo se incrementa, nunca se resetea.
    Uso informativo (podría mostrarse en pantalla de debug).

Total usado: 8 bytes de 1024 disponibles.
```

### 6.2 Protección contra Desgaste

La EEPROM tiene un límite de ~100,000 ciclos de escritura por celda. Para mitigar esto:

- **Usar `EEPROM.update()` en vez de `EEPROM.write()`**: Solo escribe si el valor cambió. Esto es crítico para la firma de validación, que nunca cambia después de la primera escritura.
- **Usar `EEPROM.put()` / `EEPROM.get()`** para tipos multi-byte (`uint16_t`, `uint32_t`), ya que internamente usan `update()`.
- **Frecuencia de escritura razonable:** El jackpot solo se escribe una vez por partida. Incluso jugando 100 partidas por día, se tendrían ~1,000 días (casi 3 años) antes de alcanzar el límite.

### 6.3 Inicialización en Primer Arranque

```
función inicializarEEPROM():
    leer firma de direcciones 0x00-0x01
    si firma != 0xCAFE:
        escribir firma 0xCAFE en 0x00-0x01
        escribir jackpot = 0 en 0x02-0x03
        escribir partidas = 0 en 0x04-0x07
```

**Decisión de diseño — Firma 0xCAFE:** Se usa una firma de 2 bytes en vez de un solo byte para reducir la probabilidad de un falso positivo en EEPROM no inicializada (1/65536 vs 1/256). Se eligió 0xCAFE por ser un valor reconocible en depuración.

---

## 7. Estructura del Código

### 7.1 Archivo Único vs Múltiples Archivos

**Decisión de diseño:** Se usa un único archivo `.ino` con secciones bien separadas por comentarios. Razones:
- Arduino IDE maneja archivos múltiples de forma peculiar (los concatena).
- Para un proyecto de este tamaño (~300-500 líneas estimadas), un solo archivo con buena organización es más simple de navegar y depurar.
- SimulIDE trabaja mejor con archivo único.

### 7.2 Organización del Archivo

```
slot_machine.ino
├── [1] Includes y Constantes
│   ├── #include <LiquidCrystal.h>
│   ├── #include <EEPROM.h>
│   ├── Constexpr de pines
│   ├── Constexpr de juego (pesos, premios, tiempos)
│   └── Constexpr de EEPROM (direcciones, firma)
│
├── [2] Enums y Variables Globales
│   ├── enum Estado : uint8_t
│   ├── enum Premio : uint8_t
│   ├── Estado estadoActual
│   ├── Variables de estado del juego
│   └── Objeto LiquidCrystal
│
├── [3] Funciones de EEPROM
│   ├── inicializarEEPROM()
│   ├── leerJackpot() → uint16_t
│   ├── guardarJackpot(uint16_t)
│   └── incrementarPartidas()
│
├── [4] Funciones de Entrada (Botones)
│   ├── leerBotones() → uint8_t
│   └── debounce integrado con millis()
│
├── [5] Funciones de Probabilidad
│   ├── calcularPeso7(uint16_t jackpot) → uint8_t
│   ├── generarNumero(uint16_t jackpot) → uint8_t
│   └── evaluarPremio(uint8_t, uint8_t, uint8_t) → Premio
│
├── [6] Funciones de Display (LCD)
│   ├── mostrarMenu(uint8_t seleccion)
│   ├── mostrarGiro(uint8_t[], uint8_t fase)
│   ├── mostrarResultado(uint8_t[], Premio, uint16_t)
│   ├── mostrarJackpot(uint16_t)
│   └── mostrarStandby()
│
├── [7] Funciones de Feedback (LEDs + Buzzer)
│   ├── animarPremioChico()
│   ├── animarPremioGrande()
│   ├── animarJackpot()
│   ├── animarPerdida()
│   └── apagarFeedback()
│
├── [8] Funciones de Estado (una por estado)
│   ├── manejarStandby()
│   ├── manejarMenu()
│   ├── manejarGirando()
│   ├── manejarEvaluando()
│   ├── manejarResultado()
│   └── manejarVerPremio()
│
├── [9] setup()
│   ├── Configurar pines
│   ├── Inicializar LCD
│   ├── Inicializar EEPROM
│   ├── randomSeed(analogRead(A5))
│   └── Estado inicial = EST_MENU
│
└── [10] loop()
    └── switch(estadoActual) → llamar función correspondiente
```

### 7.3 Convenciones de Código

- **Idioma:** Identificadores en español (consistente con el enunciado y el equipo).
- **Prefijo `static`:** Todas las funciones auxiliares llevan `static` para limitar su alcance.
- **`F()` obligatorio:** Toda cadena literal en `Serial.print()` o `lcd.print()` usa `F()`.
- **Sin `delay()` en `loop()`:** Toda temporización usa el patrón `millis()`.
- **Sin `String`:** Se usa `char[]` + `snprintf()` para formatear texto.
- **Sin `float`/`double`:** Toda aritmética es entera.
- **Variables del menor tamaño posible:** `uint8_t` donde el valor cabe en 0-255.

---

## 8. Animación de los Tambores

### 8.1 Efecto Visual

El LCD 2004 tiene 4 filas de 20 caracteres. Durante el giro se muestra:

```
====================
   GIRANDO...
   [3] [?] [?]
                    
====================
```

Los tambores se detienen de izquierda a derecha. Cada tambor "gira" mostrando números cambiantes cada ~80ms (no bloqueante), y al detenerse muestra su valor final. El intervalo de cambio se incrementa progresivamente (efecto de desaceleración) antes de que cada tambor se detenga.

### 8.2 Temporización

```
Tambor 1: gira 1.0s, luego se detiene
Tambor 2: gira 1.5s, luego se detiene
Tambor 3: gira 2.0s, luego se detiene
```

Cada tambor tiene su propio temporizador con `millis()`. Los números mostrados durante el giro son puramente cosméticos (generados con `random(1,8)` rápido); el resultado real se calcula con el sistema de pesos ponderados al momento de detener cada tambor.

**Decisión de diseño — Resultado al detenerse:** El número final de cada tambor se genera con `generarNumero(jackpot)` en el momento exacto en que ese tambor se "detiene", no antes. Esto evita almacenar el resultado antes de mostrarlo y hace que la animación sea coherente con el valor final.

---

## 9. Interacción con Botones

### 9.1 Esquema de 3 Botones

| Botón | Función en MENU | Función en GIRANDO | Función en RESULTADO |
|---|---|---|---|
| ARRIBA | Mover cursor arriba | — (ignorado) | — |
| ABAJO | Mover cursor abajo | — (ignorado) | — |
| ACCIÓN | Seleccionar opción | — (ignorado) | Volver a MENU |

### 9.2 Debounce

Se implementa debounce por software usando `millis()` con un tiempo mínimo entre pulsaciones de 200ms. Esto evita lecturas múltiples por un solo toque mecánico.

```
constexpr uint16_t DEBOUNCE_MS = 200;
```

**Decisión de diseño — Debounce simple:** No se usa interrupciones para los botones. El polling en `loop()` es suficiente dado que la máquina de estados siempre ejecuta `loop()` a alta velocidad (no hay `delay()`). Las interrupciones agregarían complejidad innecesaria para este caso.

---

## 10. Feedback Audiovisual

### 10.1 LEDs

| Evento | LED Verde | LED Amarillo | LED RGB |
|---|---|---|---|
| Sin premio | Apagado | Apagado | Apagado |
| Premio pequeño | Encendido 3s | Apagado | Apagado |
| Premio grande | Apagado | Parpadeo rápido 3s | Apagado |
| JACKPOT | Apagado | Apagado | Ciclo R→G→B→R 3s |

### 10.2 Buzzer

Melodías cortas usando `tone()`:

- **Sin premio:** Una melodía grave corta.
- **Premio pequeño:** Canción de tonos ascendentes.
- **Premio grande:** Escala ascendente más larga con ritmo.
- **JACKPOT:** Melodía con ritmo festivo.

**Decisión de diseño — `tone()` no bloqueante:** `tone()` de Arduino genera la onda en hardware (Timer2), así que no bloquea el CPU. Sin embargo, solo puede tocar un tono a la vez. Las melodías se implementan como secuencias de tonos temporizadas con `millis()`.

---

## 11. Estimación de Uso de Memoria

### 11.1 SRAM (~2048 bytes disponibles)

| Recurso | Bytes Estimados |
|---|---|
| Variables globales (estado, contadores, flags) | ~20 |
| Buffer LCD (char[21]) | ~21 |
| Objeto LiquidCrystal | ~60 |
| Tabla de pesos (uint8_t[7]) | 7 |
| Array de tambores (uint8_t[3]) | 3 |
| Stack (funciones, variables locales) | ~200 |
| **Total estimado** | **~311 bytes (~15% de SRAM)** |

Muy holgado. No se prevén problemas de memoria.

### 11.2 Flash (~32,256 bytes disponibles después de bootloader)

| Recurso | Bytes Estimados |
|---|---|
| Código del programa | ~4,000-6,000 |
| Librería LiquidCrystal | ~1,500 |
| Librería EEPROM | ~200 |
| Strings en Flash (F()) | ~500 |
| **Total estimado** | **~6,200-8,200 bytes (~20-25% de Flash)** |

Muy holgado también.

---

## 12. Decisiones de Diseño Consolidadas

1. **LCD directo (sin I2C):** Se usa conexión de 6 pines porque el kit no incluye adaptador I2C. Esto consume más pines pero evita dependencias de hardware extra.

2. **Pines analógicos como digitales para LED RGB:** Libera pines digitales para el LCD y botones. Perfectamente válido en el ATmega328P.

3. **Archivo único `.ino`:** Para un proyecto de ~400 líneas, múltiples archivos agregan complejidad sin beneficio real en Arduino IDE / SimulIDE.

4. **Aritmética 100% entera:** No se usa `float` ni `double` en ninguna parte del código. Los pesos y probabilidades se manejan con tablas de `uint8_t` y `uint16_t`.

5. **Pesos ponderados para probabilidades:** Sistema simple, eficiente, y fácilmente ajustable cambiando constantes. No requiere operaciones de punto flotante.

6. **Firma 0xCAFE en EEPROM:** Permite detectar primer arranque de forma robusta.

7. **Evaluación de premios en orden de prioridad:** Jackpot > Tres iguales > Secuencia > Dos iguales > Sin premio. Evita conflictos de clasificación.

8. **Resultado generado al detenerse cada tambor:** No se pre-calcula; el número final se genera en el instante de la detención para coherencia visual.

9. **Polling de botones sin interrupciones:** Simplifica el código. El `loop()` no tiene `delay()`, así que el polling es suficientemente responsivo.

10. **Premios fijos independientes del jackpot:** Solo el 7-7-7 otorga y resetea el acumulado. Los demás premios son valores constantes.

11. **Tope máximo en el peso del 7:** Se limita a `PESO_MAX_7` para que el 7 nunca tenga más del ~40% de probabilidad por tambor, manteniendo el juego interesante.

---

## 13. Constantes Propuestas para Ajuste

Estas constantes se definen como `constexpr` y permiten calibrar el juego sin modificar la lógica:

```cpp
// Pesos de probabilidad
static constexpr uint8_t  PESO_NUM_1_6     = 10;   // Peso de cada número 1-6
static constexpr uint8_t  PESO_BASE_7      = 3;    // Peso base del 7
static constexpr uint8_t  PESO_MAX_7       = 40;   // Peso máximo del 7
static constexpr uint8_t  DIVISOR_JACKPOT  = 5;    // Cuánto jackpot se necesita para +1 de peso al 7

// Premios
static constexpr uint8_t  PREMIO_CHICO     = 5;    // Puntos por secuencia o par
static constexpr uint8_t  PREMIO_GRANDE    = 20;   // Puntos por triple (no 7)

// Tiempos (ms)
static constexpr uint16_t TIEMPO_GIRO_T1   = 1000; // Duración giro tambor 1
static constexpr uint16_t TIEMPO_GIRO_T2   = 1500; // Duración giro tambor 2
static constexpr uint16_t TIEMPO_GIRO_T3   = 2000; // Duración giro tambor 3
static constexpr uint16_t INTERVALO_GIRO   = 80;   // ms entre cambios de número visual
static constexpr uint16_t TIEMPO_RESULTADO = 3000; // Duración pantalla de resultado
static constexpr uint16_t DEBOUNCE_MS      = 200;  // Debounce de botones
```

## Referencias

- Arduino. (s.f.). ARDUINO UNO REV3 SMD [Hoja de datos]. DigiKey.
- Microchip Technology Inc. (2018). ATmega328/P: AVR Microcontroller with picoPower Technology (Rev. A) [Hoja de datos].