// =============================================================================
// jackpot.ino — Máquina Traga-Monedas para Arduino UNO R3 (ATmega328P)
// CI-0155 Sistemas Empotrados de Tiempo Real — Universidad de Costa Rica
// =============================================================================

// =============================================================================
// [1] INCLUDES Y CONSTANTES
// =============================================================================
#include <LiquidCrystal_I2C.h>  // "LiquidCrystal I2C" by Frank de Brabander
#include <EEPROM.h>
#include <avr/pgmspace.h>

// LCD 2004 via I2C (PCF8574). Cambiar a 0x3F si el LCD no responde.
// SDA → A4, SCL → A5 (pines I2C hardware del ATmega328P)
static constexpr uint8_t LCD_I2C_ADDR = 0x27;

// Botones — pull-down externo 10kΩ, HIGH = presionado
static constexpr uint8_t BTN_ARRIBA = 8;
static constexpr uint8_t BTN_ABAJO  = 9;
static constexpr uint8_t BTN_ACCION = 10;

// LEDs
static constexpr uint8_t LED_VERDE    = 6;
static constexpr uint8_t LED_AMARILLO = 7;
static constexpr uint8_t LED_RGB_R    = A0;  // pines analógicos usados como digital
static constexpr uint8_t LED_RGB_G    = A1;
static constexpr uint8_t LED_RGB_B    = A2;

// Buzzer pasivo
static constexpr uint8_t BUZZER = 13;

// EEPROM — layout de 8 bytes (total disponible: 1024)
static constexpr uint16_t EEPROM_FIRMA_ADDR    = 0x00;  // uint16_t, firma 0xCAFE
static constexpr uint16_t EEPROM_JACKPOT_ADDR  = 0x02;  // uint16_t, acumulado
static constexpr uint16_t EEPROM_PARTIDAS_ADDR = 0x04;  // uint32_t, contador total
static constexpr uint16_t EEPROM_FIRMA_VAL     = 0xCAFE;

// Pesos de probabilidad — aritmética 100% entera, sin float
static constexpr uint8_t PESO_NUM_1_6    = 10;
static constexpr uint8_t PESO_BASE_7     = 3;
static constexpr uint8_t PESO_MAX_7      = 40;
static constexpr uint8_t DIVISOR_JACKPOT = 5;

// Premios fijos (puntos)
static constexpr uint8_t PREMIO_CHICO  = 5;
static constexpr uint8_t PREMIO_GRANDE = 20;

// Tiempos (ms) — toda temporización vía millis(), nunca delay()
static constexpr uint16_t TIEMPO_GIRO_T1   = 1000;
static constexpr uint16_t TIEMPO_GIRO_T2   = 1500;
static constexpr uint16_t TIEMPO_GIRO_T3   = 2000;
static constexpr uint16_t INTERVALO_GIRO   = 80;
static constexpr uint16_t TIEMPO_RESULTADO = 3000;
static constexpr uint16_t DEBOUNCE_MS      = 200;

// Número de opciones del menú
static constexpr uint8_t NUM_OPCIONES_MENU = 3;

// Melodías en Flash: pares [frecuencia Hz, duración ms], {0,0} marca el fin.
// PROGMEM evita que las constantes consuman SRAM.

// Arpeggio ascendente al iniciar el giro (G6→B6→D7) — ×2 vs octava anterior
static const uint16_t PROGMEM NOTAS_INICIO_GIRO[] = {
    1568, 80, 1976, 80, 2350, 150, 0, 0
};
// Pérdida — tres notas descendentes (B5→F#5→B4)
static const uint16_t PROGMEM NOTAS_PERDER[] = {
    988, 150, 740, 150, 494, 350, 0, 0
};
// Par o secuencia (+5) — alegre ascendente (C6→E6→G6→C7)
static const uint16_t PROGMEM NOTAS_CHICO[] = {
    1047, 100, 1319, 100, 1568, 100, 2093, 300, 0, 0
};
// Triple (+20) — frase triunfal con eco (C6→E6→G6→C7→G6→C7→E7)
static const uint16_t PROGMEM NOTAS_TRIPLE[] = {
    1047, 80, 1319, 80, 1568, 80, 2093, 120,
    1568, 80, 2093, 80, 2637, 400, 0, 0
};
// Jackpot — fanfarria épica de tres frases ascendentes
static const uint16_t PROGMEM NOTAS_JACKPOT[] = {
    1047, 80, 1319, 80, 1568, 80, 2093, 120,
    1568, 80, 2093, 80, 2637, 150,
    2093, 80, 2637, 80, 3136, 600, 0, 0
};

