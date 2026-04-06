# Comptor — Raspberry Pi Pico

Pilotage d’un comptoir motorisé via **Raspberry Pi Pico**, **driver DM556** et **moteur pas-à-pas**.

La version principale du projet repose sur le **Pico**, qui gère localement :

- le moteur pas-à-pas,
- le fin de course bas,
- le bouton physique,
- les sondes de température,
- le homing,
- la logique de contrôle.

Les anciens éléments **ESP8266 / WebUI** sont conservés dans le dépôt à titre **d’archive / legacy** seulement.

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
- **Lecture de température** via **DS18B20**
- **Comptage des cycles complets**
- **Pilotage moteur lissé** via `StepperKiss`
- **Timeout de sécurité sur homing**
- **Fault température** si seuil dépassé

---

## Architecture du projet

- `ComptorPico.ino`  
  Point d’entrée principal. Initialise l’application et gère la boucle principale.

- `ComptorApp.h / ComptorApp.cpp`  
  Logique centrale :
  - machine d’états,
  - homing,
  - gestion du bouton,
  - lecture température,
  - orchestration des commandes moteur.

- `Config.h`  
  Paramètres du système :
  - brochage,
  - paramètres moteur,
  - conversion pas / tours / cm,
  - seuil de température,
  - paramètres de homing,
  - debounce / long press.

- `CounterControl.h`  
  Surcouche de contrôle moteur + gestion du fin de course + bouton physique.

- `StepperKiss.h`  
  Driver stepper léger avec accélération/décélération.

---

## Matériel

- Raspberry Pi Pico
- Driver pas-à-pas **DM556** (ou équivalent)
- Moteur pas-à-pas
- Fin de course bas
- Bouton physique
- 2 sondes température **DS18B20**
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

### Température

| Fonction | GPIO Pico |
|---|---:|
| DS18B20 bus 1 | GPIO28 |
| DS18B20 bus 2 | GPIO2 |

> Ajuste les pins dans `Config.h` selon ton câblage.

---

## Paramètres principaux

Dans `Config.h` :

- `kDipSwitch = 8000`
- `kOpenTurns = 3.6`
- `kSpeed = 1.6`
- `kAcceleration = 0.0002`
- `kGearCmPerTurn = 25.4466`
- `kHomingTravelSteps = stepsPerRevolution() * 20`
- `kHomingTimeoutMs = 20000`
- `kFaultTemperatureC = 40.0`
- `kButtonDebounceMs = 50`
- `kButtonLongPressMs = 5000`

Conversion distance → pas :

```cpp id="59697"
steps = (distance_cm / 25.4466) * stepsPerRevolution()
