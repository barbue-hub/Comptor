// CounterControl : surcouche pour la gestion d'un moteur pas-à-pas, de deux
// fins de course et d'un bouton physique. Cette version est adaptée pour le
// Raspberry Pi Pico.

#pragma once

#include <Arduino.h>

#include "Config.h"
#include "StepperKiss.h"

class CounterControl {
public:
  enum class ButtonEvent : uint8_t {
    None,
    ShortPress,
    LongPress
  };

  void begin(uint8_t stepPin, uint8_t dirPin, int8_t enaPin, bool enaActiveLow,
             uint8_t limitClosePin, bool limitCloseActiveLow,
             uint8_t limitOpenPin, bool limitOpenActiveLow,
             int8_t buttonPin, bool buttonActiveLow,
             long stepsPerRev, const Config::MotionConfig& motion,
             unsigned long debounceMs, unsigned long longPressMs) {
    _stepsPerRev = stepsPerRev;
    _limitClosePin = limitClosePin;
    _limitCloseActiveLow = limitCloseActiveLow;
    _limitOpenPin = limitOpenPin;
    _limitOpenActiveLow = limitOpenActiveLow;
    _buttonPin = buttonPin;
    _buttonActiveLow = buttonActiveLow;
    _debounceMs = debounceMs;
    _longPressMs = longPressMs;

    pinMode(_limitClosePin, INPUT);
    pinMode(_limitOpenPin, INPUT);
    pinMode(_buttonPin, INPUT);

    _motor.begin(stepPin, dirPin, enaPin, enaActiveLow);
    _motor.enable(true);
    applyMotion(motion);
  }

  void enableMotor(bool enable) {
    _motor.enable(enable);
  }

  void applyMotion(const Config::MotionConfig& motion) {
    setOpenTurns(motion.openTurns);
    setMaxSpeedSteps(motion.maxStepsPerSecond);
    setAccelerationSteps2(motion.accelStepsPerSecond2);
  }

  void setOpenTurns(float turns) {
    if (turns < 0.0f) turns = 0.0f;
    _openSteps = static_cast<long>(turns * static_cast<float>(_stepsPerRev) + 0.5f);
  }

  void setMaxSpeedSteps(float stepsPerSec) {
    _motor.setMaxSpeed(stepsPerSec);
  }

  void setAccelerationSteps2(float stepsPerSec2) {
    _motor.setAcceleration(stepsPerSec2);
  }

  void stop() {
    _motor.stop();
  }

  void moveRelative(long deltaSteps) {
    _motor.enable(true);
    _motor.move(deltaSteps);
  }

  void moveToSteps(long steps) {
    _motor.enable(true);
    _motor.moveTo(steps);
  }

  void emergencyStop() {
    _motor.emergencyStop();
    _motor.moveTo(_motor.currentPosition());
  }

 ButtonEvent poll() {
  const bool closeActive = readCloseLimit();
  if (closeActive && !_limitCloseLatched) {
    Serial.println("Limit FERMER");
    _limitCloseLatched = true;
    emergencyStop();
    _motor.setCurrentPosition(0);
    _motor.moveTo(0);
    _lastCalibMs = millis();
  } else if (!closeActive) {
    _limitCloseLatched = false;
  }

  const bool movingTowardOpen = _motor.targetPosition() > _motor.currentPosition();

  if (movingTowardOpen) {
    const bool openActive = readOpenLimit();
    if (openActive && !_limitOpenLatched) {
      Serial.println("Limit OUVERT");
      _limitOpenLatched = true;
      emergencyStop();
      moveToSteps(positionSteps());
    } else if (!openActive) {
      _limitOpenLatched = false;
    }
  } else {
    _limitOpenLatched = false;
  }

  _motor.run();
  return updateButton();
}

  long positionSteps() const {
    return _motor.currentPosition();
  }

  bool isMoving() const {
    return _motor.targetPosition() != _motor.currentPosition();
  }

  unsigned long lastCalibrationMs() const {
    return _lastCalibMs;
  }

private:
  bool readDebouncedLimit(uint8_t pin, bool activeLow,
                          bool& rawLast, bool& stable, unsigned long& changeMs) {
    const unsigned long now = millis();

    const int v = digitalRead(pin);
    const bool raw = activeLow ? (v == LOW) : (v == HIGH);

    if (raw != rawLast) {
      rawLast = raw;
      changeMs = now;
    }

    if ((now - changeMs) >= _debounceMs) {
      stable = raw;
    }

    return stable;
  }

  bool readCloseLimit() {
    return readDebouncedLimit(_limitClosePin, _limitCloseActiveLow,
                              _closeRawLast, _closeStable, _closeChangeMs);
  }

  bool readOpenLimit() {
    return readDebouncedLimit(_limitOpenPin, _limitOpenActiveLow,
                              _openRawLast, _openStable, _openChangeMs);
  }

  ButtonEvent updateButton() {
    if (_buttonPin < 0) return ButtonEvent::None;

    const unsigned long now = millis();
    const bool pressed = digitalRead(_buttonPin) == (_buttonActiveLow ? LOW : HIGH);

    if (pressed) {
      if (!_buttonWasPressed) {
        _buttonWasPressed = true;
        _buttonPressMs = now;
        _buttonLongSent = false;
      } else if (!_buttonLongSent && (now - _buttonPressMs) >= _longPressMs && !readCloseLimit()) {
        _buttonLongSent = true;
        return ButtonEvent::LongPress;
      }
    } else if (_buttonWasPressed) {
      ButtonEvent event = ButtonEvent::None;
      if (!_buttonLongSent && (now - _buttonPressMs) >= _debounceMs) {
        event = ButtonEvent::ShortPress;
      }
      _buttonWasPressed = false;
      _buttonLongSent = false;
      return event;
    }

    return ButtonEvent::None;
  }

  StepperKiss _motor;

  long _stepsPerRev = Config::stepsPerRevolution();
  long _openSteps = 0;

  uint8_t _limitClosePin = 255;
  uint8_t _limitOpenPin = 255;
  int8_t _buttonPin = -1;

  bool _limitCloseActiveLow = true;
  bool _limitOpenActiveLow = true;
  bool _buttonActiveLow = true;

  bool _limitCloseLatched = false;
  bool _limitOpenLatched = false;
  unsigned long _lastCalibMs = 0;

  unsigned long _debounceMs = 50;
  unsigned long _longPressMs = 5000;
  bool _buttonWasPressed = false;
  bool _buttonLongSent = false;
  unsigned long _buttonPressMs = 0;

  bool _closeRawLast = false;
  bool _closeStable = false;
  unsigned long _closeChangeMs = 0;

  bool _openRawLast = false;
  bool _openStable = false;
  unsigned long _openChangeMs = 0;
};