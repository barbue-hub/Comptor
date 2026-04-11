// ComptorApp.cpp : implémentation de la logique centrale pour la version
// Raspberry Pi Pico du projet Comptor. Cette classe orchestre le
// pilotage du moteur pas-à-pas via CounterControl, l’exécution du
// homing, la lecture des capteurs de température et la gestion des
// commandes issues du bouton. Aucun serveur web n’est utilisé : toutes
// les interactions passent par le bouton physique et l’interface série.

#include "ComptorApp.h"

#include <OneWire.h>
#include <DallasTemperature.h>

static float clampf(float v, float vmin, float vmax) {
  if (v < vmin) return vmin;
  if (v > vmax) return vmax;
  return v;
}

// Temps de conversion DS18B20 selon résolution configurée.
// 9 bits ≈ 93.75 ms, on prend une petite marge.
static constexpr unsigned long TEMP_CONVERSION_MS = 100;

// Instances globales des capteurs OneWire et DallasTemperature.
static OneWire oneWire1(Config::kOneWireBus1Pin);
static OneWire oneWire2(Config::kOneWireBus2Pin);

static DallasTemperature sensors1(&oneWire1);
static DallasTemperature sensors2(&oneWire2);

// État interne simple pour gestion non bloquante du DS18B20
static bool tempConversionInProgress = false;
static unsigned long tempRequestMs = 0;
static bool faultTemp = false;

//------------------------------------------------------------------------
// Mesure des capteurs
//------------------------------------------------------------------------

// Lecture de la température une fois la conversion terminée.
// Cette fonction NE lance PAS la conversion, elle lit seulement la valeur.
float ComptorApp::measureTempC() {
  float t1 = sensors1.getTempCByIndex(0);
  float t2 = sensors2.getTempCByIndex(0);

  bool ok1 = !(t1 == DEVICE_DISCONNECTED_C || isnan(t1));
  bool ok2 = !(t2 == DEVICE_DISCONNECTED_C || isnan(t2));

  if (ok1 && ok2) return (t1 + t2) * 0.5f;
  if (ok1) return t1;
  if (ok2) return t2;
  return NAN;
}

//------------------------------------------------------------------------
// Initialisation et boucle principale
//------------------------------------------------------------------------

void ComptorApp::begin() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[ComptorApp] Démarrage...");

  // Initialiser les capteurs de température en mode non bloquant
  sensors1.begin();
  sensors2.begin();

  sensors1.setResolution(9);
  sensors2.setResolution(9);
  sensors1.setWaitForConversion(false);
  sensors2.setWaitForConversion(false);

  // Conserver la config par défaut comme source de vérité logique
  targetMotion_ = Config::defaultMotion();

  // Initialiser le contrôle moteur et les périphériques
  // CounterControl::begin() applique déjà la motion reçue.
  control_.begin(Config::kStepPin, Config::kDirPin, Config::kEnablePin, Config::kEnableActiveLow,
                 Config::klimitClosePin, Config::kLimitCloseActiveLow,
                 Config::kLimitOpenPin, Config::kLimitOpenActiveLow,
                 Config::kButtonPin, Config::kButtonActiveLow,
                 Config::stepsPerRevolution(), targetMotion_,
                 Config::kButtonDebounceMs, Config::kButtonLongPressMs);

  // Démarrer en état Boot afin de lancer immédiatement un homing
  state_ = State::Boot;
  pendingCommand_ = Command::None;
  cycles_ = 0;
  openedSinceLastClose_ = false;
  lastTempMs_ = millis();
  latestTempC_ = NAN;
}

void ComptorApp::loop() {
  CounterControl::ButtonEvent evt = control_.poll();
  handleButtonEvent(evt);

  tickStateMachine();
  pollTemperature();
}

//------------------------------------------------------------------------
// Gestion de la machine d’états
//------------------------------------------------------------------------

void ComptorApp::startHoming() {
  control_.setMaxSpeedSteps(targetMotion_.maxStepsPerSecond * Config::kHomingSpeedFactor);
  control_.setAccelerationSteps2(targetMotion_.accelStepsPerSecond2 * Config::kHomingAccelFactor);

  control_.moveRelative(-Config::kHomingTravelSteps);

  homingStartMs_ = millis();
  lastCalibSeen_ = control_.lastCalibrationMs();

  state_ = State::HomingRun;
  Serial.println("[FSM] HOMING start");
}

