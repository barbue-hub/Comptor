// FSM.h
#pragma once
#include <Arduino.h>
#include "Config.h"
#include "WebUI.h"
#include "CounterControl.h"
#include <string.h>

extern CounterControl ctrl;
extern const long kHomingTravel;
extern const unsigned long kHomingTimeoutMs;

static bool distanceRequested = false;
static unsigned long distanceReqMs = 0;
static float bootDistanceCm = -1.0f;
static String distRxBuffer;

// -------------------- FSM --------------------
enum class State : uint8_t {
  BOOT,
  HOMING_START,
  HOMING_RUN,
  IDLE,
  OPENING,
  CLOSING,
  STOPPING,
  FAULT
};
static State st = State::BOOT;

enum class Cmd : uint8_t { NONE, OPEN, CLOSE, STOP };
static volatile Cmd pendingCmd = Cmd::NONE;

// Variables de contrôle
static unsigned long homingStartMs = 0;
static unsigned long lastCalibSeen = 0;
static unsigned long cycles = 0;
static bool openedSinceLastClose = false;

// Historique mouvement
static Cmd lastMotion = Cmd::CLOSE;   // dernier mouvement réellement lancé

// --------- Petite file d'attente de commandes (FIFO) ----------
static Cmd cmdQ[4];
static uint8_t qHead = 0, qTail = 0, qCount = 0;

static inline bool qPush(Cmd c){
  if (qCount == 4) return false;          // file pleine -> ignorer
  cmdQ[qTail] = c;
  qTail = (uint8_t)((qTail + 1) & 3);
  qCount++;
  return true;
}
static inline bool qPop(Cmd &out){
  if (!qCount) return false;
  out = cmdQ[qHead];
  qHead = (uint8_t)((qHead + 1) & 3);
  qCount--;
  return true;
}

// -------------------- LECTURE NANO GÉNÉRIQUE --------------------
static inline float requestNanoValue(char cmdChar, const char* prefix, unsigned long timeoutMs = 2000) {
  while (Serial.available() > 0) Serial.read();  // purge
  Serial.write(cmdChar);
  unsigned long startMs = millis();
  String buf;

  auto isPrintable = [](char c) { return c >= 32 && c <= 126; };

  while (millis() - startMs < timeoutMs) {
    while (Serial.available() > 0) {
      char c = (char)Serial.read();

      if (c == '\n' || c == '\r') {
        if (buf.length()) {
          String s;
          if (prefix && prefix[0]) {
            int p = buf.indexOf(prefix);
            s = (p >= 0) ? buf.substring(p + strlen(prefix)) : buf;
          } else {
            s = buf;
          }

          // Fallback : sauter tout préfixe non numérique
          int i = 0;
          while (i < (int)s.length() && !((s[i] >= '0' && s[i] <= '9') || s[i] == '-' || s[i] == '.')) i++;
          s = s.substring(i);
          s.trim();

          float v = s.toFloat();  // peut être 0.0 si rien d’exploitable
          return v;
        }
      } else if (isPrintable(c)) {
        buf += c;  // garder seulement ASCII imprimable
      }
    }
    yield();
  }
  return -1.0f;  // timeout / erreur
}

inline float requestNanoDistance(unsigned long timeoutMs = 2000) {
  return requestNanoValue('D', "$DST:", timeoutMs);
}
inline float requestNanoTemperature(unsigned long timeoutMs = 2000) {
  return requestNanoValue('T', "$TMP:", timeoutMs);
}

// -------------------- HELPERS --------------------
static inline void serviceButton() {
  if (!ctrl.readButton()) return;

  WebUI::addLog(String("[DEBUG] Button: st=") + String((int)st) +
                ", moving=" + (ctrl.isMoving() ? "true" : "false"));

  if (ctrl.isMoving()) {
    if (st != State::STOPPING) {
      pendingCmd = Cmd::STOP;                          // appui en mouvement -> stop
      WebUI::addLog("[BUTTON] Stop requested");
    } else {
      // appui durant STOPPING -> programmer l'inverse après l'arrêt
      qPush((lastMotion == Cmd::OPEN) ? Cmd::CLOSE : Cmd::OPEN);
      WebUI::addLog("[BUTTON] Queued inverse after stop");
    }
  } else if (st == State::IDLE) {
    // Toggle à l'arrêt: si pos==0 -> OPEN, sinon -> CLOSE
    Cmd want = (ctrl.positionSteps() == 0) ? Cmd::OPEN : Cmd::CLOSE;
    if (pendingCmd == Cmd::NONE) pendingCmd = want;
    else (void)qPush(want);
    WebUI::addLog(want == Cmd::OPEN ? "[BUTTON] Open requested" : "[BUTTON] Close requested");
  }
}