// Debug serie — descomenta la siguiente línea para activar la salida de diagnóstico
// #define DEBUG_SERIAL
static constexpr uint32_t DEBUG_BAUD = 115200;

#ifdef DEBUG_SERIAL
  #define DBG(msg)        Serial.println(F(msg))
  #define DBG_VAL(lbl, v) do { Serial.print(F(lbl ": ")); Serial.println(v); } while(0)
#else
  #define DBG(msg)        ((void)0)
  #define DBG_VAL(lbl, v) ((void)0)
#endif

// =============================================================================
// [2] ENUMS Y VARIABLES GLOBALES
// =============================================================================

enum Estado : uint8_t {
    EST_STANDBY,
    EST_MENU,
    EST_GIRANDO,
    EST_EVALUANDO,
    EST_RESULTADO,
    EST_VER_PREMIO
};

enum TipoPremio : uint8_t {
    PREMIO_NINGUNO,
    PREMIO_PAR_SEQ,
    PREMIO_TRIPLE,
    PREMIO_JACKPOT
};

static Estado     estadoActual  = EST_MENU;
static uint8_t    menuSel       = 0;        // 0=Jugar, 1=Ver Premio, 2=Salir
static bool       menuDirty     = true;

// Resultados de la jugada actual
static uint8_t    tambores[3]   = {0, 0, 0};
static TipoPremio ultimoPremio  = PREMIO_NINGUNO;
static uint16_t   jackpotActual = 0;
static uint16_t   jackpotGanado = 0;  // valor capturado al ganar el jackpot

// Animación de giro
static uint8_t    visuals[3]          = {1, 1, 1};  // números cosméticos en pantalla
static uint8_t    tamboresDetenidos   = 0;           // bitmask: bit0=T1, bit1=T2, bit2=T3

// Timers para millis() — uint32_t requerido (millis() regresa uint32_t)
static uint32_t tDebounce        = 0;
static uint32_t tGiroInicio      = 0;
static uint32_t tUltimoFrame     = 0;
static uint32_t tResultadoInicio = 0;
static uint32_t tLedAnima        = 0;
static uint32_t tNotaInicio      = 0;

// Estado de animación de feedback
static uint8_t              notaIdx      = 0;        // índice del par [freq, dur] activo
static uint8_t              rgbFase      = 0;        // 0=R, 1=G, 2=B para el ciclo jackpot
static const uint16_t*      melodiaActual = nullptr; // melodía PROGMEM en reproducción

LiquidCrystal_I2C lcd(LCD_I2C_ADDR, 20, 4);

// =============================================================================
// [2b] HELPERS DE DEBUG (solo se compilan con DEBUG_SERIAL activo)
// =============================================================================

#ifdef DEBUG_SERIAL
static const __FlashStringHelper* nombreEstado(Estado e) {
    switch(e) {
        case EST_STANDBY:    return F("STANDBY");
        case EST_MENU:       return F("MENU");
        case EST_GIRANDO:    return F("GIRANDO");
        case EST_EVALUANDO:  return F("EVALUANDO");
        case EST_RESULTADO:  return F("RESULTADO");
        case EST_VER_PREMIO: return F("VER_PREMIO");
        default:             return F("?");
    }
}
static const __FlashStringHelper* nombrePremio(TipoPremio p) {
    switch(p) {
        case PREMIO_JACKPOT:  return F("JACKPOT");
        case PREMIO_TRIPLE:   return F("TRIPLE");
        case PREMIO_PAR_SEQ:  return F("PAR/SEQ");
        case PREMIO_NINGUNO:  return F("NINGUNO");
        default:              return F("?");
    }
}
static void cambiarEstado(Estado nuevo) {
    Serial.print(F("[FSM] "));
    Serial.print(nombreEstado(estadoActual));
    Serial.print(F(" -> "));
    Serial.println(nombreEstado(nuevo));
    estadoActual = nuevo;
}
#define SET_ESTADO(e) cambiarEstado(e)
#else
#define SET_ESTADO(e) (estadoActual = (e))
#endif

