#include "ComptorApp.h"

#include <ESP.h>
#include <ESP8266WiFi.h>

#include <cstring>

#include "WebUI.h"

ComptorApp* ComptorApp::instance_ = nullptr;

bool ComptorApp::CommandQueue::push(Command cmd) {
  if (cmd == Command::None) return true;
  if (count >= 4) return false;
  data[tail] = cmd;
  tail = static_cast<uint8_t>((tail + 1) & 3u);
  ++count;
  return true;
}

bool ComptorApp::CommandQueue::pop(Command& out) {
  if (count == 0) return false;
  out = data[head];
  head = static_cast<uint8_t>((head + 1) & 3u);
  --count;
  return true;
}

void ComptorApp::CommandQueue::clear() {
  head = tail = count = 0;
}

void ComptorApp::begin() {
  instance_ = this;

  Serial.begin(115200);
  delay(100);

  targetMotion_ = Config::defaultMotion();

  control_.begin(Config::kStepPin, Config::kDirPin, Config::kEnablePin, Config::kEnableActiveLow,
                 Config::kLimitBottomPin, Config::kLimitActiveLow,
                 Config::kButtonPin, Config::kButtonActiveLow,
                 Config::stepsPerRevolution(), targetMotion_,
                 Config::kButtonDebounceMs, Config::kButtonLongPressMs);

  applyMotionToMotor(targetMotion_);
  updateMotionInUi();

  WebUI::setCallbacks(onOpen, onClose, onStop, onSetTurns, onSetSpeed, onSetAccel, onGetStatus);
  WebUI::begin(Config::kWifiSsid, Config::kWifiPassword);
  WebUI::addLog("[FSM] Boot");
}

void ComptorApp::loop() {
  const auto event = control_.poll();
  if (event != CounterControl::ButtonEvent::None) {
    handleButtonEvent(event);
  }

  if (!control_.isMoving()) {
    WebUI::loop();
  }

  tickStateMachine();

  if (control_.isMoving()) {
    yield();
  }

  if (!control_.isMoving()) {
    pollTemperature();
  }
}

void ComptorApp::requestCommand(Command cmd) {
  if (cmd == Command::None) return;
  if (cmd == Command::Stop) {
    pendingCommand_ = Command::Stop;
    queue_.clear();
    return;
  }

  if (pendingCommand_ == Command::None) {
    pendingCommand_ = cmd;
  } else if (!queue_.push(cmd)) {
    WebUI::addLog("[WARN] Command queue full");
  }
}

void ComptorApp::tickStateMachine() {
  if ((state_ == State::Idle || state_ == State::Fault) && pendingCommand_ == Command::None) {
    Command next;
    if (queue_.pop(next)) {
      pendingCommand_ = next;
    }
  }

  switch (pendingCommand_) {
    case Command::Stop:
      if (state_ == State::Fault) {
        state_ = State::HomingStart;
        pendingCommand_ = Command::None;
        WebUI::addLog("[FSM] HOMING START");
      } else {
        control_.stop();
        state_ = State::Stopping;
        pendingCommand_ = Command::None;
        WebUI::addLog("[FSM] STOPPING");
      }
      break;

    case Command::Open:
      if (state_ == State::Idle) {
        control_.open();
        state_ = State::Opening;
        pendingCommand_ = Command::None;
        WebUI::addLog("[FSM] OPENING");
      } else if (state_ == State::Closing) {
        control_.stop();
        state_ = State::Stopping;
        if (!queue_.push(Command::Open)) {
          WebUI::addLog("[WARN] Command queue full");
        }
        pendingCommand_ = Command::None;
        WebUI::addLog("[FSM] STOPPING");
      } else {
        pendingCommand_ = Command::None;
      }
      break;

    case Command::Close:
      if (state_ == State::Idle) {
        control_.close();
        state_ = State::Closing;
        pendingCommand_ = Command::None;
        WebUI::addLog("[FSM] CLOSING");
      } else if (state_ == State::Opening) {
        control_.stop();
        state_ = State::Stopping;
        if (!queue_.push(Command::Close)) {
          WebUI::addLog("[WARN] Command queue full");
        }
        pendingCommand_ = Command::None;
        WebUI::addLog("[FSM] STOPPING");
      } else {
        pendingCommand_ = Command::None;
      }
      break;

    case Command::Home:
      if (state_ == State::Idle || state_ == State::Fault) {
        state_ = State::HomingStart;
        pendingCommand_ = Command::None;
        WebUI::addLog("[FSM] HOMING START");
      } else if (state_ == State::Opening || state_ == State::Closing) {
        control_.stop();
        state_ = State::Stopping;
        if (!queue_.push(Command::Home)) {
          WebUI::addLog("[WARN] Command queue full");
        }
        pendingCommand_ = Command::None;
        WebUI::addLog("[FSM] STOPPING");
      } else {
        pendingCommand_ = Command::None;
      }
      break;

    case Command::None:
    default:
      break;
  }

  switch (state_) {
    case State::Boot: {
      bootDistanceCm_ = readNanoDistance();
      if (bootDistanceCm_ >= 0.0f) {
        WebUI::addLog(String("[FSM] Tours before close: ") + String(bootDistanceCm_ / Config::kGearCmPerTurn));
      } else {
        WebUI::addLog("[FSM] Distance invalid or no reply");
      }
      state_ = State::HomingStart;
      WebUI::addLog("[FSM] HOMING START");
    } break;

    case State::HomingStart:
      startHoming();
      break;

    case State::HomingRun: {
      const bool homed = (control_.lastCalibrationMs() != lastCalibSeen_);
      const bool reached = (!control_.isMoving() && control_.positionSteps() == 0);
      const bool timeout = (millis() - homingStartMs_) > Config::kHomingTimeoutMs;
      if (homed || reached) {
        finishHomingSuccess();
      } else if (timeout) {
        state_ = State::Fault;
        WebUI::addLog("[FSM] FAULT");
      }
    } break;

    case State::Opening:
      if (!control_.isMoving()) {
        openedSinceLastClose_ = true;
        state_ = State::Idle;
        WebUI::addLog("[FSM] IDLE");
      }
      break;

    case State::Closing:
      if (!control_.isMoving()) {
        if (openedSinceLastClose_) {
          ++cycles_;
          openedSinceLastClose_ = false;
        }
        state_ = State::Idle;
        WebUI::addLog("[FSM] IDLE");
      }
      break;

    case State::Stopping:
      if (!control_.isMoving()) {
        state_ = State::Idle;
        WebUI::addLog("[FSM] IDLE");
      }
      break;

    case State::Idle:
    case State::Fault:
      break;
  }
}