void ComptorApp::finishHomingSuccess() {

  control_.setMaxSpeedSteps(targetMotion_.maxStepsPerSecond);
  control_.setAccelerationSteps2(targetMotion_.accelStepsPerSecond2);

  state_ = State::Idle;
  Serial.println("[FSM] Homing terminé -> Idle");
}

void ComptorApp::applyTargetMotion(const Config::MotionConfig& motion) {
  control_.setMaxSpeedSteps(motion.maxStepsPerSecond);
  control_.setAccelerationSteps2(motion.accelStepsPerSecond2);
}

void ComptorApp::applyMotionToMotor(const Config::MotionConfig& motion) {
  applyTargetMotion(motion);

  long openSteps = static_cast<long>(
      motion.openTurns * static_cast<float>(Config::stepsPerRevolution()) + 0.5f);

  if (pendingCommand_ == Command::Open) {
    control_.moveToSteps(openSteps);
    state_ = State::Opening;
    Serial.println("[FSM] Commande ouverture");
  } else if (pendingCommand_ == Command::Close) {
    control_.moveToSteps(0);
    state_ = State::Closing;
    Serial.println("[FSM] Commande fermeture");
  }

  pendingCommand_ = Command::None;
}

void ComptorApp::handleButtonEvent(CounterControl::ButtonEvent event) {
  if (event == CounterControl::ButtonEvent::None) return;

  // Long press autorisé même en Fault
  if (event == CounterControl::ButtonEvent::LongPress) {
    if (state_ == State::Fault || state_ == State::Idle) {
      Serial.println("[BUTTON] Long press - relance homing");
      startHoming();
    }
    return;
  }

  // En Fault, on ignore seulement les appuis courts
  if (state_ == State::Fault) {
    Serial.println("[BUTTON] Ignoré (Fault actif)");
    return;
  }

  // Appui court
  if (control_.isMoving()) {
    if (state_ != State::Stopping) {
      // 1er clic pendant mouvement = stop + mémoriser l'inverse
      if (state_ == State::Opening) {
        pendingCommand_ = Command::Close;
      } else if (state_ == State::Closing) {
        pendingCommand_ = Command::Open;
      }

      Serial.println("[BUTTON] Stop demandé");
      control_.stop();
      state_ = State::Stopping;

    } else {
      // 2e clic pendant ralentissement = inverser la commande déjà prévue
      if (pendingCommand_ == Command::Open) {
        pendingCommand_ = Command::Close;
      } else if (pendingCommand_ == Command::Close) {
        pendingCommand_ = Command::Open;
      }

      Serial.print("[BUTTON] Commande inversée -> ");
      Serial.println(static_cast<int>(pendingCommand_));
    }

  } else {
    if (state_ == State::Idle) {
      long pos = control_.positionSteps();
      if (pos == 0) {
        pendingCommand_ = Command::Open;
      } else {
        pendingCommand_ = Command::Close;
      }
    }
  }
}

void ComptorApp::tickStateMachine() {
  switch (state_) {
    case State::Boot:
      Serial.println("[FSM] Boot - lancement homing");
      startHoming();
      break;

    case State::HomingRun:
      {
        bool homed = (control_.lastCalibrationMs() != lastCalibSeen_);
        bool timeout = (millis() - homingStartMs_) > Config::kHomingTimeoutMs;

        if (homed || (!control_.isMoving() && control_.positionSteps() == 0)) {
          finishHomingSuccess();
        } else if (timeout) {
          state_ = State::Fault;
          Serial.println("[FSM] Homing timeout -> Fault");
          control_.emergencyStop();
        }
        break;
      }

    case State::Idle:
      control_.enableMotor(Config::kEnableIDLE);
      if (pendingCommand_ != Command::None) {
        pollTemperature();
        delay(200);
        applyMotionToMotor(targetMotion_);
      }
      break;

    case State::Opening:
      if (!control_.isMoving()) {
        openedSinceLastClose_ = true;
        state_ = State::Idle;
        Serial.println("[FSM] Ouverture terminée");
      }
      break;

    case State::Closing:
      if (!control_.isMoving()) {
        if (openedSinceLastClose_) {
          cycles_++;
          openedSinceLastClose_ = false;
          Serial.print("[FSM] Cycle complet n°");
          Serial.println(cycles_);
        }
        state_ = State::Idle;
        Serial.println("[FSM] Fermeture terminée");
      }
      break;

    case State::Stopping:
      if (!control_.isMoving()) {
        if (pendingCommand_ != Command::None) {
          applyMotionToMotor(targetMotion_);
        } else {
          state_ = State::Idle;
          Serial.println("[FSM] Arrêt complet -> Idle");
        }
      }
      break;

    case State::Fault:
      pendingCommand_ = Command::None;

      if (faultTemp && !isnan(latestTempC_) && latestTempC_ <= Config::kFaultTemperatureC) {
        Serial.println("[FAULT] Température revenue normale");
        state_ = State::Idle;
      }
      break;
  }
}

