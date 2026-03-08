// ComptorApp.cpp : implémentation de la logique centrale pour la version
// Raspberry Pi Pico du projet Comptor.  Cette classe orchestre le
// pilotage du moteur pas‑à‑pas via CounterControl, l’exécution du
// homing, la lecture des capteurs de distance et de température et la
// gestion des commandes issues du bouton.  Aucun serveur web n’est
// utilisé : toutes les interactions passent par le bouton physique et
// l’interface série.

#include "ComptorApp.h"

#include <OneWire.h>
#include <DallasTemperature.h>

// Définition des broches des capteurs.  Vous pouvez les modifier ici
// selon votre câblage sur le Pi Pico.  Ces constantes ne sont pas
// exposées dans le header pour limiter la pollution du namespace global.
static constexpr uint8_t TRIG_PIN = 11;      // Pin trigger du HC‑SR04
static constexpr uint8_t ECHO_PIN = 12;      // Pin echo du HC‑SR04
static constexpr uint8_t ONE_WIRE_BUS = 13;  // Bus 1‑Wire pour DS18B20

// Instances globales des capteurs OneWire et DallasTemperature.  Elles
// doivent être statiques pour survivre à l’appel de begin().
static OneWire oneWire(ONE_WIRE_BUS);
static DallasTemperature sensors(&oneWire);

//------------------------------------------------------------------------
// CommandQueue
//------------------------------------------------------------------------

bool ComptorApp::CommandQueue::push(Command cmd) {
  // Ne pas ajouter si la file est pleine ou si la commande est None.
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

// Mesure la distance en centimètres avec un HC‑SR04.  Retourne NAN en cas
// d’échec (aucun écho reçu dans le délai imparti).  La mesure est
// moyennée sur plusieurs échantillons pour réduire le bruit.
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
    unsigned long dur = pulseIn(ECHO_PIN, HIGH, 25000UL);  // timeout ≈4 m
    if (dur > 0) {
      float d = dur * 0.01715f;  // conversion µs→cm (v/2)
      sum += d;
      n++;
    }
    delay(60);  // laisser mourir l’écho
  }
  if (n == 0) return NAN;
  return sum / n;
}

// Mesure la température en degrés Celsius via un DS18B20.  Retourne NAN en
// cas d’échec (sonde non détectée ou bus occupé).  L’index 0 est
// utilisé car un seul capteur est présent sur le bus 1‑Wire.
float ComptorApp::measureTempC() {
  sensors.requestTemperatures();
  float t = sensors.getTempCByIndex(0);
  return isnan(t) ? NAN : t;
}

//------------------------------------------------------------------------
// Initialisation et boucle principale
//------------------------------------------------------------------------

