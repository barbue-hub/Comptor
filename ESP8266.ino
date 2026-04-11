#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

#define RX_PIN D6
#define TX_PIN D7
#define PICO_BAUD 9600
#define EEPROM_SIZE 64

static constexpr float kGearCmPerTurn = 25.4466f;
static constexpr unsigned long kPicoTimeoutMs = 3000UL;
static constexpr uint32_t kConfigMagic = 0x43464733UL;
static constexpr unsigned long kStateRequestTimeoutMs = 120UL;
static constexpr unsigned long kConfigRequestTimeoutMs = 180UL;
static constexpr unsigned long kDebugRefreshMs = 500UL;
static constexpr uint16_t kLogCapacity = 120;
static constexpr bool kLogPollingTraffic = false;
static constexpr unsigned long kPostCommandPollDelayMs = 250UL;

struct StoredConfig {
  uint32_t magic;
  uint8_t debugFastRefresh;
  uint8_t reserved[11];
};

struct PicoMotionConfig {
  float openTurns;
  float speedTurnsPerSec;
  float accelTurnsPerSec2;
};

SoftwareSerial picoSerial(RX_PIN, TX_PIN);
ESP8266WebServer server(80);

const char* ssid = "C1C0D41ACEF6";
const char* password = "cogeco1895385";

static float latestTemp = NAN;
static long latestPos = 0;
static bool latestMoving = false;
static float latestPercent = 0.0f;
static unsigned long lastPicoMsgMs = 0;

static StoredConfig config = {kConfigMagic, 0, {0}};
static PicoMotionConfig picoCfg = {3.6f, 1.6f, 1.6f};

static char picoLine[128];
static uint8_t picoIdx = 0;

String logLines[kLogCapacity];
uint16_t logHead = 0;
uint16_t logCount = 0;

bool picoOnline() {
  return (millis() - lastPicoMsgMs) <= kPicoTimeoutMs;
}

void addLog(const String& msg) {
  String line = "[" + String(millis()) + " ms] " + msg;
  logLines[logHead] = line;
  logHead = (logHead + 1) % kLogCapacity;
  if (logCount < kLogCapacity) logCount++;
  Serial.println(line);
}

void sanitizeConfig() {
  config.debugFastRefresh = config.debugFastRefresh ? 1 : 0;
}

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, config);

  if (config.magic != kConfigMagic) {
    config.magic = kConfigMagic;
    config.debugFastRefresh = 0;
  }

  sanitizeConfig();
}

void saveConfig() {
  config.magic = kConfigMagic;
  sanitizeConfig();
  EEPROM.put(0, config);
  EEPROM.commit();
}

void parseCfgLine(const String& line) {
  int openIdx = line.indexOf("OPEN=");
  int speedIdx = line.indexOf("SPEED=");
  int accelIdx = line.indexOf("ACCEL=");

  if (openIdx >= 0) {
    int end = line.indexOf(';', openIdx);
    picoCfg.openTurns = line.substring(openIdx + 5, end >= 0 ? end : line.length()).toFloat();
  }
  if (speedIdx >= 0) {
    int end = line.indexOf(';', speedIdx);
    picoCfg.speedTurnsPerSec = line.substring(speedIdx + 6, end >= 0 ? end : line.length()).toFloat();
  }
  if (accelIdx >= 0) {
    int end = line.indexOf(';', accelIdx);
    picoCfg.accelTurnsPerSec2 = line.substring(accelIdx + 6, end >= 0 ? end : line.length()).toFloat();
  }
}

void parseStateLine(const String& line) {
  int tIndex = line.indexOf("T:");
  int pIndex = line.indexOf(";P:");
  int mIndex = line.indexOf(";M:");

  if (tIndex < 0 || pIndex < 0 || mIndex < 0) {
    return;
  }

  int pcIndex = line.indexOf(";PC:");

  String tStr = line.substring(tIndex + 2, pIndex);
  String pStr = line.substring(pIndex + 3, mIndex);
  String mStr = (pcIndex >= 0) ? line.substring(mIndex + 3, pcIndex) : line.substring(mIndex + 3);
  String pcStr = (pcIndex >= 0) ? line.substring(pcIndex + 4) : "0";

  tStr.trim();
  pStr.trim();
  mStr.trim();
  pcStr.trim();

  if (tStr == "NaN") {
    latestTemp = NAN;
  } else {
    latestTemp = tStr.toFloat();
  }

  latestPos = pStr.toInt();
  latestMoving = (mStr.toInt() != 0);
  latestPercent = pcStr.toFloat();

  if (latestPercent < 0.0f) latestPercent = 0.0f;
  if (latestPercent > 100.0f) latestPercent = 100.0f;
}

