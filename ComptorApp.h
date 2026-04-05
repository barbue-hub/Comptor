// ComptorApp : logique centrale de l'application pour Raspberry Pi Pico.
// Cette classe orchestre la machine d'états, le homing, la gestion du
// moteur et des capteurs (distance et température). Le code s'inspire de
// l'implémentation d'origine mais n'implique plus de WebUI ni de
// communication série avec une Arduino Nano. Toutes les mesures sont
// effectuées localement sur le Pico.

#pragma once

#include <Arduino.h>
#include "Config.h"
#include "CounterControl.h"


class ComptorApp {
public:
  void begin();
  void loop();

  float latestTemp() const;
  long positionSteps() const;
  bool isMoving() const;
  void requestOpen();
  void requestClose();

private:
  enum class State : uint8_t { Boot, HomingRun, Idle, Opening, Closing, Stopping, Fault };
  enum class Command : uint8_t { None, Open, Close, Stop };

  void tickStateMachine();
  void startHoming();
  void finishHomingSuccess();
  void applyTargetMotion(const Config::MotionConfig& motion);
  void applyMotionToMotor(const Config::MotionConfig& motion);
  void handleButtonEvent(CounterControl::ButtonEvent event);
  void pollTemperature();
  float measureTempC();

  CounterControl control_;
  Config::MotionConfig targetMotion_ = Config::defaultMotion();

  State state_ = State::Boot;
  Command pendingCommand_ = Command::None;
  unsigned long cycles_ = 0;
  bool openedSinceLastClose_ = false;

  unsigned long homingStartMs_ = 0;
  unsigned long lastCalibSeen_ = 0;

  float latestTempC_ = 0.0f;
  unsigned long lastTempMs_ = 0;
};