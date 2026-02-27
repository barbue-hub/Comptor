#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("Pico OK");

#if defined(LED_BUILTIN)
  pinMode(LED_BUILTIN, OUTPUT);
#endif
  pinMode(2, OUTPUT);   // GP2 sur ton breakout (LED status devrait réagir)
}

void loop() {
#if defined(LED_BUILTIN)
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
#endif
  digitalWrite(2, !digitalRead(2));
  Serial.println("tick");
  delay(500);
}