// =============================================================================
// [3] FUNCIONES DE EEPROM
// =============================================================================

static void inicializarEEPROM(void) {
    uint16_t firma = 0;
    EEPROM.get(EEPROM_FIRMA_ADDR, firma);
    if (firma != EEPROM_FIRMA_VAL) {
        EEPROM.put(EEPROM_FIRMA_ADDR,    (uint16_t)EEPROM_FIRMA_VAL);
        EEPROM.put(EEPROM_JACKPOT_ADDR,  (uint16_t)0);
        EEPROM.put(EEPROM_PARTIDAS_ADDR, (uint32_t)0);
    }
}

static uint16_t leerJackpot(void) {
    uint16_t val = 0;
    EEPROM.get(EEPROM_JACKPOT_ADDR, val);
    return val;
}

static void guardarJackpot(uint16_t val) {
    EEPROM.put(EEPROM_JACKPOT_ADDR, val);  // put() usa update() internamente
}

static void incrementarPartidas(void) {
    uint32_t partidas = 0;
    EEPROM.get(EEPROM_PARTIDAS_ADDR, partidas);
    partidas++;
    EEPROM.put(EEPROM_PARTIDAS_ADDR, partidas);
}

// =============================================================================
// [4] FUNCIONES DE BOTONES
// =============================================================================

// Devuelve bitmask: bit0=ARRIBA, bit1=ABAJO, bit2=ACCION.
// Ignora pulsaciones más rápidas que DEBOUNCE_MS.
static uint8_t leerBotones(void) {
    uint32_t tActual = millis();
    if (tActual - tDebounce < DEBOUNCE_MS) return 0;

    uint8_t mask = 0;
    if (digitalRead(BTN_ARRIBA) == HIGH) mask |= 0x01;
    if (digitalRead(BTN_ABAJO)  == HIGH) mask |= 0x02;
    if (digitalRead(BTN_ACCION) == HIGH) mask |= 0x04;

    if (mask != 0) {
        tDebounce = tActual;
        DBG_VAL("[BTN] mask", mask);
    }
    return mask;
}

// =============================================================================
// [5] FUNCIONES DE PROBABILIDAD
// =============================================================================

static uint8_t calcularPeso7(uint16_t jackpot) {
    uint8_t peso = PESO_BASE_7 + (uint8_t)(jackpot / DIVISOR_JACKPOT);
    if (peso > PESO_MAX_7) peso = PESO_MAX_7;
#ifdef DEBUG_SERIAL
    Serial.print(F("[PROB] jackpot="));
    Serial.print(jackpot);
    Serial.print(F(" peso7="));
    Serial.println(peso);
#endif
    return peso;
}

// Genera un número 1–7 usando tabla de pesos ponderados (sin float).
static uint8_t generarNumero(uint16_t jackpot) {
    uint8_t pesos[7];
    for (uint8_t i = 0; i < 6; i++) pesos[i] = PESO_NUM_1_6;
    pesos[6] = calcularPeso7(jackpot);

    uint16_t pesoTotal = 0;
    for (uint8_t i = 0; i < 7; i++) pesoTotal += pesos[i];

    uint16_t r = (uint16_t)random(0L, (long)pesoTotal);
    uint16_t acumulado = 0;
    for (uint8_t i = 0; i < 7; i++) {
        acumulado += pesos[i];
        if (r < acumulado) {
            DBG_VAL("[GEN] numero", i + 1);
            return i + 1;
        }
    }
    DBG("[GEN] numero=7 (fallback)");
    return 7;  // fallback, no debería alcanzarse
}

