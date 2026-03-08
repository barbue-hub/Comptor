// ComptorPico.ino : portage du projet Comptor vers Raspberry Pi Pico.
//
// Ce sketch est minimal : il délègue toute la logique de pilotage au
// module ComptorApp.  Ce module se charge de gérer l’état du système,
// l’exécution du homing, la détection d’appuis sur le bouton, la
// lecture des capteurs (DS18B20 et HC‑SR04) et les commandes de
// mouvement.  Le rôle du fichier .ino se limite à instancier
// ComptorApp et à relayer les appels setup()/loop().

#include <Arduino.h>

// Inclure ComptorApp pour accéder à la logique centrale
#include "ComptorApp.h"

// Instancier l’application
static ComptorApp app;

// Arduino setup() : initialiser l’application
void setup() {
  app.begin();
}

// Arduino loop() : déléguer à ComptorApp
void loop() {
  app.loop();
}