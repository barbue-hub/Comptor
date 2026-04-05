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

static char espLine[64];
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

void sendConfig() {
  espSerial.print("CFG:OPEN=");
  espSerial.print(app.openTurns(), 3);
  espSerial.print(";SPEED=");
  espSerial.print(app.maxSpeedTurnsPerSecond(), 3);
  espSerial.print(";ACCEL=");
  espSerial.println(app.accelTurnsPerSecond2(), 3);
}

void handleEspCommand(String cmd) {
  cmd.trim();

  if (cmd.length() == 0) return;

  String upper = cmd;
  upper.toUpperCase();

  if (upper == "OPEN") {
    app.requestOpen();
    Serial.println("[ESP] OPEN");
    return;
  }

  if (upper == "CLOSE") {
    app.requestClose();
    Serial.println("[ESP] CLOSE");
    return;
  }

  if (upper == "GETCFG") {
    sendConfig();
    Serial.println("[ESP] GETCFG");
    return;
  }

  if (upper.startsWith("CFG:")) {
    float openTurns = app.openTurns();
    float speedTurns = app.maxSpeedTurnsPerSecond();
    float accelTurns = app.accelTurnsPerSecond2();

    int openIdx = upper.indexOf("OPEN=");
    int speedIdx = upper.indexOf("SPEED=");
    int accelIdx = upper.indexOf("ACCEL=");

    if (openIdx >= 0) {
      int end = upper.indexOf(';', openIdx);
      String v = cmd.substring(openIdx + 5, end >= 0 ? end : cmd.length());
      openTurns = v.toFloat();
    }

    if (speedIdx >= 0) {
      int end = upper.indexOf(';', speedIdx);
      String v = cmd.substring(speedIdx + 6, end >= 0 ? end : cmd.length());
      speedTurns = v.toFloat();
    }

    if (accelIdx >= 0) {
      int end = upper.indexOf(';', accelIdx);
      String v = cmd.substring(accelIdx + 6, end >= 0 ? end : cmd.length());
      accelTurns = v.toFloat();
    }

    app.updateMotionConfigTurns(openTurns, speedTurns, accelTurns);
    sendConfig();
    return;
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
