#pragma once
#include <Arduino.h>
#include "Config.h"
enum Role : uint8_t { Initiator = 0, Responder = 1 };

class WDLink {
public:
  //unsigned long interval    = 3000;
  //unsigned long timeout     = 2000;
  //bool          asciiCompat = true;
  void (*onUp)()   = nullptr;
  void (*onDown)() = nullptr;

  void begin(Stream& port, Role r) {
    io = &port; role = r; sh0 = sh1 = 0; lineLen = 0;
    lastPing = lastSeen = millis(); up = false; fault = false;
  }

  void poll() {
    if (!io) return;
    if (role == Initiator) {
      unsigned long now = millis();
      if (now - lastPing >= interval) {
        lastPing = now; const uint8_t pkt[2] = { 0xA5, 0x5A }; io->write(pkt, 2);
      }
    }
    while (io->available() > 0) {
      int r = io->read(); if (r < 0) break; uint8_t b = (uint8_t)r;
      sh0 = sh1; sh1 = b;
      if (role == Initiator) { if (sh0 == 0x5A && sh1 == 0xA5) markSeen(); }
      else { if (sh0 == 0xA5 && sh1 == 0x5A) { const uint8_t pong[2] = { 0x5A, 0xA5 }; io->write(pong, 2); markSeen(); } }
      if (asciiCompat) {
        if (b == '\n' || b == '\r') {
          if (lineLen) {
            lineBuf[lineLen] = 0;
            if (role == Initiator) { if (strncmp(lineBuf, "$WD:pong", 8) == 0) markSeen(); }
            else { if (strncmp(lineBuf, "$WD:ping", 8) == 0) { const uint8_t pong2[2] = { 0x5A, 0xA5 }; io->write(pong2, 2); markSeen(); } }
            lineLen = 0;
          }
        } else {
          if (lineLen < sizeof(lineBuf) - 1) lineBuf[lineLen++] = (char)b; else lineLen = 0;
        }
      }
    }
    if (timeout > 0 && (millis() - lastSeen >= timeout)) setDown();
  }

  bool isUp()   const { return up && !fault; }
  bool isDown() const { return !isUp(); }

  struct DebugInfo { unsigned long lastPing, lastSeen, interval, timeout; bool up, fault, isInitiator; };
  void getDebugInfo(DebugInfo& out) const {
    out.lastPing=lastPing; out.lastSeen=lastSeen; out.interval=interval; out.timeout=timeout;
    out.up=up; out.fault=fault; out.isInitiator=(role==Initiator);
  }

private:
  Stream* io=nullptr; Role role=Initiator;
  uint8_t sh0=0, sh1=0; char lineBuf[32]; uint8_t lineLen=0;
  unsigned long lastPing=0, lastSeen=0; bool up=false, fault=false;

  void markSeen(){ lastSeen=millis(); if (fault || !up){ fault=false; up=true; if(onUp) onUp(); } }
  void setDown(){ if (!fault){ fault=true; up=false; if(onDown) onDown(); } }
};

extern WDLink wd;
WDLink wd;