// Evalúa la combinación en orden de prioridad descendente.
static TipoPremio evaluarPremio(uint8_t a, uint8_t b, uint8_t c) {
    // 1. Jackpot
    if (a == 7 && b == 7 && c == 7) return PREMIO_JACKPOT;

    // 2. Triple (no 7)
    if (a == b && b == c) return PREMIO_TRIPLE;

    // 3. Secuencia: ordenar con bubble sort de 3 elementos y verificar escalera
    uint8_t s[3] = {a, b, c};
    if (s[0] > s[1]) { uint8_t t = s[0]; s[0] = s[1]; s[1] = t; }
    if (s[1] > s[2]) { uint8_t t = s[1]; s[1] = s[2]; s[2] = t; }
    if (s[0] > s[1]) { uint8_t t = s[0]; s[0] = s[1]; s[1] = t; }
    if (s[1] - s[0] == 1 && s[2] - s[1] == 1) return PREMIO_PAR_SEQ;

    // 4. Par
    if (a == b || b == c || a == c) return PREMIO_PAR_SEQ;

    return PREMIO_NINGUNO;
}

// =============================================================================
// [6] FUNCIONES DE DISPLAY (LCD)
// =============================================================================

static void mostrarMenu(uint8_t sel) {
    lcd.clear();
    lcd.setCursor(3, 0);
    lcd.print(F("TRAGA-MONEDAS"));
    lcd.setCursor(0, 1);
    lcd.print(sel == 0 ? F("> Jugar     ") : F("  Jugar     "));
    lcd.setCursor(0, 2);
    lcd.print(sel == 1 ? F("> Ver Premio") : F("  Ver Premio"));
    lcd.setCursor(0, 3);
    lcd.print(sel == 2 ? F("> Salir     ") : F("  Salir     "));
}

// Dibuja la pantalla estática del giro (llamar una sola vez al entrar al estado).
static void iniciarPantallaGiro(void) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("===================="));
    lcd.setCursor(4, 1);
    lcd.print(F("GIRANDO..."));
    lcd.setCursor(0, 3);
    lcd.print(F("===================="));
}

// Actualiza solo la línea de los tambores (línea 2) para minimizar tráfico I2C.
static void actualizarTamboresLCD(void) {
    char t[3][3];
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t val = (tamboresDetenidos & (1 << i)) ? tambores[i] : visuals[i];
        snprintf(t[i], sizeof(t[i]), "%u", val);
    }
    char buf[21];
    snprintf(buf, sizeof(buf), "   [%s] [%s] [%s]   ", t[0], t[1], t[2]);
    lcd.setCursor(0, 2);
    lcd.print(buf);
}

static void mostrarResultado(void) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("===================="));

    char buf[21];
    snprintf(buf, sizeof(buf), "    [%u]  [%u]  [%u]   ",
             tambores[0], tambores[1], tambores[2]);
    lcd.setCursor(0, 1);
    lcd.print(buf);

    lcd.setCursor(0, 2);
    switch (ultimoPremio) {
        case PREMIO_JACKPOT:
            lcd.print(F("  ** JACKPOT!!! **  "));
            lcd.setCursor(0, 3);
            snprintf(buf, sizeof(buf), "    Ganas: %u millones!", jackpotGanado);
            lcd.print(buf);
            break;
        case PREMIO_TRIPLE:
            lcd.print(F("  TRIPLE! +20 mil   "));
            lcd.setCursor(0, 3);
            lcd.print(F("===================="));
            break;
        case PREMIO_PAR_SEQ:
            lcd.print(F("  PAR o SEQ! +5 mil   "));
            lcd.setCursor(0, 3);
            lcd.print(F("===================="));
            break;
        case PREMIO_NINGUNO:
        default:
            lcd.print(F("   Sin premio  :(   "));
            lcd.setCursor(0, 3);
            lcd.print(F("===================="));
            break;
    }
}

static void mostrarVerPremio(uint16_t jp) {
    lcd.clear();
    lcd.setCursor(2, 0);
    lcd.print(F("PREMIO ACUMULADO"));
    char buf[12];
    snprintf(buf, sizeof(buf), "Jackpot: %u", jp);
    lcd.setCursor(6, 2);
    lcd.print(buf);
    lcd.setCursor(1, 3);
    lcd.print(F("[Presione un boton]"));
}

