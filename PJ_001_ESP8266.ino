// PJ_001_ESP8266.ino — FSM pour pilotage du stepper (ESP8266)

#include <Arduino.h>
#include <ESP.h>

#include "FSM.h"
#include "Config.h"  // constantes par défaut (moteur & UI)
#include "CounterControl.h"
#include "WebUI.h"

// -------------------- Objet moteur --------------------
// Instance unique du contrôleur moteur.  Ne pas marquer comme `static` afin que
// l'automate dans FSM.h puisse y accéder via une déclaration `extern`.
CounterControl ctrl;

// -------------------------------------------------------------------------------------
// Mesure de température cachée (non bloquante)
static float latestTempC = 0.0f;
static unsigned long lastTempUpdateMs = 0;
static const unsigned long TEMP_UPDATE_INTERVAL_MS = 5000UL;

// -------------------- Helpers --------------------
static inline void applyMotionParams() {
  ctrl.setOpenTurns(kOpenTurns);
  ctrl.setMaxSpeedSteps(kVmaxSteps);
  ctrl.setAccelerationSteps2(kAccelSteps2);
  WebUI::setOpenTurns(kOpenTurns);
  WebUI::setSpeedDisplay(kVmaxSteps);
  WebUI::setAccelDisplay(kAccelSteps2);
}

static inline void issue(Cmd c) { pendingCmd = c; }

// -------------------- WebUI Callbacks --------------------
static void onOpen()  { issue(Cmd::OPEN);  }
static void onClose() { issue(Cmd::CLOSE); }
static void onStop()  { issue(Cmd::STOP);  }
static void onMeasure() { issue(Cmd::MEASURE); }

static void onSetTurns(float v)      { if (v > 0.0f) { kOpenTurns   = v; applyMotionParams(); } }
static void onSetSpeed(float v)      { if (v > 0.0f) { kVmaxSteps   = v; applyMotionParams(); } }
static void onSetAccel(float v)      { if (v > 0.0f) { kAccelSteps2 = v; applyMotionParams(); } }

static void getStatus(void* out_) {
  auto* out = reinterpret_cast<WebUI_Status*>(out_);
  out->tempC = latestTempC;                             // non bloquant
  out->lastCalibMs = ctrl.lastCalibMs();
  out->posTurns = ctrl.positionTurns();
  out->cycles = cycles;
  out->ip = WiFi.localIP();
  out->uptimeSec = millis() / 1000UL;
  uint32_t freeH = ESP.getFreeHeap();
  int usedPct = (int)constrain((long)(100 - (freeH * 100L / 80000L)), 0L, 100L);
  out->usedRamPercent = usedPct;
  out->cpuMHz = ESP.getCpuFreqMHz();
  out->chipId = ESP.getChipId();
}

// -------------------- Arduino --------------------
void setup() {
  Serial.begin(115200);
  delay(100);
/**/
  // Moteur + fins de course
  ctrl.begin(
    /*STEP_PIN D7*/13, /*DIR_PIN D6*/12, /*ENA_PIN D5*/14, /*ENA_ACTIVE_LOW*/true,
    /*LIMIT_BOTTOM D1*/5, /*limitActiveLow=*/true,
    kStepsPerRev, kOpenTurns);
  applyMotionParams();

  // Réseau & UI
  WebUI::setCallbacks(onOpen, onClose, onStop, onMeasure, onSetTurns, onSetSpeed, onSetAccel, getStatus);
  WebUI::begin(WIFI_SSID, WIFI_PWD);
  WebUI::addLog("[FSM] Boot");
}

void loop() {
  // ---------------- priorité aux pas moteur ----------------
  ctrl.poll();                    // limites + run() stepper

  // ---------------- UI seulement si à l'arrêt ----------------
  if (!ctrl.isMoving()) {
    WebUI::loop();                // HTTP UI
  }

  // ---------------- tick de l'automate ----------------
  fsmTick();

  // Laisser respirer le Wi-Fi si ça bouge
  if (ctrl.isMoving()) yield();

  // Température non bloquante (à l'arrêt seulement)
  if ((millis() - lastTempUpdateMs) >= TEMP_UPDATE_INTERVAL_MS && !ctrl.isMoving()) {
    float t = requestNanoTemperature();
    if (t >= 0.0f) latestTempC = t;
    lastTempUpdateMs = millis();
  }
}

// IDÉES D'AMÉLIORATIONS :
// 1. Ajouter les informations de la NANO, etc.
// 2. Vérification distance ultrason à configurer.
// 3. Détection premier cycle HOMING.
// 4. Rampe d'accélération/décélération.