//------------------------------------------------------------------------
// Température non bloquante
//------------------------------------------------------------------------

void ComptorApp::pollTemperature() {
  if (state_ != State::Idle && state_ != State::Fault) {
    return;
  }

  unsigned long now = millis();

  // Si aucune conversion n'est en cours, en démarrer une périodiquement
  if (!tempConversionInProgress) {
    if ((now - lastTempMs_) >= Config::kTemperaturePollMs) {
      sensors1.requestTemperatures();
      sensors2.requestTemperatures();
      tempRequestMs = now;
      tempConversionInProgress = true;
      lastTempMs_ = now;
    }
    return;
  }

  // Attendre la fin de conversion sans bloquer la boucle moteur
  if ((now - tempRequestMs) >= TEMP_CONVERSION_MS) {
    float t = measureTempC();

    if (!isnan(t)) {
      latestTempC_ = t;

      Serial.print("[TEMP] ");
      Serial.print(t, 2);
      Serial.println(" °C");

      if (t > Config::kFaultTemperatureC) {
        if (state_ != State::Fault) {
          control_.stop();
          faultTemp = true;
          state_ = State::Fault;
          Serial.print("[FAULT] Température > ");
          Serial.print(Config::kFaultTemperatureC, 1);
          Serial.println(" C");
        }
      } else {
        faultTemp = false;
      }
    } else {
      latestTempC_ = NAN;
      Serial.println("[TEMP] Lecture invalide");
    }

    tempConversionInProgress = false;
  }
}

float ComptorApp::latestTemp() const {
  return latestTempC_;
}

long ComptorApp::positionSteps() const {
  return control_.positionSteps();
}

bool ComptorApp::isMoving() const {
  return control_.isMoving();
}

float ComptorApp::openTurns() const {
  return targetMotion_.openTurns;
}

float ComptorApp::maxSpeedTurnsPerSecond() const {
  return targetMotion_.maxStepsPerSecond / static_cast<float>(Config::stepsPerRevolution());
}

float ComptorApp::accelTurnsPerSecond2() const {
  return targetMotion_.accelStepsPerSecond2 / static_cast<float>(Config::stepsPerRevolution());
}

void ComptorApp::requestOpen() {
  pendingCommand_ = Command::Open;
}

void ComptorApp::requestClose() {
  pendingCommand_ = Command::Close;
}

void ComptorApp::updateMotionConfigTurns(float openTurns, float speedTurnsPerSecond, float accelTurnsPerSecond2) {
  openTurns = clampf(openTurns, 0.1f, 4.0f);
  speedTurnsPerSecond = clampf(speedTurnsPerSecond, 0.05f, 2.0f);
  accelTurnsPerSecond2 = clampf(accelTurnsPerSecond2, 0.05f, 0.5f);

  targetMotion_.openTurns = openTurns;
  targetMotion_.maxStepsPerSecond = speedTurnsPerSecond * static_cast<float>(Config::stepsPerRevolution());
  targetMotion_.accelStepsPerSecond2 = accelTurnsPerSecond2 * static_cast<float>(Config::stepsPerRevolution());

  control_.setOpenTurns(targetMotion_.openTurns);

  if (state_ == State::Idle || state_ == State::Fault) {
    control_.setMaxSpeedSteps(targetMotion_.maxStepsPerSecond);
    control_.setAccelerationSteps2(targetMotion_.accelStepsPerSecond2);
  }

  Serial.print("[CFG] openTurns=");
  Serial.print(targetMotion_.openTurns, 3);
  Serial.print(" speedTurns=");
  Serial.print(maxSpeedTurnsPerSecond(), 3);
  Serial.print(" accelTurns=");
  Serial.println(accelTurnsPerSecond2, 3);
}