static inline void startHomingFromSensor() {
  float d_cm = requestNanoDistance();
  long bootSteps = (long)((d_cm / 25.446f) * kStepsPerRev + 0.5f);

  homingStartMs = millis();
  lastCalibSeen = ctrl.lastCalibMs();

  if (bootSteps > 0) {
    ctrl.motor.setCurrentPosition(bootSteps); // position connue au-dessus du switch
    ctrl.motor.setDirection(-1);
    ctrl.motor.moveTo(0);                     // aller vers 0
    WebUI::addLog("[FSM] Distance OK, steps=" + String(bootSteps));
  } else {
    ctrl.setMaxSpeedSteps(1600);
    ctrl.setAccelerationSteps2(400);
    ctrl.motor.setDirection(-1);
    ctrl.motor.move(-kHomingTravel);          // homing relatif lent
    WebUI::addLog("[FSM] Distance invalid, slow homing");
  }
  st = State::HOMING_RUN;
  WebUI::addLog("[FSM] HOMING");
}

static inline void handleStoppingCompletion() {
  if (!ctrl.isMoving()) {
    st = State::IDLE;
    if (pendingCmd == Cmd::NONE) {
      Cmd next;
      if (qPop(next)) pendingCmd = next;      // exécuter la commande en file, s'il y en a
    }
  }
}

static inline void maybePullQueueWhileIdle() {
  if (st == State::IDLE && pendingCmd == Cmd::NONE) {
    Cmd next;
    if (qPop(next)) pendingCmd = next;
  }
}

// -------------------- FSM CORE --------------------
static void fsmTick() {
  // Bouton
  serviceButton();

  // Tirer une commande éventuelle quand on est au repos (si rien de pending)
  maybePullQueueWhileIdle();

  // PENDING COMMAND
  switch (pendingCmd) {
    case Cmd::STOP:
      ctrl.stop();
      st = State::STOPPING;
      pendingCmd = Cmd::NONE;
      WebUI::addLog("[FSM] STOPPING");
      break;

    case Cmd::OPEN:
      if (st == State::IDLE) {
        ctrl.motor.setDirection(1);
        ctrl.open();
        st = State::OPENING;
        pendingCmd = Cmd::NONE;
        lastMotion = Cmd::OPEN;
        WebUI::addLog("[FSM] OPENING");
      } else if (st == State::CLOSING) {
        ctrl.stop();
        st = State::STOPPING;
        WebUI::addLog("[FSM] STOPPING");
      }
      break;

    case Cmd::CLOSE:
      if (st == State::IDLE) {
        ctrl.motor.setDirection(-1);
        ctrl.close();
        st = State::CLOSING;
        pendingCmd = Cmd::NONE;
        lastMotion = Cmd::CLOSE;
        WebUI::addLog("[FSM] CLOSING");
      } else if (st == State::OPENING) {
        ctrl.stop();
        st = State::STOPPING;
        WebUI::addLog("[FSM] STOPPING");
      }
      break;

    case Cmd::NONE:
    default: break;
  }

  // STATES
  switch (st) {
    case State::BOOT: {
      float bootDistanceCmLocal = requestNanoDistance();
      if (bootDistanceCmLocal >= 0.0f) {
        WebUI::addLog("[FSM] Tours before close: " + String(bootDistanceCmLocal / 25.446f));
      } else {
        WebUI::addLog("[FSM] Distance invalid or no reply");
      }
      st = State::HOMING_START;
      WebUI::addLog("[FSM] HOMING START");
    } break;

    case State::HOMING_START: {
      startHomingFromSensor();
    } break;

    case State::HOMING_RUN: {
      bool homed = (ctrl.lastCalibMs() != lastCalibSeen);
      bool timeout = (millis() - homingStartMs) > kHomingTimeoutMs;
      if (homed || (!ctrl.isMoving() && ctrl.motor.currentPosition() == 0)) {
        ctrl.setMaxSpeedSteps(kVmaxSteps);
        ctrl.setAccelerationSteps2(kAccelSteps2);
        st = State::IDLE;
        WebUI::addLog("[FSM] IDLE");
      } else if (timeout) {
        st = State::FAULT;
        WebUI::addLog("[FSM] FAULT");
      }
    } break;

    case State::IDLE:
      // rien
      break;

    case State::OPENING:
      if (!ctrl.isMoving()) {
        openedSinceLastClose = true;
        st = State::IDLE;
        WebUI::addLog("[FSM] IDLE");
      }
      break;

    case State::CLOSING:
      if (!ctrl.isMoving()) {
        if (openedSinceLastClose) {
          cycles++;
          openedSinceLastClose = false;
        }
        st = State::IDLE;
        WebUI::addLog("[FSM] IDLE");
      }
      break;

    case State::STOPPING:
      handleStoppingCompletion();
      break;

    case State::FAULT:
      // rester en faute jusqu’à STOP, puis relancer homing
      if (pendingCmd == Cmd::STOP) {
        pendingCmd = Cmd::NONE;
        st = State::HOMING_START;
        WebUI::addLog("[FSM] HOMING START");
      }
      break;
  }
}
