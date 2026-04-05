#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

#define RX_PIN D6
#define TX_PIN D7
#define PICO_BAUD 9600
#define EEPROM_SIZE 128

static constexpr float kGearCmPerTurn = 25.4466f;
static constexpr unsigned long kPicoTimeoutMs = 3000UL;
static constexpr unsigned long kConfigPushPeriodMs = 5000UL;
static constexpr uint32_t kConfigMagic = 0x43464731UL;

struct StoredConfig {
  uint32_t magic;
  float openTurns;
  float speedTurnsPerSec;
  float accelTurnsPerSec2;
};

SoftwareSerial picoSerial(RX_PIN, TX_PIN);
ESP8266WebServer server(80);

const char* ssid = "TELUS7704";
const char* password = "B6hxR6CHJ87n";

static float latestTemp = NAN;
static long latestPos = 0;
static bool latestMoving = false;
static unsigned long lastPicoMsgMs = 0;
static unsigned long lastConfigPushMs = 0;

static StoredConfig config = {kConfigMagic, 3.6f, 1.6f, 1.6f};

static char picoLine[96];
static uint8_t picoIdx = 0;

bool picoOnline() {
  return (millis() - lastPicoMsgMs) <= kPicoTimeoutMs;
}

float clampf(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

void sanitizeConfig() {
  config.openTurns = clampf(config.openTurns, 0.1f, 20.0f);
  config.speedTurnsPerSec = clampf(config.speedTurnsPerSec, 0.05f, 10.0f);
  config.accelTurnsPerSec2 = clampf(config.accelTurnsPerSec2, 0.05f, 20.0f);
}

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, config);

  if (config.magic != kConfigMagic || isnan(config.openTurns) || isnan(config.speedTurnsPerSec) || isnan(config.accelTurnsPerSec2)) {
    config.magic = kConfigMagic;
    config.openTurns = 3.6f;
    config.speedTurnsPerSec = 1.6f;
    config.accelTurnsPerSec2 = 1.6f;
  }

  sanitizeConfig();
}

void saveConfig() {
  config.magic = kConfigMagic;
  sanitizeConfig();
  EEPROM.put(0, config);
  EEPROM.commit();
}

void pushConfigToPico() {
  sanitizeConfig();
  picoSerial.print("CFG:OPEN=");
  picoSerial.print(config.openTurns, 3);
  picoSerial.print(";SPEED=");
  picoSerial.print(config.speedTurnsPerSec, 3);
  picoSerial.print(";ACCEL=");
  picoSerial.println(config.accelTurnsPerSec2, 3);
  lastConfigPushMs = millis();
}

