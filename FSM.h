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

// Mémorise la dernière commande OPEN/CLOSE exécutée ou programmée.
// L’état initial est CLOSE pour qu’un premier appui demande une ouverture.
static Cmd lastToggle = Cmd::CLOSE;

// Variables de contrôle
static unsigned long homingStartMs = 0;
static unsigned long lastCalibSeen = 0;
static unsigned long cycles = 0;
static bool openedSinceLastClose = false;
float cm_to_tour = 0;

// -------------------- LECTURE NANO GÉNÉRIQUE --------------------
static inline float requestNanoValue(char cmdChar, const char* prefix, unsigned long timeoutMs = 2000) {
  while (Serial.available() > 0) Serial.read();  // purge
  Serial.write(cmdChar);
  unsigned long startMs = millis();
  String buf;

  auto isPrintable = [](char c) {
    return c >= 32 && c <= 126;
  };

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
  // Gestion du bouton : si appui, soit on arrête le mouvement en cours, soit on inverse la commande lorsque le moteur est à l’arrêt.
  if (ctrl.readButton()) {
    WebUI::addLog(String("[DEBUG] Bouton appuyé: st=") + String((int)st) +
                  ", moving=" + (ctrl.isMoving() ? "true" : "false") +
                  ", lastToggle=" + (lastToggle == Cmd::OPEN ? "OPEN" : "CLOSE"));

    // Si un mouvement est en cours, demander un arrêt
    if (ctrl.isMoving()) {
      if (st != State::STOPPING) {
        pendingCmd = Cmd::STOP;
        WebUI::addLog("[BUTTON] Stop demandé");
      }
    }
    // Sinon, inverser la commande en IDLE
    else if (st == State::IDLE) {
      lastToggle = (lastToggle == Cmd::OPEN ? Cmd::CLOSE : Cmd::OPEN);
      pendingCmd = lastToggle;
      if (pendingCmd == Cmd::OPEN) {
        WebUI::addLog("[BUTTON] Ouverture demandée");
      } else {
        WebUI::addLog("[BUTTON] Fermeture demandée");
      }
    }
  }

  //----------------------------- PENDING COMMAND -----------------------------
  switch (pendingCmd) {
    case Cmd::STOP:
      // Arrêt immédiat : stoppe le moteur et revient en IDLE
      ctrl.stop();
      st = State::IDLE;
      WebUI::addLog("[DEBUG] pendingCmd STOP → IDLE");
      WebUI::addLog("[FSM] IDLE");
      pendingCmd = Cmd::NONE;
      break;

    case Cmd::OPEN:
      if (st == State::IDLE) {
        // mémoriser la direction demandée immédiatement
        lastToggle = Cmd::OPEN;
        // fixer la direction d’ouverture (+1)
        ctrl.motor.setDirection(1);
        ctrl.open();
        st = State::OPENING;
        WebUI::addLog("[DEBUG] pendingCmd OPEN → OPENING");
        WebUI::addLog("[FSM] OPENING");
      } else if (st == State::CLOSING) {
        // arrêt immédiat si on était en fermeture
        ctrl.stop();
        st = State::IDLE;
        WebUI::addLog("[DEBUG] pendingCmd OPEN during CLOSING → IDLE");
        WebUI::addLog("[FSM] IDLE");
      }
      pendingCmd = Cmd::NONE;
      break;

    case Cmd::CLOSE:
      if (st == State::IDLE) {
        // mémoriser la direction demandée immédiatement
        lastToggle = Cmd::CLOSE;
        // fixer la direction de fermeture (−1)
        ctrl.motor.setDirection(-1);
        ctrl.close();
        st = State::CLOSING;
        WebUI::addLog("[DEBUG] pendingCmd CLOSE → CLOSING");
        WebUI::addLog("[FSM] CLOSING");
      } else if (st == State::OPENING) {
        // arrêt immédiat si on était en ouverture
        ctrl.stop();
        st = State::IDLE;
        WebUI::addLog("[DEBUG] pendingCmd CLOSE during OPENING → IDLE");
        WebUI::addLog("[FSM] IDLE");
      }
      pendingCmd = Cmd::NONE;
      break;

    case Cmd::NONE:
    default:
      break;
  }

  //--------------- GESTION DES TRANSITIONS STATE ---------------
  switch (st) {
    case State::BOOT: {
      float bootDistanceCm = requestNanoDistance();
      if (bootDistanceCm >= 0.0f) {
        WebUI::addLog("[FSM] Tours avant fermeture : " + String(bootDistanceCm / 25.446));
      } else {
        WebUI::addLog("[FSM] Distance invalide ou absence de réponse");
      }
      st = State::HOMING_START;
      WebUI::addLog("[FSM] HOMING START");
    } break;

    case State::HOMING_START: {
      float BootDistanceTurn = (requestNanoDistance() / 25.446f) * kStepsPerRev;
      homingStartMs = millis();
      lastCalibSeen = ctrl.lastCalibMs();
      if (BootDistanceTurn > 0.2f) {
        ctrl.motor.setCurrentPosition(BootDistanceTurn);
        ctrl.motor.setDirection(-1);
        ctrl.motor.moveTo(0);
        WebUI::addLog("[FSM] Distance sécuritaire pour ouverture contrôlée " + String(BootDistanceTurn));
      } else {
        ctrl.motor.setMaxSpeed(1600);
        ctrl.motor.setAcceleration(400);
        ctrl.motor.setDirection(-1);
        ctrl.motor.move(-kHomingTravel);
        WebUI::addLog("[FSM] Distance non-sécuritaire, ouverture lente " + String(BootDistanceTurn));
      }
      st = State::HOMING_RUN;
      WebUI::addLog("[FSM] HOMING");
      break;
    }

    case State::HOMING_RUN: {
      bool homed = (ctrl.lastCalibMs() != lastCalibSeen);
      bool timeout = (millis() - homingStartMs) > kHomingTimeoutMs;
      if (homed || (!ctrl.isMoving() && ctrl.motor.currentPosition() == 0)) {
        ctrl.setMaxSpeedSteps(kVmaxSteps);
        ctrl.setAccelerationSteps2(kAccelSteps2);
        st = State::IDLE;
        lastToggle = Cmd::CLOSE;  // après homing, le volet est fermé
        WebUI::addLog("[FSM] IDLE");
      } else if (timeout) {
        st = State::FAULT;
        WebUI::addLog("[FSM] FAULT");
      }
    } break;

    case State::IDLE:
      break;

    case State::OPENING:
      if (!ctrl.isMoving()) {
        openedSinceLastClose = true;
        lastToggle = Cmd::OPEN;
        st = State::IDLE;
        WebUI::addLog("[DEBUG] Fin OPENING -> IDLE");
        WebUI::addLog("[FSM] IDLE");
      }
      break;

    case State::CLOSING:
      if (!ctrl.isMoving()) {
        if (openedSinceLastClose) {
          cycles++;
          openedSinceLastClose = false;
        }
        lastToggle = Cmd::CLOSE;
        st = State::IDLE;
        WebUI::addLog("[DEBUG] Fin CLOSING -> IDLE");
        WebUI::addLog("[FSM] IDLE");
      }
      break;

    case State::STOPPING:
      // Traiter STOPPING comme IDLE : plus de ralentissement intermédiaire
      st = State::IDLE;
      break;

    case State::FAULT:
      if (pendingCmd == Cmd::STOP) {
        pendingCmd = Cmd::NONE;
        st = State::HOMING_START;
        WebUI::addLog("[FSM] HOMING START");
      }
      break;
  }
}
