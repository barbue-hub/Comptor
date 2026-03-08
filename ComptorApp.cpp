// ComptorApp.cpp : implémentation de la logique centrale pour la version
// Raspberry Pi Pico du projet Comptor. Cette classe orchestre le
// pilotage du moteur pas-à-pas via CounterControl, l’exécution du
// homing, la lecture des capteurs de distance et de température et la
// gestion des commandes issues du bouton. Aucun serveur web n’est
// utilisé : toutes les interactions passent par le bouton physique et
// l’interface série.

#include "ComptorApp.h"

#include <OneWire.h>
#include <DallasTemperature.h>

// Définition des broches des capteurs.
static constexpr uint8_t TRIG_PIN = 11;      // Pin trigger du HC-SR04
static constexpr uint8_t ECHO_PIN = 12;      // Pin echo du HC-SR04
static constexpr uint8_t ONE_WIRE_BUS1 = 28;  // Bus 1-Wire pour DS18B20
static constexpr uint8_t ONE_WIRE_BUS2 = 2;  // Bus 1-Wire pour DS18B20
// Temps de conversion DS18B20 selon résolution configurée.
// Ici 10 bits => ~187.5 ms, on prend une petite marge.
static constexpr unsigned long TEMP_CONVERSION_MS = 40;

// Instances globales des capteurs OneWire et DallasTemperature.
static OneWire oneWire1(ONE_WIRE_BUS1);
static OneWire oneWire2(ONE_WIRE_BUS2);

static DallasTemperature sensors1(&oneWire1);
static DallasTemperature sensors2(&oneWire2);

// État interne simple pour gestion non bloquante du DS18B20
static bool tempConversionInProgress = false;
static unsigned long tempRequestMs = 0;

//------------------------------------------------------------------------
// CommandQueue
//------------------------------------------------------------------------

bool ComptorApp::CommandQueue::push(Command cmd) {
  if (count >= 4 || cmd == Command::None) return false;
  data[tail] = cmd;
  tail = (tail + 1) % 4;
  count++;
  return true;
}

bool ComptorApp::CommandQueue::pop(Command& out) {
  if (count == 0) return false;
  out = data[head];
  head = (head + 1) % 4;
  count--;
  return true;
}

void ComptorApp::CommandQueue::clear() {
  head = tail = count = 0;
  for (uint8_t i = 0; i < 4; i++) data[i] = Command::None;
}

//------------------------------------------------------------------------
// Mesure des capteurs
//------------------------------------------------------------------------

// Mesure la distance en centimètres avec un HC-SR04.
float ComptorApp::measureDistanceCM() {
  const uint8_t nbrSamples = 10;
  float sum = 0.0f;
  uint8_t n = 0;

  for (uint8_t i = 0; i < nbrSamples; i++) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    unsigned long dur = pulseIn(ECHO_PIN, HIGH, 25000UL);  // timeout ≈ 4 m
    if (dur > 0) {
      float d = dur * 0.01715f;  // conversion µs -> cm
      sum += d;
      n++;
    }
    delay(60);
  }

  if (n == 0) return NAN;
  return sum / n;
}

// Lecture de la température une fois la conversion terminée.
// Cette fonction NE lance PAS la conversion, elle lit seulement la valeur.
float ComptorApp::measureTempC() {
  float t1 = sensors1.getTempCByIndex(0);
  float t2 = sensors2.getTempCByIndex(0);

  bool ok1 = !(t1 == DEVICE_DISCONNECTED_C || isnan(t1));
  bool ok2 = !(t2 == DEVICE_DISCONNECTED_C || isnan(t2));

if (ok1 && ok2) {Serial.print("double");}

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

  // Configurer les broches des capteurs
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // Initialiser le capteur de température en mode non bloquant
  sensors1.begin();
sensors2.begin();

sensors1.setResolution(9);
sensors2.setResolution(9);
  sensors1.setWaitForConversion(false);
  sensors2.setWaitForConversion(false);

  // Amorcer une première conversion
  sensors1.requestTemperatures();
  tempRequestMs = millis();
  tempConversionInProgress = true;
  sensors2.requestTemperatures();
  tempRequestMs = millis();
  tempConversionInProgress = true;
  // Initialiser le contrôle moteur et les périphériques
  control_.begin(Config::kStepPin, Config::kDirPin, Config::kEnablePin, Config::kEnableActiveLow,
                 Config::kLimitBottomPin, Config::kLimitActiveLow,
                 Config::kButtonPin, Config::kButtonActiveLow,
                 Config::stepsPerRevolution(), Config::defaultMotion(),
                 Config::kButtonDebounceMs, Config::kButtonLongPressMs);

  // Appliquer la configuration motion par défaut
  targetMotion_ = Config::defaultMotion();
  control_.setOpenTurns(targetMotion_.openTurns);
  control_.setMaxSpeedSteps(targetMotion_.maxStepsPerSecond);
  control_.setAccelerationSteps2(targetMotion_.accelStepsPerSecond2);

  // Lire une distance de référence au boot
  bootDistanceCm_ = measureDistanceCM();
  if (!isnan(bootDistanceCm_)) {
    Serial.print("[BOOT] Distance initiale: ");
    Serial.print(bootDistanceCm_, 2);
    Serial.println(" cm");
  } else {
    Serial.println("[BOOT] Aucune distance valide détectée");
  }

  // Démarrer en état Boot afin de lancer immédiatement un homing
  state_ = State::Boot;
  pendingCommand_ = Command::None;
  motionOverridden_ = false;
  cycles_ = 0;
  openedSinceLastClose_ = false;
  lastTempMs_ = millis();
  latestTempC_ = NAN;
  queue_.clear();
}

void ComptorApp::loop() {
  CounterControl::ButtonEvent evt = control_.poll();
  handleButtonEvent(evt);

  tickStateMachine();

  // Maintenant permis aussi pendant le mouvement, sans blocage
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

  long openSteps = (long)(motion.openTurns * (float)Config::stepsPerRevolution() + 0.5f);

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

  if (event == CounterControl::ButtonEvent::LongPress) {
    if (state_ == State::Fault || state_ == State::Idle) {
      Serial.println("[BUTTON] Long press - relance homing");
      startHoming();
    }
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
    } else if (state_ == State::Fault) {
      Serial.println("[BUTTON] Relance homing");
      startHoming();
    }
  }
}

void ComptorApp::tickStateMachine() {
  switch (state_) {
    case State::Boot:
      Serial.println("[FSM] Boot - lancement homing");
      startHoming();
      break;

    case State::HomingStart:
      break;

    case State::HomingRun: {
      bool homed = (control_.lastCalibrationMs() != lastCalibSeen_);
      bool timeout = (millis() - homingStartMs_) > Config::kHomingTimeoutMs;

      if (homed || (!control_.isMoving() && control_.positionSteps() == 0)) {
        finishHomingSuccess();
      } else if (timeout) {
        state_ = State::Fault;
        Serial.println("[FSM] Homing timeout -> Fault");
      }
      break;
    }

    case State::Idle:
      if (pendingCommand_ != Command::None) {
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
      break;
  }
}

//------------------------------------------------------------------------
// Température non bloquante
//------------------------------------------------------------------------

void ComptorApp::pollTemperature() {
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
    } else {
      Serial.println("[TEMP] Lecture invalide");
    }

    tempConversionInProgress = false;
  }
}