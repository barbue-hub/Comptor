#pragma once
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

typedef void (*VoidCb)();
typedef void (*SetFloatCb)(float);
typedef void (*GetStatusCb)(void*);

struct WebUI_Status {
  float tempC;
  unsigned long lastCalibMs;
  float posTurns;
  unsigned long cycles;
  IPAddress ip;
  unsigned long uptimeSec;
  int usedRamPercent;
  int cpuMHz;
  uint32 chipId;
};

namespace WebUI {
  void setCallbacks(VoidCb onOpen, VoidCb onClose, VoidCb onStop, VoidCb onMeasure,
                    SetFloatCb onSetTurns, SetFloatCb onSetSpeed,
                    SetFloatCb onSetAccel, GetStatusCb getStatus);
  void begin(const char* ssid, const char* wifiPwd);
  void loop();
  void addLog(const String& msg);
  void setOpenTurns(float turns);
  void setSpeedDisplay(float speed_steps_per_s);
  void setAccelDisplay(float accel_steps2_per_s);
}
