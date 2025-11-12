// PJ_001_ESP8266.ino — Point d'entrée de l'application Comptor.

#include <Arduino.h>

#include "ComptorApp.h"

static ComptorApp app;

void setup() {
  app.begin();
}

void loop() {
  app.loop();
}