void ComptorApp::startHoming() {
  float distanceCm = bootDistanceCm_;
  bootDistanceCm_ = -1.0f;
  if (distanceCm < 0.0f) {
    distanceCm = readNanoDistance();
  }

  const long estimatedSteps = Config::cmToSteps(distanceCm);

  homingStartMs_ = millis();
  lastCalibSeen_ = control_.lastCalibrationMs();
  motionOverridden_ = false;

  if (estimatedSteps > 0) {
    control_.seedPosition(estimatedSteps);
    control_.moveToSteps(0);
    WebUI::addLog(String("[FSM] Distance OK, steps=") + String(estimatedSteps));
  } else {
    Config::MotionConfig slow = targetMotion_;
    if (slow.maxStepsPerSecond <= 0.0f) slow.maxStepsPerSecond = 1.0f;
    if (slow.accelStepsPerSecond2 <= 0.0f) slow.accelStepsPerSecond2 = 1.0f;
    slow.maxStepsPerSecond *= Config::kHomingSpeedFactor;
    slow.accelStepsPerSecond2 *= Config::kHomingAccelFactor;
    if (slow.maxStepsPerSecond < 1.0f) slow.maxStepsPerSecond = 1.0f;
    if (slow.accelStepsPerSecond2 < 1.0f) slow.accelStepsPerSecond2 = 1.0f;
    applyMotionToMotor(slow);
    motionOverridden_ = true;
    control_.moveRelative(-Config::kHomingTravelSteps);
    WebUI::addLog("[FSM] Distance invalid, slow homing");
  }

  state_ = State::HomingRun;
  WebUI::addLog("[FSM] HOMING");
}

void ComptorApp::finishHomingSuccess() {
  applyMotionToMotor(targetMotion_);
  motionOverridden_ = false;
  state_ = State::Idle;
  lastCalibSeen_ = control_.lastCalibrationMs();
  WebUI::addLog("[FSM] IDLE");
}

void ComptorApp::applyTargetMotion(const Config::MotionConfig& motion) {
  targetMotion_ = motion;
  if (!motionOverridden_) {
    applyMotionToMotor(targetMotion_);
  }
  updateMotionInUi();
}

void ComptorApp::applyMotionToMotor(const Config::MotionConfig& motion) {
  control_.applyMotion(motion);
}

void ComptorApp::updateMotionInUi() const {
  WebUI::setOpenTurns(targetMotion_.openTurns);
  WebUI::setSpeedDisplay(targetMotion_.maxStepsPerSecond);
  WebUI::setAccelDisplay(targetMotion_.accelStepsPerSecond2);
}

