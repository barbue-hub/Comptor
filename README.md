# Comptoir motorisé — ESP8266 + Nano

Pilotage d’un comptoir motorisé (NEMA23 + DM556) via ESP8266 (WebUI) et Arduino Nano (capteurs). Homing sur fin de course **bas** avec estimation de distance fournie par la Nano. Moteur géré par **StepperKiss** (accélération/décélération lissées).

## Matériel

* ESP8266 (ex. NodeMCU)
* Arduino Nano (capteur distance + température)
* Driver pas-à-pas DM556 (ou équivalent)
* Moteur NEMA 23
* Fin de course bas (contact NO/NC)
* Transmission **pignon-crémaillère** (pignon pas ≈ 25.4466 cm/tour)

## Câblage (ESP8266 → Driver/Capteurs)

| Signal            | Pin ESP8266                 |
| ----------------- | --------------------------- |
| STEP              | D7 (GPIO13)                 |
| DIR               | D6 (GPIO12)                 |
| ENA               | D5 (GPIO14)                 |
| Fin de course bas | D1 (GPIO5, pull-up interne) |

> `ENA_ACTIVE_LOW = true`

## Structure du code

* `PJ_001_ESP8266.ino` — point d’entrée, setup/loop + WebUI
* `Config.h` — pins, Wi-Fi, vitesses, conversion pas
* `FSM.h` — machine d’états (BOOT, HOMING_START/RUN, IDLE, OPENING, CLOSING, STOPPING, FAULT)
* `CounterControl.*` — surcouche moteur (vitesses, limites, état)
* `StepperKiss.h` — driver pas-à-pas (move/moveTo, accel)
* `WebUI.*` — interface HTTP (log, commandes)

## Réseau

Dans `Config.h` :

```cpp
#define WIFI_SSID "..."
#define WIFI_PWD  "..."
```

L’ESP8266 sert une **WebUI** (ouvrir/fermer/stop, réglages, logs).

## Protocole ESP8266 ⇄ Nano (série)

* Envoyer `'D'` → la Nano répond `"$DST:<cm>\r\n"` (distance en cm)
* Envoyer `'T'` → `"$TMP:<°C>\r\n"`
* Le parsing est robuste (préfixe optionnel + extraction numérique).

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

## Conversion distance → pas (pignon/crémaillère)

Périmètre **au pas** du pignon (avec vos cotes) :
`C ≈ 25.4466 cm/tour`

Équations :

```
turns  = distance_cm / 25.4466
steps  = round(turns * kStepsPerRev)
```

## Logique de homing (résumé)

1. **BOOT**

   * Essaye de lire la distance sur la Nano (log informatif).
2. **HOMING_START**

   * Re-lit la distance.
   * Si `distance_cm > 0` :

     * `steps = round((distance_cm / 25.4466) * kStepsPerRev)`
     * `setCurrentPosition(steps)` (position connue au-dessus du switch)
     * `moveTo(0)` (descendre jusqu’au switch)
   * Sinon (distance invalide) :

     * Réduit `vitesse/accélération`, **move(-kHomingTravel)** (relatif)
3. **HOMING_RUN**

   * Fin si **calibration détectée** (front fin de course) ou absence de mouvement cohérente.
   * Timeout ⇒ **FAULT**.
   * En sortie (succès) : restaure `vitesse/accélération` nominales et passe **IDLE**.

## Commandes

* **OPEN** : direction +1, `open()`, état **OPENING**
* **CLOSE** : direction −1, `close()`, état **CLOSING**
* **STOP** : `stop()`, état **STOPPING**

## Build & flash

* **Arduino IDE** ou **PlatformIO**
* Board : ESP8266 (NodeMCU)
* Vitesse série : 115200
* Compiler puis flasher l’ESP8266 et la Nano (sketch capteurs) séparément.

## Sécurité & garde-fous

* Fin de course **matériel** pour la référence zéro
* Trajet de homing **généreux** (`kHomingTravel`) + **timeout** 30 s
* Vitesse/accélération **réduites** si distance capteur invalide

## Dépannage rapide

* Erreur “jump to case label” : encapsuler les déclarations **dans un bloc `{}`** à l’intérieur du `case`.
* Position inconnue : utiliser `move(Δ)` (relatif), pas `moveTo(x)` (absolu).
* Vitesses persistantes : rétablir les valeurs nominales après homing dans `HOMING_RUN`.

## Licence

MIT (ou ajuster selon vos besoins).

---

**Astuce description GitHub (<350 caract.)**

> *Comptoir motorisé (ESP8266+Nano) : NEMA23/DM556, pignon-crémaillère. Homing fin de course bas avec estimation distance ultrason. WebUI (open/close/stop, réglages, logs). Moteur via StepperKiss, sécurité timeout.*