static void mostrarStandby(void) {
    lcd.clear();
    lcd.setCursor(3, 1);
    lcd.print(F("Pulse cualquier"));
    lcd.setCursor(5, 2);
    lcd.print(F("boton..."));
}

// =============================================================================
// [7] FUNCIONES DE FEEDBACK (LEDs + BUZZER) — todas no bloqueantes
// =============================================================================

static void apagarFeedback(void) {
    noTone(BUZZER);
    melodiaActual = nullptr;
    digitalWrite(LED_VERDE,    LOW);
    digitalWrite(LED_AMARILLO, LOW);
    digitalWrite(LED_RGB_R,    LOW);
    digitalWrite(LED_RGB_G,    LOW);
    digitalWrite(LED_RGB_B,    LOW);
}

// Inicia la reproducción de una melodía PROGMEM; avanzarMelodia() la progresa
// cada iteración del loop principal.
static void iniciarMelodia(const uint16_t* notas) {
    melodiaActual = notas;
    notaIdx       = 0;
    tNotaInicio   = millis();
    uint16_t freq = pgm_read_word(&notas[0]);
    if (freq != 0) tone(BUZZER, freq);
}

// Avanza la melodía activa; llamar una vez por iteración de loop().
// Es un no-op si no hay melodía activa o ya terminó.
static void avanzarMelodia(void) {
    if (melodiaActual == nullptr) return;
    uint16_t freq = pgm_read_word(&melodiaActual[notaIdx * 2]);
    if (freq == 0) return;

    uint16_t dur = pgm_read_word(&melodiaActual[notaIdx * 2 + 1]);
    if (millis() - tNotaInicio >= (uint32_t)dur) {
        notaIdx++;
        uint16_t freqNext = pgm_read_word(&melodiaActual[notaIdx * 2]);
        if (freqNext != 0) {
            tone(BUZZER, freqNext);
        } else {
            noTone(BUZZER);
        }
        tNotaInicio = millis();
    }
}

// true mientras haya una nota activa en la melodía actual.
static bool melodiaActiva(void) {
    if (melodiaActual == nullptr) return false;
    return pgm_read_word(&melodiaActual[notaIdx * 2]) != 0;
}

// Reproduce un tono corto de evento solo si no hay melodía en curso,
// evitando interrumpir fanfarrias de premio. tone() con duración es no bloqueante.
static void sonarBeep(uint16_t freq, uint16_t dur) {
    if (!melodiaActiva()) tone(BUZZER, freq, dur);
}

// --- Animaciones de resultado (solo LEDs; melodía avanza en loop) ---

static void animarPerdida(void) {
    // LEDs permanecen apagados
}

static void animarPremioChico(void) {
    digitalWrite(LED_VERDE, HIGH);
}

static void animarPremioTriple(void) {
    uint32_t tActual = millis();
    if (tActual - tLedAnima >= 200UL) {
        tLedAnima = tActual;
        digitalWrite(LED_AMARILLO, !digitalRead(LED_AMARILLO));
    }
}

static void animarJackpot(void) {
    uint32_t tActual = millis();
    if (tActual - tLedAnima >= 300UL) {
        tLedAnima = tActual;
        digitalWrite(LED_RGB_R, rgbFase == 0 ? HIGH : LOW);
        digitalWrite(LED_RGB_G, rgbFase == 1 ? HIGH : LOW);
        digitalWrite(LED_RGB_B, rgbFase == 2 ? HIGH : LOW);
        rgbFase = (rgbFase + 1) % 3;
    }
}

// =============================================================================
// [8] MANEJADORES DE ESTADO (uno por estado del FSM)
// =============================================================================

static void manejarStandby(void) {
    if (leerBotones() != 0) {
        SET_ESTADO(EST_MENU);
        menuDirty = true;
    }
}

