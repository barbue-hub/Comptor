#pragma once

#include <Arduino.h>

#include "Config.h"
#include "StepperKiss.h"

class CounterControl {
public:
  enum class ButtonEvent : uint8_t { None, ShortPress, LongPress };

  void begin(uint8_t stepPin, uint8_t dirPin, int8_t enaPin, bool enaActiveLow,
             uint8_t limitPin, bool limitActiveLow,
             int8_t buttonPin, bool buttonActiveLow,
             long stepsPerRev, const Config::MotionConfig& motion,
             unsigned long debounceMs, unsigned long longPressMs) {
    _stepsPerRev = stepsPerRev;
    _limitPin = limitPin;
    _limitActiveLow = limitActiveLow;
    _buttonPin = buttonPin;
    _buttonActiveLow = buttonActiveLow;
    _debounceMs = debounceMs;
    _longPressMs = longPressMs;

    pinMode(_limitPin, INPUT_PULLUP);
    if (_buttonPin >= 0) {
      pinMode(_buttonPin, _buttonActiveLow ? INPUT_PULLUP : INPUT);
    }

    _motor.begin(stepPin, dirPin, enaPin, enaActiveLow);
    _motor.enable(true);
    applyMotion(motion);
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

  void open() {
    _motor.enable(true);
    _motor.moveTo(_openSteps);
  }

  void close() {
    _motor.enable(true);
    _motor.moveTo(0);
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

  void seedPosition(long steps) {
    _motor.setCurrentPosition(steps);
  }

  ButtonEvent poll() {
    const bool limitActive = readLimit();
    if (limitActive && !_limitLatched) {
      _limitLatched = true;
      _motor.emergencyStop();
      _motor.setCurrentPosition(0);
      _motor.moveTo(0);
      _lastCalibMs = millis();
    } else if (!limitActive) {
      _limitLatched = false;
    }

    _motor.run();
    return updateButton();
  }

  long positionSteps() const { return _motor.currentPosition(); }

  float positionTurns() const {
    return _motor.currentPosition() / static_cast<float>(_stepsPerRev);
  }

  bool isMoving() const { return _motor.targetPosition() != _motor.currentPosition(); }

  unsigned long lastCalibrationMs() const { return _lastCalibMs; }

  long stepsPerRevolution() const { return _stepsPerRev; }

private:
  bool readLimit() const {
    const int v = digitalRead(_limitPin);
    return _limitActiveLow ? (v == HIGH) : (v == LOW);
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
      } else if (!_buttonLongSent && (now - _buttonPressMs) >= _longPressMs) {
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
  uint8_t _limitPin = 255;
  int8_t _buttonPin = -1;
  bool _limitActiveLow = true;
  bool _buttonActiveLow = true;
  bool _limitLatched = false;
  unsigned long _lastCalibMs = 0;

  unsigned long _debounceMs = 50;
  unsigned long _longPressMs = 5000;
  bool _buttonWasPressed = false;
  bool _buttonLongSent = false;
  unsigned long _buttonPressMs = 0;
};
