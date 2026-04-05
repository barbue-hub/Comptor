#include <Arduino.h>
#include <SoftwareSerial.h>
#include "ComptorApp.h"

#define RX_PIN 10
#define TX_PIN 11
#define ESP_BAUD 9600

SoftwareSerial espSerial(RX_PIN, TX_PIN);
static ComptorApp app;

static char espLine[96];
static uint8_t espIdx = 0;

static float clampf_local(float v, float vmin, float vmax) {
  if (v < vmin) return vmin;
  if (v > vmax) return vmax;
  return v;
}

void sendState() {
  const float temp = app.latestTemp();
  const long pos = app.positionSteps();
  const bool moving = app.isMoving();
  const float openSteps = app.openTurns() * static_cast<float>(Config::stepsPerRevolution());
  float percent = 0.0f;

  if (openSteps > 0.0f) {
    percent = (static_cast<float>(pos) * 100.0f) / openSteps;
  }
  percent = clampf_local(percent, 0.0f, 100.0f);

  espSerial.print("T:");
  if (isnan(temp)) {
    espSerial.print("NaN");
  } else {
    espSerial.print(temp, 2);
  }

  espSerial.print(";P:");
  espSerial.print(pos);
  espSerial.print(";M:");
  espSerial.print(moving ? 1 : 0);
  espSerial.print(";PC:");
  espSerial.println(percent, 1);
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
    sendState();
    Serial.println("[ESP] OPEN");
    return;
  }

  if (upper == "CLOSE") {
    app.requestClose();
    sendState();
    Serial.println("[ESP] CLOSE");
    return;
  }

  if (upper == "GETCFG") {
    sendConfig();
    Serial.println("[ESP] GETCFG");
    return;
  }

  if (upper == "GETSTATE") {
    sendState();
    return;
  }

  if (upper.startsWith("SETCFG:") || upper.startsWith("CFG:")) {
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
    char c = static_cast<char>(espSerial.read());

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
}