void parsePicoLine(const String& line) {
  const bool isPollingLine = line.startsWith("T:") || line.startsWith("CFG:");
  if (kLogPollingTraffic || !isPollingLine) {
    addLog("PICO <= " + line);
  }

  if (line.startsWith("CFG:")) {
    parseCfgLine(line);
    return;
  }

  if (line.startsWith("T:")) {
    parseStateLine(line);
  }
}

void readPicoNonBlocking() {
  while (picoSerial.available()) {
    char c = static_cast<char>(picoSerial.read());

    if (c == '\r') continue;

    if (c == '\n') {
      picoLine[picoIdx] = '\0';
      if (picoIdx > 0) {
        String line = String(picoLine);
        parsePicoLine(line);
        lastPicoMsgMs = millis();
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

void waitForPico(unsigned long timeoutMs) {
  unsigned long start = millis();
  while ((millis() - start) < timeoutMs) {
    readPicoNonBlocking();
    yield();
    delay(1);
  }
}

void requestStateFromPico() {
  if (kLogPollingTraffic) addLog("ESP => GETSTATE");
  picoSerial.println("GETSTATE");
  waitForPico(kStateRequestTimeoutMs);
}

void requestConfigFromPico() {
  if (kLogPollingTraffic) addLog("ESP => GETCFG");
  picoSerial.println("GETCFG");
  waitForPico(kConfigRequestTimeoutMs);
}

void sendConfigToPico(float openTurns, float speedTurnsPerSec, float accelTurnsPerSec2) {
  addLog("ESP => SETCFG OPEN=" + String(openTurns, 3) +
         " SPEED=" + String(speedTurnsPerSec, 3) +
         " ACCEL=" + String(accelTurnsPerSec2, 3));

  picoSerial.print("SETCFG:OPEN=");
  picoSerial.print(openTurns, 3);
  picoSerial.print(";SPEED=");
  picoSerial.print(speedTurnsPerSec, 3);
  picoSerial.print(";ACCEL=");
  picoSerial.println(accelTurnsPerSec2, 3);
  waitForPico(kConfigRequestTimeoutMs);
}

String baseStyle() {
  String css;
  css += "html,body{margin:0;padding:0;font-family:Arial,sans-serif;background:#000;color:#fff;}";
  css += "body{min-height:100vh;}";
  css += ".topbar{display:flex;justify-content:space-between;align-items:center;padding:10px 14px;background:#0d0d0d;font-size:16px;}";
  css += ".toplinks a{color:#8dc6ff;text-decoration:none;margin-left:14px;}";
  css += ".statusmini{display:flex;gap:14px;align-items:center;font-weight:bold;flex-wrap:wrap;}";
  css += ".ok{color:#16c60c;font-weight:bold;}";
  css += ".bad{color:#d13438;font-weight:bold;}";
  css += ".card{margin:14px;padding:16px;border-radius:14px;background:#111;}";
  css += ".muted{color:#bbb;}";
  css += "input{width:100%;box-sizing:border-box;padding:14px;font-size:18px;border-radius:10px;border:1px solid #444;background:#1b1b1b;color:#fff;}";
  css += "label{display:block;margin:14px 0 8px;font-weight:bold;}";
  css += "button,.btn{display:inline-block;padding:14px 18px;border:none;border-radius:10px;background:#2d6cdf;color:#fff;text-decoration:none;font-size:18px;cursor:pointer;}";
  css += ".secondary{background:#333;}";
  css += ".stack{display:flex;flex-direction:column;gap:12px;}";
  css += ".remote{height:calc(100vh - 52px);display:flex;flex-direction:column;}";
  css += "a.bigbtn{flex:1;display:flex;align-items:center;justify-content:center;font-size:18vw;font-weight:bold;text-decoration:none;color:#fff;user-select:none;-webkit-tap-highlight-color:transparent;}";
  css += ".open{background:#050505;}";
  css += ".close{background:#2a2a2a;}";
  css += ".switch{display:flex;align-items:center;gap:10px;}";
  css += ".switch input{width:auto;transform:scale(1.4);}";
  css += ".logwrap{white-space:pre-wrap;font-family:monospace;font-size:14px;line-height:1.35;background:#050505;padding:12px;border-radius:10px;max-height:70vh;overflow:auto;}";
  return css;
}

String makePage() {
  String html;
  html += "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>";
  html += "<title>Comptor Remote</title>";
  html += "<style>" + baseStyle() + "</style></head><body>";

  html += "<div class='topbar'>";
  html += "<div class='statusmini'>";
  html += "<span id='comm' class='";
  html += picoOnline() ? "ok'>OK" : "bad'>PERDUE";
  html += "</span>";
  html += "<span id='posPct'>" + String(latestPercent, 1) + "%</span>";
  html += "<span id='temp'>";
  html += isnan(latestTemp) ? "N/A" : String(latestTemp, 1) + "&deg;C";
  html += "</span>";
  html += "</div>";
  html += "<div class='toplinks'>";
  html += "<a href='/config'>Config</a>";
  html += "<a href='/log'>Log</a>";
  html += "</div>";
  html += "</div>";

html += "<div class='remote'>";
html += "<a class='bigbtn open' href='/open' onclick=\"return confirm('Assurez-vous que le comptoir est dégagé. Continuer ?');\">OUVRIR</a>";
html += "<a class='bigbtn close' href='/close' onclick=\"return confirm('Assurez-vous que le comptoir est dégagé. Continuer ?');\">FERMER</a>";
html += "</div>";

  html += "<script>";
  html += "const debugFastRefresh=" + String(config.debugFastRefresh ? "true" : "false") + ";";
  html += "const debugRefreshMs=" + String((unsigned long)kDebugRefreshMs) + ";";
  html += "async function refreshState(){";
  html += "try{";
  html += "const r=await fetch('/api/state',{cache:'no-store'});";
  html += "const s=await r.json();";
  html += "document.getElementById('temp').innerHTML=s.tempText;";
  html += "document.getElementById('posPct').textContent=s.posPct.toFixed(1)+'%';";
  html += "const c=document.getElementById('comm');";
  html += "c.textContent=s.online?'OK':'PERDUE';";
  html += "c.className=s.online?'ok':'bad';";
  html += "}catch(e){}";
  html += "}";
  html += "if(debugFastRefresh){";
  html += "refreshState();";
  html += "setInterval(refreshState, debugRefreshMs);";
  html += "}";
  html += "</script>";

  html += "</body></html>";
  return html;
}

String makeConfigPage(bool saved) {
  String html;
  html += "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Config Comptor</title>";
  html += "<style>" + baseStyle() + "</style></head><body>";

  html += "<div class='topbar'>";
  html += "<div><strong>Config</strong></div>";
  html += "<div class='toplinks'>";
  html += "<a href='/'>Accueil</a>";
  html += "<a href='/log'>Log</a>";
  html += "</div>";
  html += "</div>";

  html += "<div class='card'><h2 style='margin-top:0'>Paramètres mouvement</h2>";
  if (saved) {
    html += "<p class='ok'>Configuration sauvegardée.</p>";
  }
  html += "<form method='POST' action='/config/save' class='stack'>";
  html += "<div><label for='openTurns'>Distance ouverture (tours)</label>";
  html += "<input id='openTurns' name='openTurns' type='number' min='0.1' max='4.2' step='0.01' value='" + String(picoCfg.openTurns, 3) + "'></div>";
  html += "<div class='muted'>Approx: " + String(picoCfg.openTurns * kGearCmPerTurn, 2) + " cm</div>";
  html += "<div><label for='speed'>Vitesse (tours/s)</label>";
  html += "<input id='speed' name='speed' type='number' min='0.05' max='2.0' step='0.01' value='" + String(picoCfg.speedTurnsPerSec, 3) + "'></div>";
  html += "<div><label for='accel'>Accélération (tours/s²)</label>";
  html += "<input id='accel' name='accel' type='number' min='0.05' max='5.0' step='0.01' value='" + String(picoCfg.accelTurnsPerSec2, 3) + "'></div>";
  html += "<div><label class='switch' for='debugFastRefresh'><input id='debugFastRefresh' name='debugFastRefresh' type='checkbox' value='1'";
  html += config.debugFastRefresh ? " checked" : "";
  html += ">Mode debug refresh rapide</label></div>";
  html += "<div class='muted'>Accueil = cache utile. Config = lecture réelle.</div>";
  html += "<button type='submit'>Sauvegarder</button>";
  html += "</form>";
  html += "<p><a class='btn secondary' href='/config/reset'>Remettre défaut</a></p>";
  html += "</div></body></html>";
  return html;
}

String makeLogPage() {
  String html;
  html += "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Log Comptor</title>";
  html += "<style>" + baseStyle() + "</style></head><body>";

  html += "<div class='topbar'>";
  html += "<div><strong>Log</strong></div>";
  html += "<div class='toplinks'>";
  html += "<a href='/'>Accueil</a>";
  html += "<a href='/config'>Config</a>";
  html += "<a href='/log/clear'>Vider</a>";
  html += "</div>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<div class='muted'>Cache local ESP8266 + messages Pico reçus</div>";
  html += "<div class='logwrap'>";

  if (logCount == 0) {
    html += "Aucun log.";
  } else {
    for (uint16_t i = 0; i < logCount; i++) {
      uint16_t idx = (logHead + kLogCapacity - logCount + i) % kLogCapacity;
      html += logLines[idx];
      html += "\n";
    }
  }

  html += "</div></div></body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", makePage());
}

void handleConfigPage() {
  requestConfigFromPico();
  bool saved = server.hasArg("saved") && server.arg("saved") == "1";
  server.send(200, "text/html", makeConfigPage(saved));
}

void handleLogPage() {
  server.send(200, "text/html", makeLogPage());
}

void handleLogClear() {
  logHead = 0;
  logCount = 0;
  addLog("LOG CLEAR");
  server.sendHeader("Location", "/log");
  server.send(303);
}

void handleApiState() {
  if (config.debugFastRefresh) {
    requestStateFromPico();
  }

  String json = "{";
  json += "\"temp\":";
  if (isnan(latestTemp)) json += "null"; else json += String(latestTemp, 2);
  json += ",\"tempText\":\"";
  json += isnan(latestTemp) ? "N/A" : String(latestTemp, 1) + "&deg;C";
  json += "\",\"pos\":" + String(latestPos);
  json += ",\"posPct\":" + String(latestPercent, 1);
  json += ",\"moving\":" + String(latestMoving ? "true" : "false");
  json += ",\"online\":" + String(picoOnline() ? "true" : "false");
  json += ",\"openCm\":" + String(picoCfg.openTurns * kGearCmPerTurn, 2);
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleOpen() {
  addLog("WEB => OPEN");
  picoSerial.println("OPEN");
  delay(kPostCommandPollDelayMs);
  requestStateFromPico();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleClose() {
  addLog("WEB => CLOSE");
  picoSerial.println("CLOSE");
  delay(kPostCommandPollDelayMs);
  requestStateFromPico();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleConfigSave() {
  float openTurns = picoCfg.openTurns;
  float speedTurns = picoCfg.speedTurnsPerSec;
  float accelTurns = picoCfg.accelTurnsPerSec2;

  if (server.hasArg("openTurns")) openTurns = server.arg("openTurns").toFloat();
  if (server.hasArg("speed")) speedTurns = server.arg("speed").toFloat();
  if (server.hasArg("accel")) accelTurns = server.arg("accel").toFloat();

  config.debugFastRefresh = server.hasArg("debugFastRefresh") ? 1 : 0;
  sanitizeConfig();
  saveConfig();

  sendConfigToPico(openTurns, speedTurns, accelTurns);
  requestConfigFromPico();

  server.sendHeader("Location", "/config?saved=1");
  server.send(303);
}

void handleConfigReset() {
  config.debugFastRefresh = 0;
  sanitizeConfig();
  saveConfig();

  sendConfigToPico(3.6f, 1.6f, 1.6f);
  requestConfigFromPico();

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

  addLog("WiFi OK IP=" + WiFi.localIP().toString());

  server.on("/", handleRoot);
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/config/save", HTTP_POST, handleConfigSave);
  server.on("/config/reset", HTTP_GET, handleConfigReset);
  server.on("/api/state", HTTP_GET, handleApiState);
  server.on("/log", HTTP_GET, handleLogPage);
  server.on("/log/clear", HTTP_GET, handleLogClear);
  server.on("/open", handleOpen);
  server.on("/close", handleClose);
  server.begin();

  requestConfigFromPico();
  requestStateFromPico();

  addLog("Serveur web prêt");
}

void loop() {
  server.handleClient();
  readPicoNonBlocking();
}