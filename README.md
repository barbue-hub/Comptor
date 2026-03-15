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
```

---

## Logique de fonctionnement

### Démarrage
Au boot :

1. le Pico initialise les capteurs,
2. lit une distance initiale,
3. initialise le contrôle moteur,
4. passe en état `Boot`,
5. lance immédiatement un **homing**.

### Homing
Le homing :

- réduit vitesse et accélération,
- effectue un mouvement relatif vers le bas,
- attend la calibration par le fin de course,
- passe en `Fault` si le timeout est dépassé.

### Bouton physique
- **Appui court**
  - si immobile : alterne ouverture / fermeture selon la position
  - si en mouvement : demande un arrêt contrôlé
  - si déjà en arrêt contrôlé : prépare une inversion après arrêt complet

- **Appui long**
  - relance le homing

### Température
La température est relue périodiquement **uniquement quand le moteur est immobile**.

---

## Dépendances

Bibliothèques utilisées :

- `Arduino`
- `OneWire`
- `DallasTemperature`

---

## Build / flash

Le projet est prévu pour un environnement Arduino compatible Pico.

### En pratique
- Ouvrir `ComptorPico.ino`
- Sélectionner une carte **Raspberry Pi Pico**
- Installer les bibliothèques :
  - `OneWire`
  - `DallasTemperature`
- Compiler et flasher

### Moniteur série
Configurer le moniteur série à :

```txt
115200 bauds
```

---

## Logs série utiles

Exemples de messages émis :

- `[ComptorApp] Démarrage...`
- `[BOOT] Distance initiale: ... cm`
- `[FSM] Boot – lancement homing`
- `[FSM] HOMING start`
- `[FSM] Homing terminé → Idle`
- `[FSM] Homing timeout → Fault`
- `[FSM] Ouverture terminée`
- `[FSM] Fermeture terminée`
- `[FSM] Cycle complet n°...`
- `[TEMP] ... °C`

---

## Sécurité / garde-fous

- homing au démarrage,
- référence bas via fin de course,
- timeout sur homing,
- arrêt contrôlé en mouvement,
- état `Fault` si homing invalide,
- paramètres moteur centralisés.

---

## Notes

- Cette branche **remplace l’ancienne architecture ESP8266 + Nano + WebUI** par une version **plus simple et autonome** sur Pico.
- `StepperKiss` reste utilisé comme base de pilotage moteur.
- Le projet est structuré pour être facile à ajuster côté brochage, motion et logique de contrôle.

---

## Licence

Apache 2.0
