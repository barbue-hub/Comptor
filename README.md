---

# Comptoir motorisé — ESP8266 + Nano

Pilotage d’un comptoir motorisé (NEMA23 + DM556) via ESP8266 (WebUI) et Arduino Nano (capteurs).
Homing sur fin de course **bas**, avec estimation de distance fournie par la Nano.
Ajout d’un **bouton physique** (open/close/stop/homing long-press).
Moteur géré par **StepperKiss** (accélération/décélération lissées).

---

## Matériel

* ESP8266 (ex. NodeMCU)
* Arduino Nano (capteur distance + température)
* Driver pas-à-pas DM556 (ou équivalent)
* Moteur NEMA 23
* Fin de course bas (contact NO/NC)
* Bouton physique (commande locale)
* Transmission **pignon-crémaillère** (pignon pas ≈ 25.4466 cm/tour)

---

## Câblage (ESP8266 → Driver/Capteurs)

| Signal            | Pin ESP8266                 |
| ----------------- | --------------------------- |
| STEP              | D7 (GPIO13)                 |
| DIR               | D6 (GPIO12)                 |
| ENA               | D5 (GPIO14)                 |
| Fin de course bas | D1 (GPIO5, pull-up interne) |
| Bouton physique   | D2 (GPIO4, pull-up interne) |

> `ENA_ACTIVE_LOW = true`

---

## Structure du code

* `PJ_001_ESP8266.ino` — point d’entrée, setup/loop + WebUI
* `Config.h` — pins, Wi-Fi, vitesses, conversion pas
* `FSM.h` — machine d’états (BOOT, HOMING_START/RUN, IDLE, OPENING, CLOSING, STOPPING, FAULT)
* `CounterControl.*` — surcouche moteur (vitesses, limites, bouton physique)
* `StepperKiss.h` — driver pas-à-pas (move/moveTo, accel)
* `WebUI.*` — interface HTTP (log, commandes)

---

## Réseau

Dans `Config.h` :

```cpp
#define WIFI_SSID "..."
#define WIFI_PWD  "..."
```

L’ESP8266 sert une **WebUI** (ouvrir/fermer/stop, réglages, logs).

---

## Protocole ESP8266 ⇄ Nano (série)

* Envoyer `'D'` → la Nano répond `"$DST:<cm>\r\n"` (distance en cm)
* Envoyer `'T'` → la Nano répond `"$TMP:<°C>\r\n"`
* Parsing robuste avec préfixe optionnel et extraction numérique.

---

## Paramètres mouvement (extraits)

```cpp
FULL_STEPS_PER_REV = 200
MICROSTEP_FACTOR   = 10
kStepsPerRev       = 2000      // 200×10
VMAX_REV_S_DEFAULT = 1.6
ACCEL_REV_S2_DEF   = 0.2
kHomingTravel      = kStepsPerRev * 40L   // marge sûre (≈ 80 000 pas)
kHomingTimeoutMs   = 30000UL
```

---

## Conversion distance → pas (pignon/crémaillère)

Périmètre **au pas** du pignon :
`C ≈ 25.4466 cm/tour`

```
turns  = distance_cm / 25.4466
steps  = round(turns * kStepsPerRev)
```

---

## Logique de homing

1. **BOOT**

   * Lecture distance sur la Nano (log informatif).
2. **HOMING_START**

   * Si `distance_cm > 0` :

     * `steps = round((distance_cm / 25.4466) * kStepsPerRev)`
     * `setCurrentPosition(steps)`
     * `moveTo(0)` (descente jusqu’au switch)
   * Sinon :

     * vitesse/accélération réduites, `move(-kHomingTravel)`
3. **HOMING_RUN**

   * Fin si **fin de course active** (front immédiat)
   * Timeout ⇒ **FAULT**
   * En sortie : rétablit vitesses nominales, état **IDLE**

---

## Logique du bouton physique

* **Appui court :**

  * En **IDLE** → alterne OPEN ↔ CLOSE
  * En **mouvement** → STOP immédiat
* **Appui long (≥ 5 s) :**

  * En **IDLE** → relance séquence de **homing**

Détection gérée dans `CounterControl::poll()`
→ front rapide, lecture stable même pendant déplacement moteur.

---

## Commandes Web

* **OPEN** : direction +1 → `open()`, état **OPENING**
* **CLOSE** : direction −1 → `close()`, état **CLOSING**
* **STOP** : `stop()`, état **STOPPING**

---

## Build & flash

* **Arduino IDE** ou **PlatformIO**
* Board : ESP8266 (NodeMCU)
* Vitesse série : 115200
* Flasher séparément :

  * ESP8266 → contrôleur moteur
  * Nano → capteurs distance/température

---

## Sécurité & garde-fous

* Fin de course **matériel** pour la référence zéro
* Trajet de homing **généreux** (`kHomingTravel`)
* **Timeout** 30 s
* **Arrêt immédiat** sur front de fin de course
* Vitesses réduites si mesure Nano invalide

---

## Dépannage rapide

* Erreur “jump to case label” → encapsuler les déclarations dans un bloc `{}`
* Position inconnue → utiliser `move(Δ)` (relatif), pas `moveTo(x)` (absolu)
* Vitesses incorrectes → restaurer valeurs nominales après homing
* Si arrêt non immédiat sur fin de course → vérifier inversion de logique `readLimit()` (HIGH/LOW)

---

## Licence

Apache 2.0

---

