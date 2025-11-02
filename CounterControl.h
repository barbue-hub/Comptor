 // Classe CounterControl : gère un moteur pas-à-pas avec butée de fin de course.
// - Init : configure pins, vitesses, accélération et nombre de pas pour ouverture.
// - Commandes : open(), close(), stop().
// - poll() : surveille le fin de course, stoppe/calibre si activé, exécute moteur.
// - Accès : position en pas/tours, état du mouvement, dernier calibrage.

#pragma once
#include <Arduino.h>
#include "StepperKiss.h"

class CounterControl {
public:
  void begin(uint8_t stepPin, uint8_t dirPin, int8_t enaPin, bool enaActiveLow,
             uint8_t limitPin, bool limitActiveLow,
             long stepsPerRev, float openTurns)
  {
    _stepsPerRev = stepsPerRev;
    _openSteps = (long)(openTurns * (float)stepsPerRev + 0.5f);
    _limitPin = limitPin;
    _limitActiveLow = limitActiveLow;

    pinMode(_limitPin, INPUT_PULLUP);
    motor.begin(stepPin, dirPin, enaPin, enaActiveLow);
    motor.setMaxSpeed(_vMaxSteps);
    motor.setAcceleration(_accelSteps2);
    motor.enable(true);
  }

  void setOpenTurns(float turns)           { _openSteps = (long)(turns * (float)_stepsPerRev + 0.5f); }
  void setMaxSpeedSteps(float stepsPerSec) { _vMaxSteps = stepsPerSec; motor.setMaxSpeed(_vMaxSteps); }
  void setAccelerationSteps2(float s2)     { _accelSteps2 = s2;       motor.setAcceleration(_accelSteps2); }

  void open()  { motor.moveTo(_openSteps); }
  void close() { motor.moveTo(0); }
  void stop()  { motor.stop(); }

  void poll() {
    bool limitActive = readLimit();
    if (limitActive && !_limitLatched) {
      _limitLatched = true;
      motor.emergencyStop();
      motor.setCurrentPosition(0);
      motor.moveTo(0);
      _lastCalibMs = millis();
    } else if (!limitActive) {
      _limitLatched = false;
    }
    motor.run();
  }

  long  positionSteps() const { return motor.currentPosition(); }
  float positionTurns() const { return motor.currentPosition() / (float)_stepsPerRev; }
  unsigned long lastCalibMs() const { return _lastCalibMs; }
  bool isMoving() const { return motor.targetPosition() != motor.currentPosition(); }

  StepperKiss motor;

private:
  bool readLimit() const {
    int v = digitalRead(_limitPin);
    return _limitActiveLow ? (v == LOW) : (v == HIGH);
  }

  long _stepsPerRev = 200, _openSteps = 2000;
  uint8_t _limitPin = 255; bool _limitActiveLow = true;
  float _vMaxSteps = 1600.0f, _accelSteps2 = 2000.0f;
  bool _limitLatched = false;
  unsigned long _lastCalibMs = 0;
};