void ComptorApp::begin() {
  // Démarrer la communication série
  Serial.begin(115200);
  delay(200);
  Serial.println("[ComptorApp] Démarrage...");

  // Configurer les broches des capteurs
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // Initialiser le capteur de température
  sensors.begin();
  sensors.setResolution(10);

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

  // Lire une distance de référence au boot (optionnel).  Cela peut
  // servir à calibrer l’ouverture en fonction de la hauteur détectée.
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

// Boucle principale appelée depuis loop() de l’arduino
void ComptorApp::loop() {
  // Mettre à jour le moteur et lire l’état du bouton
  CounterControl::ButtonEvent evt = control_.poll();
  handleButtonEvent(evt);

  // Exécuter la machine d’états en fonction du contexte
  tickStateMachine();

  // Mettre à jour périodiquement la température lorsque le moteur est à l’arrêt
  pollTemperature();
}

//------------------------------------------------------------------------
// Gestion de la machine d’états
//------------------------------------------------------------------------

// Lance un homing : réduit la vitesse/accélération, déplace le moteur
// vers le bas jusqu’à toucher le capteur de fin de course.  Cet appel
// met l’état interne à HomingRun.
void ComptorApp::startHoming() {
  // Appliquer des vitesses réduites pour le homing
  control_.setMaxSpeedSteps(targetMotion_.maxStepsPerSecond * Config::kHomingSpeedFactor);
  control_.setAccelerationSteps2(targetMotion_.accelStepsPerSecond2 * Config::kHomingAccelFactor);

  // Effectuer un mouvement relatif vers le bas
  control_.moveRelative(-Config::kHomingTravelSteps);

  // Enregistrer l’instant du début et la dernière calibration connue
  homingStartMs_ = millis();
  lastCalibSeen_ = control_.lastCalibrationMs();

  state_ = State::HomingRun;
  Serial.println("[FSM] HOMING start");
}

// Terminer un homing avec succès : réinitialise les vitesses et passe à l’état Idle
void ComptorApp::finishHomingSuccess() {
  // Restaurer les paramètres de vitesse/accélération
  control_.setMaxSpeedSteps(targetMotion_.maxStepsPerSecond);
  control_.setAccelerationSteps2(targetMotion_.accelStepsPerSecond2);

  // Signaler la réussite et passer en Idle
  state_ = State::Idle;
  Serial.println("[FSM] Homing terminé → Idle");
}

// Appliquer le motion cible (max speed/accel) à la classe CounterControl
void ComptorApp::applyTargetMotion(const Config::MotionConfig& motion) {
  control_.setMaxSpeedSteps(motion.maxStepsPerSecond);
  control_.setAccelerationSteps2(motion.accelStepsPerSecond2);
}

// Appliquer un mouvement absolu ou relatif au moteur en fonction de la
// commande.  Ce helper permet d’ajouter facilement des appels à moveTo
// ou moveRelative avec les paramètres adéquats.
void ComptorApp::applyMotionToMotor(const Config::MotionConfig& motion) {
  applyTargetMotion(motion);
  // Calculer le nombre de pas pour l’ouverture complète
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

// Gérer un événement du bouton en fonction de l’état actuel.  Les
// appuis courts et longs déclenchent des commandes différentes.
void ComptorApp::handleButtonEvent(CounterControl::ButtonEvent event) {
  if (event == CounterControl::ButtonEvent::None) return;
  if (event == CounterControl::ButtonEvent::LongPress) {
    // Un appui long lance un nouveau homing si l’on est en faute ou à l’arrêt
    if (state_ == State::Fault || state_ == State::Idle) {
      Serial.println("[BUTTON] Long press – relance homing");
      startHoming();
    }
    return;
  }

  // Appui court
  if (control_.isMoving()) {
    // Si le moteur est en mouvement : stopper en douceur ou inverser après arrêt
    if (state_ != State::Stopping) {
      Serial.println("[BUTTON] Stop demandé");
      // Demander un arrêt contrôlé
      control_.stop();
      state_ = State::Stopping;
    } else {
      // Si déjà en arrêt, planifier l’inversion de sens après l’arrêt complet
      long pos = control_.positionSteps();
      long openSteps = (long)(targetMotion_.openTurns * (float)Config::stepsPerRevolution() + 0.5f);
      if (pos == 0) {
        // on était en bas : programmer une ouverture
        pendingCommand_ = Command::Open;
        Serial.println("[BUTTON] Inversion après arrêt → ouverture");
      } else {
        // on était en haut ou en cours : programmer une fermeture
        pendingCommand_ = Command::Close;
        Serial.println("[BUTTON] Inversion après arrêt → fermeture");
      }
    }
  } else {
    // Le moteur est à l’arrêt
    if (state_ == State::Idle) {
      // Basculer entre ouverture et fermeture selon la position
      long pos = control_.positionSteps();
      if (pos == 0) {
        pendingCommand_ = Command::Open;
      } else {
        pendingCommand_ = Command::Close;
      }
    } else if (state_ == State::Fault) {
      // En état de faute : relancer homing
      Serial.println("[BUTTON] Relance homing");
      startHoming();
    }
  }
}

// Machine d’états principale.  Cette fonction gère le cycle de vie du
// système en fonction de l’état courant et des actions en cours.
void ComptorApp::tickStateMachine() {
  switch (state_) {
    case State::Boot:
      // Au démarrage, lancer immédiatement un homing
      Serial.println("[FSM] Boot – lancement homing");
      startHoming();
      break;

    case State::HomingStart:
      // Cet état n’est pas utilisé explicitement : startHoming() passe
      // directement à HomingRun.  On l’inclut pour complétude.
      break;

    case State::HomingRun: {
      bool homed = (control_.lastCalibrationMs() != lastCalibSeen_);
      bool timeout = (millis() - homingStartMs_) > Config::kHomingTimeoutMs;
      if (homed || (!control_.isMoving() && control_.positionSteps() == 0)) {
        // Calibration réussie
        finishHomingSuccess();
      } else if (timeout) {
        state_ = State::Fault;
        Serial.println("[FSM] Homing timeout → Fault");
      }
      break;
    }

    case State::Idle:
      // À l’arrêt : exécuter une commande en file si disponible
      if (pendingCommand_ != Command::None) {
        applyMotionToMotor(targetMotion_);
      }
      break;

    case State::Opening:
      // Vérifier l’achèvement du mouvement
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
      // Attendre que le moteur ait fini de ralentir puis exécuter la
      // commande différée éventuelle ou revenir à l’Idle
      if (!control_.isMoving()) {
        if (pendingCommand_ != Command::None) {
          applyMotionToMotor(targetMotion_);
        } else {
          state_ = State::Idle;
          Serial.println("[FSM] Arrêt complet → Idle");
        }
      }
      break;

    case State::Fault:
      // Rester en faute jusqu’à ce qu’un appui long relance le homing
      break;
  }
}

// Met à jour périodiquement la température quand le moteur est immobile
void ComptorApp::pollTemperature() {
  if (control_.isMoving()) return;
  unsigned long now = millis();
  if ((now - lastTempMs_) >= Config::kTemperaturePollMs) {
    float t = measureTempC();
    if (!isnan(t)) {
      latestTempC_ = t;
      Serial.print("[TEMP] ");
      Serial.print(t, 2);
      Serial.println(" °C");
    }
    lastTempMs_ = now;
  }
}
