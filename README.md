Comptoir motorisé — ESP8266 + Nano

Pilotage d’un comptoir motorisé (NEMA23 + DM556) via ESP8266 (WebUI) et Arduino Nano (capteurs).
Le système effectue un homing automatique sur fin de course bas avec estimation de la distance fournie par la Nano.
Un bouton physique permet de commander ouverture, fermeture et arrêt localement.
Le moteur est géré par StepperKiss, assurant des rampes d’accélération et décélération fluides.

Fonctionnalités principales

Homing automatique intelligent (vitesse ajustée si mesure invalide)

Lecture série Nano : distance et température

Contrôle manuel via bouton physique (toggle open/close, arrêt en cours)

Interface Web : commandes OPEN/CLOSE/STOP et journal d’événements

Gestion complète du moteur pas-à-pas sur ESP8266

Sécurité logicielle (timeout, fin de course matérielle)

Architecture simple, modulaire et robuste

Matériel
Composant	Description
ESP8266	(ex. NodeMCU) – contrôle moteur + WebUI
Arduino Nano	mesure distance (ultrason) et température
DM556	driver pas-à-pas
NEMA 23	moteur principal
Fin de course bas	contact NO/NC, référence 0
Bouton physique	commande manuelle locale
Transmission	pignon-crémaillère (≈ 25.446 cm/tour)
Câblage (ESP8266)
Signal	GPIO	Fonction
STEP	D7 (GPIO13)	impulsions moteur
DIR	D6 (GPIO12)	direction
ENA	D5 (GPIO14)	enable actif bas
Fin de course bas	D1 (GPIO5)	entrée avec pull-up
Bouton physique	D2 (GPIO4)	entrée avec pull-up

ENA_ACTIVE_LOW = true

Structure du projet
Fichier	Rôle
PJ_001_ESP8266.ino	boucle principale, WebUI
Config.h	constantes, pins, Wi-Fi
FSM.h	machine d’états (BOOT → HOMING → IDLE → etc.)
CounterControl.h	logique moteur + bouton physique
StepperKiss.h	gestion moteur pas-à-pas
WebUI.*	interface web et logs
Séquence de homing

BOOT
Lecture distance via Nano ($DST:<cm>). Si valide, conversion en tours → pas.

HOMING_START
Si distance connue : position initialisée, moveTo(0).
Sinon : vitesse réduite + move(-kHomingTravel).

HOMING_RUN
Arrêt dès front actif du fin de course. Timeout 30 s → FAULT.

IDLE
Attente commande Web ou bouton.

Ouverture/Fermeture
Commandes relatives selon état. Stop si réappui du bouton.

Logique du bouton physique

Appui court :

En IDLE : alterne OPEN ↔ CLOSE.

En mouvement : STOP immédiat.

Appui long (≥ 5 s) :

En IDLE : lance séquence de homing forcée.

Le traitement du bouton est effectué dans CounterControl::poll() pour garantir une détection réactive même pendant le mouvement.

Communication ESP8266 ⇄ Nano
Commande	Réponse	Description
'D'	$DST:<cm>	Distance (cm)
'T'	$TMP:<°C>	Température
Timeout	-1.0	Si pas de réponse

Le parsing est robuste et tolère les préfixes parasites.

Paramètres essentiels
FULL_STEPS_PER_REV = 200
MICROSTEP_FACTOR   = 10
kStepsPerRev       = 2000
kHomingTravel      = kStepsPerRev * 40L
kHomingTimeoutMs   = 30000UL

Compilation et flash

IDE : Arduino IDE ou PlatformIO

Carte : ESP8266 (NodeMCU)

Vitesse série : 115200

Flasher séparément :

ESP8266 → contrôleur moteur

Nano → capteur distance/température

Sécurité

Fin de course matériel obligatoire

Timeout de homing (30 s)

Vitesse réduite si distance invalide

Arrêt immédiat sur front du fin de course

Dépannage rapide
Problème	Solution
“jump to case label”	Entourer la variable dans un bloc {}
Homing trop lent	Vérifier distance Nano
Pas d’arrêt sur fin de course	Vérifier logique readLimit() (LOW/HIGH)
Vitesses incorrectes après homing	Recharger kVmaxSteps / kAccelSteps2
Licence

Apache 2.0