void ComptorApp::handleButtonEvent(CounterControl::ButtonEvent event) {
  switch (event) {
    case CounterControl::ButtonEvent::ShortPress:
      if (control_.isMoving()) {
        WebUI::addLog("[BUTTON] Stop requested");
        requestCommand(Command::Stop);
      } else if (state_ == State::Idle) {
        if (control_.positionSteps() == 0) {
          WebUI::addLog("[BUTTON] Open requested");
          requestCommand(Command::Open);
        } else {
          WebUI::addLog("[BUTTON] Close requested");
          requestCommand(Command::Close);
        }
      }
      break;

    case CounterControl::ButtonEvent::LongPress:
      WebUI::addLog("[BUTTON] Homing requested");
      requestCommand(Command::Home);
      break;

    case CounterControl::ButtonEvent::None:
    default:
      break;
  }
}

void ComptorApp::pollTemperature() {
  const unsigned long now = millis();
  if ((now - lastTempMs_) < Config::kTemperaturePollMs) return;

  const float t = readNanoTemperature();
  if (t >= 0.0f) {
    latestTempC_ = t;
  }
  lastTempMs_ = now;
}

float ComptorApp::readNanoValue(char command, const char* prefix) const {
  while (Serial.available() > 0) Serial.read();
  Serial.write(command);

  const unsigned long start = millis();
  String buffer;
  auto isPrintable = [](char c) { return c >= 32 && c <= 126; };

  while ((millis() - start) < Config::kNanoTimeoutMs) {
    while (Serial.available() > 0) {
      const char c = static_cast<char>(Serial.read());
      if (c == '\n' || c == '\r') {
        if (buffer.length()) {
          String value = buffer;
          if (prefix && prefix[0]) {
            const int idx = value.indexOf(prefix);
            if (idx >= 0) {
              value = value.substring(idx + static_cast<int>(strlen(prefix)));
            }
          }
          int firstDigit = 0;
          while (firstDigit < value.length() &&
                 !((value[firstDigit] >= '0' && value[firstDigit] <= '9') || value[firstDigit] == '-' || value[firstDigit] == '.')) {
            ++firstDigit;
          }
          value = value.substring(firstDigit);
          value.trim();
          return value.toFloat();
        }
      } else if (isPrintable(c)) {
        buffer += c;
      }
    }
    yield();
  }

  return -1.0f;
}

float ComptorApp::readNanoDistance() const {
  return readNanoValue('D', "$DST:");
}

float ComptorApp::readNanoTemperature() const {
  return readNanoValue('T', "$TMP:");
}

void ComptorApp::onOpen() {
  if (!instance_) return;
  WebUI::addLog("[UI] Open requested");
  instance_->requestCommand(Command::Open);
}

void ComptorApp::onClose() {
  if (!instance_) return;
  WebUI::addLog("[UI] Close requested");
  instance_->requestCommand(Command::Close);
}

void ComptorApp::onStop() {
  if (!instance_) return;
  WebUI::addLog("[UI] Stop requested");
  instance_->requestCommand(Command::Stop);
}

void ComptorApp::onSetTurns(float turns) {
  if (!instance_ || !(turns > 0.0f)) return;
  Config::MotionConfig updated = instance_->targetMotion_;
  updated.openTurns = turns;
  instance_->applyTargetMotion(updated);
}

void ComptorApp::onSetSpeed(float speedStepsPerSec) {
  if (!instance_ || !(speedStepsPerSec > 0.0f)) return;
  Config::MotionConfig updated = instance_->targetMotion_;
  updated.maxStepsPerSecond = speedStepsPerSec;
  instance_->applyTargetMotion(updated);
}

void ComptorApp::onSetAccel(float accelStepsPerSec2) {
  if (!instance_ || !(accelStepsPerSec2 > 0.0f)) return;
  Config::MotionConfig updated = instance_->targetMotion_;
  updated.accelStepsPerSecond2 = accelStepsPerSec2;
  instance_->applyTargetMotion(updated);
}

void ComptorApp::onGetStatus(void* out) {
  if (!instance_ || !out) return;
  auto* status = reinterpret_cast<WebUI_Status*>(out);
  status->tempC = instance_->latestTempC_;
  status->lastCalibMs = instance_->control_.lastCalibrationMs();
  status->posTurns = instance_->control_.positionTurns();
  status->cycles = instance_->cycles_;
  status->ip = WiFi.localIP();
  status->uptimeSec = millis() / 1000UL;
  const uint32_t freeHeap = ESP.getFreeHeap();
  const long used = 100 - static_cast<long>(freeHeap * 100UL / 80000UL);
  status->usedRamPercent = static_cast<int>(constrain(used, 0L, 100L));
  status->cpuMHz = ESP.getCpuFreqMHz();
  status->chipId = ESP.getChipId();
}

