# Comptor — Raspberry Pi Pico

Pilotage d’un comptoir motorisé via **Raspberry Pi Pico**, **driver DM556** et **moteur pas-à-pas**.

Cette branche (`pico_main`) est un **portage du projet Comptor vers le Pico**.  
La logique de contrôle est maintenant **entièrement locale** : plus de WebUI, plus de communication série avec une Arduino Nano pour les capteurs. Le Pico gère directement :

- le moteur pas-à-pas,
- le fin de course bas,
- le bouton physique,
- le capteur de distance **HC-SR04**,
- le capteur de température **DS18B20**.

---

## Fonctionnalités

- **Homing automatique au démarrage**
- **Commande locale par bouton physique**
  - appui court : ouvrir / fermer / stop selon l’état
  - appui long : relance du homing
- **Machine d’états claire**
  - `Boot`
  - `HomingRun`
  - `Idle`
  - `Opening`
  - `Closing`
  - `Stopping`
  - `Fault`
- **Mesure de distance** avec HC-SR04
- **Mesure de température** avec DS18B20
- **Comptage des cycles complets**
- **Pilotage moteur lissé** via `StepperKiss`
- **Timeout de sécurité** sur homing

---

## Architecture du projet

- `ComptorPico.ino`  
  Point d’entrée minimal. Initialise l’application et délègue toute la logique à `ComptorApp`.

- `ComptorApp.h / ComptorApp.cpp`  
  Logique centrale :
  - machine d’états,
  - homing,
  - lecture des capteurs,
  - gestion du bouton,
  - orchestration des commandes moteur.

- `Config.h`  
  Paramètres du système :
  - brochage,
  - conversion pas/tour,
  - vitesse / accélération,
  - paramètres de homing,
  - debounce / long press.

- `CounterControl.h`  
  Surcouche de contrôle moteur + gestion du fin de course + bouton physique.

- `StepperKiss.h`  
  Driver stepper léger avec accélération/décélération et comportement anti-stutter.

---

## Matériel

- Raspberry Pi Pico
- Driver pas-à-pas **DM556** (ou équivalent)
- Moteur pas-à-pas
- Fin de course bas
- Bouton physique
- Capteur ultrason **HC-SR04**
- Sonde température **DS18B20**
- Transmission mécanique du comptoir

---

## Brochage actuel

### Moteur / commande
| Fonction | GPIO Pico |
|---|---:|
| STEP | GPIO8 |
| DIR | GPIO7 |
| ENABLE | GPIO6 |
| Fin de course bas | GPIO21 |
| Bouton physique | GPIO22 |

### Capteurs
| Fonction | GPIO Pico |
|---|---:|
| HC-SR04 Trigger | GPIO11 |
| HC-SR04 Echo | GPIO12 |
| DS18B20 (1-Wire) | GPIO13 |

> Ajuste les pins dans `Config.h` et `ComptorApp.cpp` selon ton câblage.

---

## Paramètres principaux

Dans `Config.h` :

- `kFullStepsPerRev = 200`
- `kMicrostepFactor = 10`
- `stepsPerRevolution() = 2000`
- `openTurns = 10.0`
- `kGearCmPerTurn = 25.4466`
- `kHomingTravelSteps = stepsPerRevolution() * 40`
- `kHomingTimeoutMs = 30000`
- `kButtonDebounceMs = 50`
- `kButtonLongPressMs = 5000`

Conversion distance → pas :

```cpp
steps = (distance_cm / 25.4466) * stepsPerRevolution()
