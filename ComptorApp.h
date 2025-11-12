#pragma once

#include "Config.h"
#include "CounterControl.h"

class ComptorApp {
public:
  void begin();
  void loop();

private:
  enum class State : uint8_t { Boot, HomingStart, HomingRun, Idle, Opening, Closing, Stopping, Fault };
  enum class Command : uint8_t { None, Open, Close, Stop, Home };

  struct CommandQueue {
    Command data[4] = {Command::None, Command::None, Command::None, Command::None};
    uint8_t head = 0;
    uint8_t tail = 0;
    uint8_t count = 0;

    bool push(Command cmd);
    bool pop(Command& out);
    void clear();
  };

  static ComptorApp* instance_;

  static void onOpen();
  static void onClose();
  static void onStop();
  static void onSetTurns(float turns);
  static void onSetSpeed(float speedStepsPerSec);
  static void onSetAccel(float accelStepsPerSec2);
  static void onGetStatus(void* out);

  void requestCommand(Command cmd);
  void tickStateMachine();
  void startHoming();
  void finishHomingSuccess();
  void applyTargetMotion(const Config::MotionConfig& motion);
  void applyMotionToMotor(const Config::MotionConfig& motion);
  void updateMotionInUi() const;
  void handleButtonEvent(CounterControl::ButtonEvent event);
  void pollTemperature();
  float readNanoValue(char command, const char* prefix) const;
  float readNanoDistance() const;
  float readNanoTemperature() const;

  CounterControl control_;
  CommandQueue queue_;

  Config::MotionConfig targetMotion_ = Config::defaultMotion();
  bool motionOverridden_ = false;

  State state_ = State::Boot;
  Command pendingCommand_ = Command::None;
  unsigned long cycles_ = 0;
  bool openedSinceLastClose_ = false;

  unsigned long homingStartMs_ = 0;
  unsigned long lastCalibSeen_ = 0;
  float bootDistanceCm_ = -1.0f;

  float latestTempC_ = 0.0f;
  unsigned long lastTempMs_ = 0;
};

