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
// États possibles du FSM
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
// Initialise le FSM à BOOT
static State st = State::BOOT;

// Commandes possibles du moteur
enum class Cmd : uint8_t { NONE, OPEN, CLOSE, STOP };
// Initialise à NONE
static volatile Cmd pendingCmd = Cmd::NONE;

// Variables de contrôle
static unsigned long homingStartMs = 0;
static unsigned long lastCalibSeen = 0;
static unsigned long cycles = 0;
static bool openedSinceLastClose = false;
float cm_to_tour = 0;
// -------------------- LECTURE NANO GÉNÉRIQUE --------------------
// cmdChar : caractère de commande à envoyer (ex.: 'D' pour distance, 'T' pour température)
// prefix  : préfixe attendu dans la réponse (ex.: "$DST:", "$TMP:")
// timeoutMs : durée max (ms) d'attente de la réponse.
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

// Wrappers conviviaux
inline float requestNanoDistance(unsigned long timeoutMs = 2000) {
  return requestNanoValue('D', "$DST:", timeoutMs);
}
inline float requestNanoTemperature(unsigned long timeoutMs = 2000) {
  return requestNanoValue('T', "$TMP:", timeoutMs);
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------
//**************************************** PENDING COMMAND ****************************************
//-------------------------------------------------------------------------------------------------------------------------------------------------------
static void fsmTick() {
  //-------------------------------------------------------------------------------
  //******************************* PENDING COMMAND *******************************
  //-------------------------------------------------------------------------------
  switch (pendingCmd) {
    // --------------- PENDING CMD STOP --------------
    case Cmd::STOP:
      ctrl.stop();
      st = State::STOPPING;
      pendingCmd = Cmd::NONE;
      WebUI::addLog("[FSM] STOPPING");
      break;

    // --------------- PENDING CMD OPEN --------------
    case Cmd::OPEN:
      if (st == State::IDLE) {
        // fixer la direction d'ouverture (+1)
        ctrl.motor.setDirection(1);
        ctrl.open();
        st = State::OPENING;
        pendingCmd = Cmd::NONE;
        WebUI::addLog("[FSM] OPENING");
      } else if (st == State::CLOSING) {
        ctrl.stop();
        st = State::STOPPING;
        WebUI::addLog("[FSM] STOPPING");
      }
      break;

    // --------------- PENDING CMD CLOSE --------------
    case Cmd::CLOSE:
      if (st == State::IDLE) {
        // fixer la direction de fermeture (-1)
        ctrl.motor.setDirection(-1);
        ctrl.close();
        st = State::CLOSING;
        pendingCmd = Cmd::NONE;
        WebUI::addLog("[FSM] CLOSING");
      } else if (st == State::OPENING) {
        ctrl.stop();
        st = State::STOPPING;
        WebUI::addLog("[FSM] STOPPING");
      }
      break;

    // --------------- PENDING CMD NONE --------------
    case Cmd::NONE:
    default: break;
  }

  //-----------------------------------------------------------------------------------------------
  //******************************* GESTION DES TRANSITIONS STATE *******************************
  //-----------------------------------------------------------------------------------------------
  switch (st) {
    // --------------- STATE BOOT --------------
    case State::BOOT:
      {
        float bootDistanceCm = requestNanoDistance();
        if (bootDistanceCm >= 0.0f) {
          WebUI::addLog("[FSM] Tours avant fermeture : " + String(bootDistanceCm/25.446));
          //WebUI::addLog("[FSM] Distance avant fermeture : " + String(bootDistanceCm)); 
        } else {
          WebUI::addLog("[FSM] Distance invalide ou absence de réponse");
        }

        st = State::HOMING_START;
        WebUI::addLog("[FSM] HOMING START");
      }
      break;

    // --------------- STATE HOMING START --------------
    case State::HOMING_START:
    {
      float BootDistanceTurn = (long)(requestNanoDistance()/25.446f) * kStepsPerRev + 0.5f; 
      homingStartMs = millis();
      lastCalibSeen = ctrl.lastCalibMs();

      if (BootDistanceTurn > 0){
        ctrl.motor.setCurrentPosition(BootDistanceTurn);
        ctrl.motor.setDirection(-1);
        ctrl.motor.moveTo(0);
      }
      else{
        ctrl.motor.setMaxSpeed(1600);
        ctrl.motor.setAcceleration(400);
        ctrl.motor.setDirection(-1);
        ctrl.motor.move(-kHomingTravel);
      }
      st = State::HOMING_RUN;
      WebUI::addLog("[FSM] HOMING");
      break; 
    }
    // --------------- STATE HOMING RUN --------------
    case State::HOMING_RUN:
      {
        // Terminé si latched (lastCalibMs change) ou si plus de mouvement vers 0
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
      }
      break;

    // --------------- STATE IDLE --------------
    case State::IDLE:
      break;

    // --------------- STATE OPENING --------------
    case State::OPENING:
      if (!ctrl.isMoving()) {
        openedSinceLastClose = true;
        st = State::IDLE;
        WebUI::addLog("[FSM] IDLE");
      }
      break;

    // --------------- STATE CLOSING --------------
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

    // --------------- STATE STOPPING --------------
    case State::STOPPING:
      if (!ctrl.isMoving()) st = State::IDLE;
      break;

    // --------------- STATE FAULT --------------
    case State::FAULT:
      // Rester en faute jusqu’à commande STOP, puis relancer homing
      if (pendingCmd == Cmd::STOP) {
        pendingCmd = Cmd::NONE;
        st = State::HOMING_START;
        WebUI::addLog("[FSM] HOMING START");
      }
      break;
  }
}