static void manejarMenu(void) {
    uint8_t btn = leerBotones();

    if (btn & 0x01) {  // ARRIBA — cursor circular
        sonarBeep(1760, 40);
        menuSel   = (menuSel == 0) ? NUM_OPCIONES_MENU - 1 : menuSel - 1;
        menuDirty = true;
    } else if (btn & 0x02) {  // ABAJO — cursor circular
        sonarBeep(1320, 40);
        menuSel   = (menuSel + 1) % NUM_OPCIONES_MENU;
        menuDirty = true;
    } else if (btn & 0x04) {  // ACCION — seleccionar opción
        sonarBeep(2093, 60);
        if (menuSel == 0) {
            tamboresDetenidos        = 0;
            visuals[0] = visuals[1] = visuals[2] = 1;
            tGiroInicio  = millis();
            tUltimoFrame = millis();
            iniciarMelodia(NOTAS_INICIO_GIRO);
            SET_ESTADO(EST_GIRANDO);
            iniciarPantallaGiro();
            actualizarTamboresLCD();
        } else if (menuSel == 1) {
            SET_ESTADO(EST_VER_PREMIO);
            mostrarVerPremio(leerJackpot());
        } else {
            sonarBeep(1320, 200);
            SET_ESTADO(EST_STANDBY);
            mostrarStandby();
        }
        return;
    }

    if (menuDirty) {
        mostrarMenu(menuSel);
        menuDirty = false;
    }
}

static void manejarGirando(void) {
    uint32_t tActual = millis();
    uint32_t elapsed = tActual - tGiroInicio;
    bool     redraw  = false;

    // Actualizar números cosméticos cada INTERVALO_GIRO ms
    if (tActual - tUltimoFrame >= INTERVALO_GIRO) {
        tUltimoFrame = tActual;
        for (uint8_t i = 0; i < 3; i++) {
            if (!(tamboresDetenidos & (1 << i))) {
                visuals[i] = (uint8_t)random(1L, 8L);
            }
        }
        // Click mecánico de rodillo solo mientras queden tambores girando
        if (tamboresDetenidos != 0x07) sonarBeep(2000, 20);
        redraw = true;
    }

    // Detener tambor 1 — el número real se genera en este momento
    if (!(tamboresDetenidos & 0x01) && elapsed >= TIEMPO_GIRO_T1) {
        tambores[0] = generarNumero(jackpotActual);
        DBG_VAL("[GIRO] T1", tambores[0]);
        tone(BUZZER, 2400, 70);  // click de parada, siempre audible
        tamboresDetenidos |= 0x01;
        redraw = true;
    }
    // Detener tambor 2
    if (!(tamboresDetenidos & 0x02) && elapsed >= TIEMPO_GIRO_T2) {
        tambores[1] = generarNumero(jackpotActual);
        DBG_VAL("[GIRO] T2", tambores[1]);
        tone(BUZZER, 3200, 70);
        tamboresDetenidos |= 0x02;
        redraw = true;
    }
    // Detener tambor 3 → pasar a evaluación
    if (!(tamboresDetenidos & 0x04) && elapsed >= TIEMPO_GIRO_T3) {
        tambores[2] = generarNumero(jackpotActual);
        DBG_VAL("[GIRO] T3", tambores[2]);
        tone(BUZZER, 4000, 100);  // pitch más alto: anticipación
        tamboresDetenidos |= 0x04;
        redraw = true;
        SET_ESTADO(EST_EVALUANDO);
    }

    if (redraw) actualizarTamboresLCD();
}

