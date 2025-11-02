#pragma once
#define KISS_USE_FAST_GPIO 1

// ---- Pins ----
#define STEP_PIN   13   // D7
#define DIR_PIN    12   // D6
#define ENA_PIN    14   // D5
#define LIMIT_BOTTOM 5  // D1 (pull-up interne)
#define BUTTON_PIN 4  // D2, (pull-up interne)

// -------------------- Paramètres WIFI --------------------
#define WIFI_SSID "TELUS7704"
#define WIFI_PWD  "B6hxR6CHJ87n"

static const int   FULL_STEPS_PER_REV = 200;
static const int   MICROSTEP_FACTOR   = 10;// devrait le combiner avec FULL_STEPS_PER_REV *****
static const bool  ENA_ACTIVE_LOW     = true;

//-----------------------------------------

static const float OPEN_TURNS_DEFAULT = 10.0f; // Nombre de tgour à l'ouverture
static const float VMAX_REV_S_DEFAULT = 1.6f;
static const float ACCEL_REV_S2_DEF   = 0.0002f; //semble ici pour le 1000000 d'acceleration sur le webUI

// Conversion tours/s -> steps/s, etc.
static const long  kStepsPerRev   = (long)(FULL_STEPS_PER_REV * MICROSTEP_FACTOR);
static float       kOpenTurns     = OPEN_TURNS_DEFAULT;
static float       kVmaxSteps     = VMAX_REV_S_DEFAULT   * kStepsPerRev;
static float       kAccelSteps2   = ACCEL_REV_S2_DEF     * kStepsPerRev * kStepsPerRev;

// Homing: déplacement négatif "sûr" jusqu’au fin de course bas (D1, PULLUP)
// Valeurs pour le homing : déplacement sûr et délai maximum
const long  kHomingTravel  = kStepsPerRev * 40L;   // marge généreuse
// Utiliser une notation entière simple (30000) car certains compilateurs Arduino
// ne supportent pas les séparateurs de milliers avec des apostrophes.
const unsigned long kHomingTimeoutMs = 30000UL;

// ---- StepperKiss options anti-stutter ----

//static const bool KISS_USE_FAST_GPIO = true; // GPOS/GPOC sur ESP8266
static const uint8_t KISS_MIN_PULSE_US = 6;  // DM556 >=5µs