void parsePicoLine(const String& line) {
  if (line.startsWith("CFG:")) {
    int openIdx = line.indexOf("OPEN=");
    int speedIdx = line.indexOf("SPEED=");
    int accelIdx = line.indexOf("ACCEL=");

    if (openIdx >= 0) {
      int end = line.indexOf(';', openIdx);
      config.openTurns = line.substring(openIdx + 5, end >= 0 ? end : line.length()).toFloat();
    }
    if (speedIdx >= 0) {
      int end = line.indexOf(';', speedIdx);
      config.speedTurnsPerSec = line.substring(speedIdx + 6, end >= 0 ? end : line.length()).toFloat();
    }
    if (accelIdx >= 0) {
      int end = line.indexOf(';', accelIdx);
      config.accelTurnsPerSec2 = line.substring(accelIdx + 6, end >= 0 ? end : line.length()).toFloat();
    }

    sanitizeConfig();
    saveConfig();
    return;
  }

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

void readPicoNonBlocking() {
  while (picoSerial.available()) {
    char c = (char)picoSerial.read();

    if (c == '\r') continue;

    if (c == '\n') {
      picoLine[picoIdx] = '\0';
      if (picoIdx > 0) {
        String line = String(picoLine);
        parsePicoLine(line);
        lastPicoMsgMs = millis();
        Serial.println(line);
      }
      picoIdx = 0;
    } else {
      if (picoIdx < sizeof(picoLine) - 1) {
        picoLine[picoIdx++] = c;
      } else {
        picoIdx = 0;
      }
    }
  }
}

String navBar(const String& active) {
  String html;
  html += "<div class='nav'>";
  html += active == "/" ? "<strong>Accueil</strong>" : "<a href='/'>Accueil</a>";
  html += " | ";
  html += active == "/config" ? "<strong>Config</strong>" : "<a href='/config'>Config</a>";
  html += "</div>";
  return html;
}

String baseStyle() {
  String css;
  css += "html,body{margin:0;padding:0;font-family:Arial,sans-serif;background:#000;color:#fff;}";
  css += ".nav{padding:14px 18px;background:#101010;font-size:18px;}";
  css += ".nav a{color:#8dc6ff;text-decoration:none;}";
  css += ".card{margin:16px;padding:18px;border-radius:14px;background:#111;}";
  css += ".muted{color:#bbb;}";
  css += "input{width:100%;box-sizing:border-box;padding:14px;font-size:18px;border-radius:10px;border:1px solid #444;background:#1b1b1b;color:#fff;}";
  css += "label{display:block;margin:14px 0 8px;font-weight:bold;}";
  css += "button,.btn{display:inline-block;padding:14px 18px;border:none;border-radius:10px;background:#2d6cdf;color:#fff;text-decoration:none;font-size:18px;cursor:pointer;}";
  css += ".secondary{background:#333;}";
  css += ".stack{display:flex;flex-direction:column;gap:12px;}";
  css += ".buttons{height:calc(100vh - 230px);display:flex;flex-direction:column;}";
  css += "a.bigbtn{flex:1;display:flex;align-items:center;justify-content:center;font-size:16vw;font-weight:bold;text-decoration:none;color:#fff;}";
  css += ".open{background:#111;}";
  css += ".close{background:#2b2b2b;}";
  css += ".ok{color:#16c60c;font-weight:bold;}";
  css += ".bad{color:#d13438;font-weight:bold;}";
  return css;
}

String makePage() {
  String html;
  html += "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>";
  html += "<meta http-equiv='refresh' content='2'>";
  html += "<title>Comptor</title>";
  html += "<style>" + baseStyle() + "</style></head><body>";
  html += navBar("/");
  html += "<div class='card'>";
  html += "Temp: ";
  if (isnan(latestTemp)) {
    html += "N/A";
  } else {
    html += String(latestTemp, 1) + "&deg;C";
  }
  html += "<br>Position: " + String(latestPos) + " pas<br>";
  html += "Mouvement: ";
  html += latestMoving ? "Oui" : "Non";
  html += "<br>Comm Pico: ";
  html += picoOnline() ? "<span class='ok'>OK</span>" : "<span class='bad'>PERDUE</span>";
  html += "<br><span class='muted'>Distance ouverture: ";
  html += String(config.openTurns * kGearCmPerTurn, 1);
  html += " cm approx</span>";
  html += "</div>";
  html += "<div class='buttons'>";
  html += "<a class='bigbtn open' href='/open'>OUVRIR</a>";
  html += "<a class='bigbtn close' href='/close'>FERMER</a>";
  html += "</div></body></html>";
  return html;
}

String makeConfigPage(bool saved) {
  String html;
  html += "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Config Comptor</title>";
  html += "<style>" + baseStyle() + "</style></head><body>";
  html += navBar("/config");
  html += "<div class='card'><h2 style='margin-top:0'>Paramètres mouvement</h2>";
  if (saved) {
    html += "<p class='ok'>Configuration sauvegardée.</p>";
  }
  html += "<form method='POST' action='/config/save' class='stack'>";
  html += "<div><label for='openTurns'>Distance ouverture (tours)</label>";
  html += "<input id='openTurns' name='openTurns' type='number' min='0.1' max='20' step='0.01' value='" + String(config.openTurns, 3) + "'></div>";
  html += "<div class='muted'>Approx: " + String(config.openTurns * kGearCmPerTurn, 2) + " cm</div>";
  html += "<div><label for='speed'>Vitesse (tours/s)</label>";
  html += "<input id='speed' name='speed' type='number' min='0.05' max='10' step='0.01' value='" + String(config.speedTurnsPerSec, 3) + "'></div>";
  html += "<div><label for='accel'>Accélération (tours/s²)</label>";
  html += "<input id='accel' name='accel' type='number' min='0.05' max='20' step='0.01' value='" + String(config.accelTurnsPerSec2, 3) + "'></div>";
  html += "<button type='submit'>Sauvegarder</button>";
  html += "</form>";
  html += "<p><a class='btn secondary' href='/config/reset'>Remettre défaut</a></p>";
  html += "</div></body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", makePage());
}

void handleConfigPage() {
  bool saved = server.hasArg("saved") && server.arg("saved") == "1";
  server.send(200, "text/html", makeConfigPage(saved));
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

void handleConfigSave() {
  if (server.hasArg("openTurns")) config.openTurns = server.arg("openTurns").toFloat();
  if (server.hasArg("speed")) config.speedTurnsPerSec = server.arg("speed").toFloat();
  if (server.hasArg("accel")) config.accelTurnsPerSec2 = server.arg("accel").toFloat();

  sanitizeConfig();
  saveConfig();
  pushConfigToPico();

  server.sendHeader("Location", "/config?saved=1");
  server.send(303);
}

void handleConfigReset() {
  config.openTurns = 3.6f;
  config.speedTurnsPerSec = 1.6f;
  config.accelTurnsPerSec2 = 1.6f;
  saveConfig();
  pushConfigToPico();

  server.sendHeader("Location", "/config?saved=1");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  picoSerial.begin(PICO_BAUD);
  loadConfig();

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
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/config/save", HTTP_POST, handleConfigSave);
  server.on("/config/reset", HTTP_GET, handleConfigReset);
  server.on("/open", handleOpen);
  server.on("/close", handleClose);
  server.begin();

  picoSerial.println("GETCFG");
  pushConfigToPico();

  Serial.println("Serveur web prêt");
}

void loop() {
  server.handleClient();
  readPicoNonBlocking();

  if ((millis() - lastConfigPushMs) >= kConfigPushPeriodMs) {
    pushConfigToPico();
  }
}