static void manejarEvaluando(void) {
    ultimoPremio     = evaluarPremio(tambores[0], tambores[1], tambores[2]);
    tResultadoInicio = millis();
    tLedAnima        = millis();
    rgbFase          = 0;

#ifdef DEBUG_SERIAL
    Serial.print(F("[EVAL] Tambores: "));
    Serial.print(tambores[0]); Serial.print(' ');
    Serial.print(tambores[1]); Serial.print(' ');
    Serial.println(tambores[2]);
    Serial.print(F("[EVAL] Premio: "));
    Serial.println(nombrePremio(ultimoPremio));
#endif

    incrementarPartidas();

    switch (ultimoPremio) {
        case PREMIO_JACKPOT:
            jackpotGanado = leerJackpot();
            jackpotActual = 0;
            guardarJackpot(0);
            DBG_VAL("[EVAL] Jackpot ganado", jackpotGanado);
            iniciarMelodia(NOTAS_JACKPOT);
            break;
        case PREMIO_TRIPLE:
            iniciarMelodia(NOTAS_TRIPLE);
            break;
        case PREMIO_PAR_SEQ:
            iniciarMelodia(NOTAS_CHICO);
            break;
        case PREMIO_NINGUNO:
        default:
            jackpotActual = leerJackpot() + 1;
            guardarJackpot(jackpotActual);
            DBG_VAL("[EVAL] Jackpot nuevo", jackpotActual);
            iniciarMelodia(NOTAS_PERDER);
            break;
    }

    mostrarResultado();
    SET_ESTADO(EST_RESULTADO);
}

static void manejarResultado(void) {
    switch (ultimoPremio) {
        case PREMIO_JACKPOT:  animarJackpot();      break;
        case PREMIO_TRIPLE:   animarPremioTriple(); break;
        case PREMIO_PAR_SEQ:  animarPremioChico();  break;
        case PREMIO_NINGUNO:  animarPerdida();      break;
    }

    if (millis() - tResultadoInicio >= TIEMPO_RESULTADO) {
        apagarFeedback();
        SET_ESTADO(EST_MENU);
        menuDirty = true;
    }
}

static void manejarVerPremio(void) {
    if (leerBotones() != 0) {
        SET_ESTADO(EST_MENU);
        menuDirty = true;
    }
}

// =============================================================================
// [9] SETUP
// =============================================================================

void setup() {
#ifdef DEBUG_SERIAL
    Serial.begin(DEBUG_BAUD);
    DBG("=== DEBUG SERIAL ACTIVO ===");
#endif
    pinMode(BTN_ARRIBA, INPUT);  // pull-down externo
    pinMode(BTN_ABAJO,  INPUT);
    pinMode(BTN_ACCION, INPUT);

    pinMode(LED_VERDE,    OUTPUT);
    pinMode(LED_AMARILLO, OUTPUT);
    pinMode(LED_RGB_R,    OUTPUT);  // A0 configurado como salida digital
    pinMode(LED_RGB_G,    OUTPUT);  // A1
    pinMode(LED_RGB_B,    OUTPUT);  // A2
    pinMode(BUZZER,       OUTPUT);
    apagarFeedback();

    // Test RGB al arranque: R → G → B, 400 ms por color
    digitalWrite(LED_RGB_R, HIGH); delay(400); digitalWrite(LED_RGB_R, LOW);
    digitalWrite(LED_RGB_G, HIGH); delay(400); digitalWrite(LED_RGB_G, LOW);
    digitalWrite(LED_RGB_B, HIGH); delay(400); digitalWrite(LED_RGB_B, LOW);

    delay(50);          // HD44780 necesita ≥40 ms tras power-on antes de recibir comandos
    lcd.init();
    lcd.begin(20, 4);   // fuerza la inicialización del controlador explícitamente
    lcd.backlight();

    inicializarEEPROM();
    jackpotActual = leerJackpot();
    DBG_VAL("[SETUP] Jackpot inicial", jackpotActual);

    // A3 flotante: ruido eléctrico como semilla (A4/A5 ocupados por I2C)
    randomSeed(analogRead(A3));

    SET_ESTADO(EST_MENU);
    mostrarMenu(menuSel);
}

// =============================================================================
// [10] LOOP — despacho de estados, sin delay()
// =============================================================================

void loop() {
    avanzarMelodia();  // avanza la melodía activa en cada iteración, independiente del estado
    switch (estadoActual) {
        case EST_STANDBY:    manejarStandby();   break;
        case EST_MENU:       manejarMenu();      break;
        case EST_GIRANDO:    manejarGirando();   break;
        case EST_EVALUANDO:  manejarEvaluando(); break;
        case EST_RESULTADO:  manejarResultado(); break;
        case EST_VER_PREMIO: manejarVerPremio(); break;
    }
}
