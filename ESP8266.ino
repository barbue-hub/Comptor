#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>

#define RX_PIN D6   // RX ESP8266 <- TX Pico
#define TX_PIN D7   // TX ESP8266 -> RX Pico
#define PICO_ BAUD   9600

SoftwareSerial picoSerial(RX_PIN, TX_PIN);
ESP8266WebServer server(80);

// TON WIFI
const char* ssid = "TELUS7704";
const char* password = "B6hxR6CHJ87n";

static float latestTemp = NAN;
static long latestPos = 0;
static bool latestMoving = false;
static unsigned long lastPicoMsgMs = 0;

bool picoOnline() {
  return (millis() - lastPicoMsgMs) <= 3000;
}

void parsePicoLine(const String& line) {
  int tIndex = line.indexOf("T:");
  int pIndex = line.indexOf(";P:");
  int mIndex = line.indexOf(";M:");

  if (tIndex < 0 || pIndex < 0 || mIndex < 0) {
    return;
  }

  String tStr = line.substring(tIndex + 2, pIndex);
  String pStr = line.substring(pIndex + 3, mIndex);
  String mStr = line.substring(mIndex + 3);

  tStr.trim();
  pStr.trim();
  mStr.trim();

  if (tStr == "NaN") {
    latestTemp = NAN;
  } else {
    latestTemp = tStr.toFloat();
  }

  latestPos = pStr.toInt();
  latestMoving = (mStr.toInt() != 0);
}

String makePage() {
  String html;
  html += "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>";
  html += "<meta http-equiv='refresh' content='2'>";
  html += "<title>Comptor</title>";
  html += "<style>";
  html += "html,body{margin:0;padding:0;width:100%;height:100%;overflow:hidden;font-family:Arial,sans-serif;background:#000;}";
  html += ".wrap{width:100vw;height:100vh;display:flex;flex-direction:column;}";
  html += ".status{width:100%;box-sizing:border-box;background:#f2f2f2;color:#111;padding:20px 24px;font-size:6vw;font-weight:bold;line-height:1.35;}";
  html += ".buttons{flex:1;display:flex;flex-direction:column;}";
  html += "a.btn{flex:1;display:flex;align-items:center;justify-content:center;font-size:16vw;font-weight:bold;text-decoration:none;color:#fff;}";
  html += ".open{background:#111;}";
  html += ".close{background:#2b2b2b;}";
  html += "</style></head><body>";

  html += "<div class='wrap'>";

  html += "<div class='status'>";
  html += "Temp: ";
  if (isnan(latestTemp)) {
    html += "N/A";
  } else {
    html += String(latestTemp, 1);
    html += "&deg;C";
  }
  html += "<br>";

  html += "Position: ";
  html += String(latestPos);
  html += " pas<br>";

  html += "Mouvement: ";
  html += latestMoving ? "Oui" : "Non";
  html += "<br>";

  html += "Comm Pico: ";
  html += picoOnline() ? "OK " : "PERDUE ";
  html += "<span style='display:inline-block;width:14px;height:14px;border-radius:50%;background:";
  html += picoOnline() ? "#16c60c" : "#d13438";
  html += ";'></span>";

  html += "</div>";

  html += "<div class='buttons'>";
  html += "<a class='btn open' href='/open'>OUVRIR</a>";
  html += "<a class='btn close' href='/close'>FERMER</a>";
  html += "</div>";

  html += "</div></body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", makePage());
}

void handleOpen() {
  picoSerial.println("OPEN");
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleClose() {
  picoSerial.println("CLOSE");
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  picoSerial.begin(PICO_BAUD);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.println();
  Serial.print("Connexion au WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connecté");
  Serial.print("IP locale: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/open", handleOpen);
  server.on("/close", handleClose);
  server.begin();

  Serial.println("Serveur web prêt");
}

void loop() {
  server.handleClient();

  while (picoSerial.available()) {
    String line = picoSerial.readStringUntil('\n');
    line.trim();

    if (line.length() > 0) {
      parsePicoLine(line);
      lastPicoMsgMs = millis();
      Serial.println(line);
    }
  }
}