// Classe StepperKiss : driver pas-à-pas sans à-coups (surtout pour ESP8266).
// - Init : configure pins, vitesse max, accélération, enable.
// - Commandes : moveTo(), move(), stop(), emergencyStop().
// - run() : planifie et exécute au plus 1 pas par appel (pas de rafales).
// - Gestion douce accel/décel + I/O rapides optionnelles (fast GPIO).

#pragma once
#include <Arduino.h>

// ---- Import des options (peuvent définir KISS_USE_FAST_GPIO / KISS_MIN_PULSE_US / etc.)
#include "Config.h"

// ---- Valeurs par défaut si non définies dans Config.h
#ifndef KISS_USE_FAST_GPIO
  #define KISS_USE_FAST_GPIO 1
#endif

#ifndef KISS_MIN_PULSE_US
  #define KISS_MIN_PULSE_US 6
#endif

// ---- Paramètres d'amorçage / intégration ----
// Vitesse minimale utilisée pour calculer l'intervalle du (des) premiers pas
#ifndef KISS_MIN_START_SPS
  #define KISS_MIN_START_SPS 2.0f
#endif
// Plafond de l'intervalle entre pas (µs) lors du démarrage/lente vitesse
#ifndef KISS_MAX_STEP_INTERVAL_US
  #define KISS_MAX_STEP_INTERVAL_US 50000UL
#endif
// Limite du pas d'intégration (dt) pour éviter un énorme a*dt après longue inactivité
#ifndef KISS_MAX_DT_S
  #define KISS_MAX_DT_S 0.05f  // 50 ms
#endif

// Pour les écritures GPIO rapides sur ESP8266 (GPOS/GPOC)
#if defined(ARDUINO_ARCH_ESP8266)
  extern "C" {
    #include "gpio.h"
  }
#endif

// StepperKiss — Version anti-stutter pour ESP8266
// - Planificateur à micros(): 1 seul pas max par run()
// - Pas de "rafales" même si loop() prend du retard
// - I/O rapides sur ESP8266 (GPOS/GPOC)
class StepperKiss {
public:
  StepperKiss() {}

  // initialisation.
  void begin(uint8_t stepPin, uint8_t dirPin, int8_t enaPin = -1, bool enaActiveLow = true) {
    _stepPin = stepPin;
    _dirPin  = dirPin;
    _enaPin  = enaPin;
    _enaActiveLow = enaActiveLow;

    pinMode(_stepPin, OUTPUT);
    pinMode(_dirPin, OUTPUT);
    digitalWrite(_stepPin, LOW);
    digitalWrite(_dirPin, LOW);

    if (_enaPin >= 0) {
      pinMode(_enaPin, OUTPUT);
      enable(false);
    }

    _lastUpdateUs = micros();
    _nextStepUs   = 0;        // non planifié
    _stepIntervalUs = 1000;   // valeur sûre temp.
  }

  //Rend les valeurs de speed et acceleration noob proof (valeurs négatives impossible)
  void setMaxSpeed(float stepsPerSec)      { if (stepsPerSec < 0) stepsPerSec = -stepsPerSec; _maxSpeed = stepsPerSec < 1.0f ? 1.0f : stepsPerSec; }
  void setAcceleration(float stepsPerSec2) { if (stepsPerSec2 < 0) stepsPerSec2 = -stepsPerSec2; _accel = stepsPerSec2 < 1.0f ? 1.0f : stepsPerSec2; }

  //Gestion de la pin ENABLE
  void enable(bool on) {
    if (_enaPin < 0) return;
    if (_enaActiveLow) digitalWrite(_enaPin, on ? LOW : HIGH);
    else               digitalWrite(_enaPin, on ? HIGH : LOW);
    _enabled = on;
  }

  // retourne l’état mémorisé (_enabled)
  bool enabled() const { return _enabled; }

  // provient de CounterControl.h
  void moveTo(long target) {
    _target = target;
    if (fabsf(_speed) < 1e-3f && _target != _position) {
      const int dir = (_target > _position) ? 1 : -1;
      _speed = dir * KISS_MIN_START_SPS;
    }
  }

  void move(long delta)    { moveTo(_position + delta); }
  void setCurrentPosition(long p) { _position = p; }
  long currentPosition() const { return _position; }
  long targetPosition()  const { return _target; }
  float speed()          const { return _speed; }

