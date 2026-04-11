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

static float parseFloatField(const String& source, const String& upperSource, const char* key, uint8_t keyLen, float fallback) {
  const int fieldIdx = upperSource.indexOf(key);
  if (fieldIdx < 0) return fallback;

  const int endIdx = upperSource.indexOf(';', fieldIdx);
  const String value = source.substring(fieldIdx + keyLen, endIdx >= 0 ? endIdx : source.length());
  return value.toFloat();
}

static void logEspCommand(const __FlashStringHelper* label) {
  Serial.print(F("[ESP] "));
  Serial.println(label);
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
    logEspCommand(F("OPEN"));
    return;
  }

  if (upper == "CLOSE") {
    app.requestClose();
    sendState();
    logEspCommand(F("CLOSE"));
    return;
  }

  if (upper == "GETCFG") {
    sendConfig();
    logEspCommand(F("GETCFG"));
    return;
  }

  if (upper == "GETSTATE") {
    sendState();
    return;
  }

  if (upper.startsWith("SETCFG:") || upper.startsWith("CFG:")) {
    const float openTurns = parseFloatField(cmd, upper, "OPEN=", 5, app.openTurns());
    const float speedTurns = parseFloatField(cmd, upper, "SPEED=", 6, app.maxSpeedTurnsPerSecond());
    const float accelTurns = parseFloatField(cmd, upper, "ACCEL=", 6, app.accelTurnsPerSecond2());

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
        // Ligne trop longue: on réinitialise le buffer pour repartir proprement.
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
