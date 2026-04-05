#pragma once

#include <Arduino.h>

namespace Config {

// -------------------- Brochage (Pi Pico) --------------------
// These pin numbers correspond to GPIO numbers on the Raspberry Pi Pico.
// Adapt them to your own wiring as necessary.
inline constexpr uint8_t kStepPin        = 8;  // GPIO8 → STEP input of the driver
inline constexpr uint8_t kDirPin         = 7;  // GPIO7 → DIR input of the driver
inline constexpr int8_t  kEnablePin      = 6;  // GPIO6 → ENA input of the driver
inline constexpr bool    kEnableActiveLow = true;

inline constexpr uint8_t kLimitBottomPin = 21;   // GPIO21 → limit switch at bottom
inline constexpr bool    kLimitActiveLow = false;

inline constexpr int8_t  kButtonPin      = 22;  // GPIO22 → local push-button
inline constexpr bool    kButtonActiveLow = false;

// -------------------- Paramètres moteurs --------------------
inline constexpr bool  kEnableIDLE = true;
inline constexpr int   kDipSwitch = 8000; 
inline constexpr float kOpenTurns = 3.6f; // tour 3,6 défaut
inline constexpr float kSpeed = 1.6f; // tour/sec 1.6 defaut
inline constexpr float kAcceleration = 0.0002f;// valeur à determiner expérimentalement 0.00002 défaut

inline constexpr long stepsPerRevolution() {
  return static_cast<long>(kDipSwitch);
}

// Motion parameters: number of turns to fully open and default speed/acceleration.
struct MotionConfig {
  float openTurns;
  float maxStepsPerSecond;
  float accelStepsPerSecond2;
};

inline constexpr MotionConfig defaultMotion() {
  return MotionConfig{
      kOpenTurns,//Open turns 3.6 de base
      kSpeed * static_cast<float>(stepsPerRevolution()),//Speed 1.6 de base
      kAcceleration * static_cast<float>(stepsPerRevolution()) * static_cast<float>(stepsPerRevolution())};//Acceleration
}

inline constexpr float kGearCmPerTurn = 25.4466f;

// -------------------- Capteurs DS18B20 --------------------
inline constexpr uint8_t kOneWireBus1Pin = 28;  // GPIO28 → DS18B20 bus 1
inline constexpr uint8_t kOneWireBus2Pin = 2;   // GPIO2 → DS18B20 bus 2

// -------------------- Seuils --------------------
inline constexpr float kFaultTemperatureC = 40.0f;

inline long cmToSteps(float distanceCm) {
  return static_cast<long>((distanceCm / kGearCmPerTurn) * static_cast<float>(stepsPerRevolution()) + 0.5f);
}

// -------------------- Homing --------------------
inline constexpr long kHomingTravelSteps = stepsPerRevolution() * 20L;
inline constexpr unsigned long kHomingTimeoutMs = 20000UL;
inline constexpr float kHomingSpeedFactor = 0.25f;
inline constexpr float kHomingAccelFactor = 0.25f;

// -------------------- Divers --------------------
inline constexpr unsigned long kTemperaturePollMs = 10000UL;
inline constexpr unsigned long kButtonDebounceMs = 50UL;
inline constexpr unsigned long kButtonLongPressMs = 5000UL;

// ---- StepperKiss options anti‑stutter ----
inline constexpr uint8_t KISS_MIN_PULSE_US = 6;
inline constexpr bool    KISS_USE_FAST_GPIO = true;

}  // namespace Config

// Provide default macros expected by StepperKiss.h if not overridden elsewhere.
#ifndef KISS_USE_FAST_GPIO
#define KISS_USE_FAST_GPIO Config::KISS_USE_FAST_GPIO
#endif

#ifndef KISS_MIN_PULSE_US
#define KISS_MIN_PULSE_US Config::KISS_MIN_PULSE_US
#endif