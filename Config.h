#pragma once

#include <Arduino.h>

#ifndef WIFI_SSID
#define WIFI_SSID "...."
#endif

#ifndef WIFI_PWD
#define WIFI_PWD "...."
#endif

namespace Config {

// -------------------- Brochage --------------------
inline constexpr uint8_t kStepPin        = 13;  // D7
inline constexpr uint8_t kDirPin         = 12;  // D6
inline constexpr int8_t  kEnablePin      = 14;  // D5
inline constexpr bool    kEnableActiveLow = true;

inline constexpr uint8_t kLimitBottomPin = 5;   // D1 (pull-up interne)
inline constexpr bool    kLimitActiveLow = true;

inline constexpr int8_t  kButtonPin      = 4;   // D2 (pull-up interne)
inline constexpr bool    kButtonActiveLow = true;

// -------------------- Wi-Fi --------------------
inline constexpr char kWifiSsid[]     = WIFI_SSID;
inline constexpr char kWifiPassword[] = WIFI_PWD;

// -------------------- Paramètres moteurs --------------------
inline constexpr int   kFullStepsPerRev = 200;
inline constexpr int   kMicrostepFactor = 10;

inline constexpr long stepsPerRevolution() {
  return static_cast<long>(kFullStepsPerRev) * static_cast<long>(kMicrostepFactor);
}

struct MotionConfig {
  float openTurns;
  float maxStepsPerSecond;
  float accelStepsPerSecond2;
};

inline constexpr MotionConfig defaultMotion() {
  return MotionConfig{
      10.0f,
      1.6f * static_cast<float>(stepsPerRevolution()),
      0.0002f * static_cast<float>(stepsPerRevolution()) * static_cast<float>(stepsPerRevolution())};
}

inline constexpr float kGearCmPerTurn = 25.4466f;

inline long cmToSteps(float distanceCm) {
  return static_cast<long>((distanceCm / kGearCmPerTurn) * static_cast<float>(stepsPerRevolution()) + 0.5f);
}

// -------------------- Homing --------------------
inline constexpr long kHomingTravelSteps = stepsPerRevolution() * 40L;
inline constexpr unsigned long kHomingTimeoutMs = 30000UL;
inline constexpr float kHomingSpeedFactor = 0.25f;
inline constexpr float kHomingAccelFactor = 0.25f;

// -------------------- Divers --------------------
inline constexpr unsigned long kNanoTimeoutMs = 2000UL;
inline constexpr unsigned long kTemperaturePollMs = 5000UL;
inline constexpr unsigned long kButtonDebounceMs = 125UL;
inline constexpr unsigned long kButtonLongPressMs = 5000UL;

// ---- StepperKiss options anti-stutter ----
inline constexpr uint8_t KISS_MIN_PULSE_US = 6;  // DM556 >=5µs
inline constexpr bool    KISS_USE_FAST_GPIO = true;

}  // namespace Config

#ifndef KISS_USE_FAST_GPIO
#define KISS_USE_FAST_GPIO Config::KISS_USE_FAST_GPIO
#endif

#ifndef KISS_MIN_PULSE_US
#define KISS_MIN_PULSE_US Config::KISS_MIN_PULSE_US
#endif