  void stop() {
    // place une cible pour s'arrêter en douceur
    int dir = (_speed >= 0.0f) ? 1 : -1;
    long stepsToStop = (long)((_speed * _speed) / (2.0f * _accel) + 0.5f);
    if (stepsToStop < 1) stepsToStop = 1;
    _target = _position + dir * stepsToStop;
  }

  void emergencyStop() {
    _speed = 0.0f;
    _nextStepUs = 0;
  }

  // Retourne true si un pas vient d'être émis
  bool run() {
    const unsigned long now = micros();
    float dt = (now - _lastUpdateUs) * 1e-6f;
    if (dt < 0) dt = 0; // empêche dt d'être négatif

    // Distance restante à parcourir en nombre de pas (stepsRemaining).
    long stepsRemaining = llabs(_target - _position);

    // Si proche de l'arrêt et cible atteinte -> repos (NE PAS rafraîchir _lastUpdateUs)
    if (stepsRemaining == 0 && fabsf(_speed) < 1e-3f) {
      _speed = 0.0f;
      _nextStepUs = 0;
      return false;
    }

    // On quitte l'état repos -> on reprend l'intégration
    _lastUpdateUs = now;

    // Evite grande accélération après longue inactivité
    if (dt > KISS_MAX_DT_S) dt = KISS_MAX_DT_S;

    // Choisir l'accélération en fonction de la phase et de la direction demandée
    int desiredDir = 0;
    if (_target > _position) desiredDir = 1;
    else if (_target < _position) desiredDir = -1;
    else if (fabsf(_speed) > 1e-3f) desiredDir = (_speed >= 0.0f) ? 1 : -1;

    float stepsToStop = (_speed * _speed) / (2.0f * _accel);
    bool decelPhase = (stepsToStop >= (float)stepsRemaining);
    float a = decelPhase
                ? -((_speed >= 0.0f) ? 1.0f : -1.0f) * _accel
                : desiredDir * _accel;

    // Intégration vitesse + clamp
    _speed += a * dt;
    if (_speed >  _maxSpeed) _speed =  _maxSpeed;
    if (_speed < -_maxSpeed) _speed = -_maxSpeed;

    // Intervalle souhaité (us) en fonction de la vitesse instantanée
    float sps = fabsf(_speed);
    if (sps < KISS_MIN_START_SPS) sps = KISS_MIN_START_SPS;  // amorçage doux

    unsigned long stepInterval = (unsigned long)(1000000.0f / sps);
    if (stepInterval > KISS_MAX_STEP_INTERVAL_US)
      stepInterval = KISS_MAX_STEP_INTERVAL_US;
    _stepIntervalUs = stepInterval;

    // Planification initiale si nécessaire
    if (_nextStepUs == 0) _nextStepUs = now + _stepIntervalUs;

    // Émettre AU PLUS UN pas ici (pas de rafale)
    if ((long)(now - _nextStepUs) >= 0 && stepsRemaining != 0 && desiredDir != 0) {
      pulseStep(desiredDir);
      _position += desiredDir;

      // Replanifie le prochain pas à partir de "maintenant"
      _nextStepUs = now + _stepIntervalUs;
      return true;
    }

    return false;
  }

private:
  // I/O rapides facultatives (ESP8266)
  inline void writeDirFast(bool high) {
  #if defined(ARDUINO_ARCH_ESP8266)
    if (KISS_USE_FAST_GPIO) { if (high) GPOS = (1u << _dirPin); else GPOC = (1u << _dirPin); return; }
  #endif
    digitalWrite(_dirPin, high ? HIGH : LOW);
  }

  inline void pulseStep(int stepDir) {
    bool newHigh = (stepDir > 0);
    if (newHigh != _dirHigh) {
      writeDirFast(newHigh);
      delayMicroseconds(5);   // setup time DIR->STEP
      _dirHigh = newHigh;
    } else {
      writeDirFast(newHigh);
    }

    // impulsion STEP
    digitalWrite(_stepPin, HIGH);
    delayMicroseconds(KISS_MIN_PULSE_US);
    digitalWrite(_stepPin, LOW);
  }

  uint8_t _stepPin = 255, _dirPin = 255;
  int8_t  _enaPin = -1;
  bool    _enaActiveLow = true, _enabled = false;
  bool    _dirHigh = false;

  volatile long  _position = 0, _target = 0;

  float _maxSpeed = 2000.0f;   // steps/s
  float _accel    = 2000.0f;   // steps/s^2
  float _speed    = 0.0f;      // steps/s

  unsigned long _lastUpdateUs = 0;
  unsigned long _nextStepUs   = 0;
  unsigned long _stepIntervalUs = 1000;

};
