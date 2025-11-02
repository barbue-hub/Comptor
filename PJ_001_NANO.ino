// PJ_001_NANO.ino
/*
 * Rôle = Slave
 * Description : envoi des valeurs des sensors de température moteur et ultrason. À DÉTERMINER SI SENSOR DE COURANT VAUT LA PEINE..
 */

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ACS712.h>

ACS712 cs(A1, 5.0, 1023, 100);  // ACS712 20A → 100 mV/A
// ----------------- Capteur température DS18B20 -----------------
#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
float tempSeuil = 50.0f;
float lastTemp = 0.0f;

// ------------- Envoie des valeurs sensors -------------
void sendValue(uint8_t ID, uint8_t val) {
  Serial.write(0xFF);  // start-byte unique (≠ 0xA5,0x5A)
  Serial.write(ID);
  Serial.write(val);
}

// ------------- Capteur distance (HC-SR04) -------------
int trigPin = 3;
int echoPin = 5;
long duration;
int distance;

static const char CMD_READ_DISTANCE = 'D';

static unsigned long lastDbg = 0;

const uint8_t nbrsVal = 30;  // taille moyenne mobile
static float fBuffer[nbrsVal] = { 0.0f };

float measureDistanceCM() {
  float sum = 0.0f;
  uint8_t n = 0;

  for (uint8_t i = 0; i < nbrsVal; i++) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    unsigned long dur = pulseIn(echoPin, HIGH, 25000UL); // ~4 m max
    if (dur == 0) continue;               // timeout → on ignore
    float d = dur * 0.01715f;             // (0.0343/2) cm/µs
    fBuffer[i] = d;
    sum += d;
    n++;
    delay(60);                            // laisser mourir l’écho
  }
  if (n == 0) return NAN;
  return sum / n;
}


// ----------------- Température & surchauffe -----------------
float measureTempC() {
  sensors.setWaitForConversion(true);
  sensors.requestTemperatures();
  lastTemp = sensors.getTempCByIndex(0);
  Serial.print(F("Temp = "));
  Serial.println(lastTemp);
  return lastTemp;
}

void triggerOverheat() {
  // TODO: gestion surchauffe
}

void testSensor(){
  Serial.print("Température:");
  Serial.println(measureTempC());
  Serial.print("Distance :");
  Serial.println(measureDistanceCM());
  delay(250);
}
// ----------------- Moyenne mobile courant -----------------
const uint8_t MA_WINDOW = 10;  // taille fenêtre
static float iDcBuffer[MA_WINDOW] = { 0.0f };
static uint8_t iDcIndex = 0;  // index circulaire
static uint8_t iDcCount = 0;  // nb valeurs accumulées (≤ MA_WINDOW)
static float iDcSum = 0.0f;   // somme courante

void setup() {
  Serial.begin(115200);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  sensors.begin();
  sensors.setResolution(9);
}

void loop() {
  // Ajout de la logique pour renvoyer les mesures sur commande série.
  if (Serial.available() > 0) {
    char cmd = (char)Serial.read();
    switch (cmd) {
      case 'D': {
        float d = measureDistanceCM();
        if (!isnan(d)) {
          Serial.print("$DST:");
          Serial.println(d);
        } else {
          Serial.println("$DST:NaN");
        }
        break;
      }
      case 'T': {
        sensors.setWaitForConversion(true);
        sensors.requestTemperatures();
        float t = sensors.getTempCByIndex(0);
        if (!isnan(t)) {
          Serial.print("$TMP:");
          Serial.println(t);
        } else {
          Serial.println("$TMP:NaN");
        }
        break;
      }
      default:
        // commandes inconnues : ne rien faire
        break;
    }
  }
}
