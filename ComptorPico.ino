#include <Arduino.h>
#include <SoftwareSerial.h>
#include "ComptorApp.h"

#define RX_PIN 10
#define TX_PIN 11
#define ESP_BAUD 9600

SoftwareSerial espSerial(RX_PIN, TX_PIN);
static ComptorApp app;

static unsigned long lastSendMs = 0;
static const unsigned long kSendPeriodMs = 1000;

static char espLine[32];
static uint8_t espIdx = 0;

void sendState() {
  const float temp = app.latestTemp();
  const long pos = app.positionSteps();
  const bool moving = app.isMoving();

  espSerial.print("T:");
  if (isnan(temp)) {
    espSerial.print("NaN");
  } else {
    espSerial.print(temp, 2);
  }

  espSerial.print(";P:");
  espSerial.print(pos);
  espSerial.print(";M:");
  espSerial.println(moving ? 1 : 0);
}

void handleEspCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "OPEN") {
    app.requestOpen();
    Serial.println("[ESP] OPEN");
  } else if (cmd == "CLOSE") {
    app.requestClose();
    Serial.println("[ESP] CLOSE");
  }
}

void readEspNonBlocking() {
  while (espSerial.available()) {
    char c = (char)espSerial.read();

    if (c == '\r') continue;

    if (c == '\n') {
      espLine[espIdx] = '\0';
      if (espIdx > 0) {
        handleEspCommand(String(espLine));
      }
      espIdx = 0;
    } else {
      if (espIdx < sizeof(espLine) - 1) {
        espLine[espIdx++] = c;
      } else {
        espIdx = 0;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  espSerial.begin(ESP_BAUD);
  app.begin();
}

void loop() {
  app.loop();

  readEspNonBlocking();

  if (millis() - lastSendMs >= kSendPeriodMs) {
    sendState();
    lastSendMs = millis();
  }
}