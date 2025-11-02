#include "WebUI.h"
#include <ESP.h>

namespace {
  ESP8266WebServer server(80);
  static bool isAuthenticated = true;
  static const char* kAuthPwd = "69420";
  static VoidCb cbOpen=nullptr, cbClose=nullptr, cbStop=nullptr;
  static SetFloatCb cbSetTurns=nullptr, cbSetSpeed=nullptr, cbSetAccel=nullptr;
  static GetStatusCb cbGetStatus=nullptr;

  static float openTurnsDisplay=10.0f, speedDisplay=50000.0f, accelDisplay=1500.0f;

  String logs; static uint32_t logsVer=0;
  static void pushLog(const String& s){ logs += s + "\n"; if (logs.length()>2000) logs.remove(0, logs.length()-2000); logsVer++; }

  void handleLogin() {
    if (!server.hasArg("pwd")) { server.send(400,"text/plain","Parameter 'pwd' missing"); return; }
    if (server.arg("pwd") == kAuthPwd) { isAuthenticated = true; pushLog("[START] Auth OK"); server.sendHeader("Location","/"); server.send(303); }
    else { pushLog("[START] Auth FAIL"); server.send(401,"text/html","<html><body>Mot de passe incorrect. <a href='/'>Réessayer</a></body></html>"); }
  }

  void handleRoot() {
    if (!isAuthenticated) {
      server.send(200,"text/html",
        "<html><head><meta charset='utf-8'></head><body>"
        "<h1>Authentification requise</h1>"
        "<form action='/login' method='GET'>Mot de passe: <input type='password' name='pwd'>"
        "<input type='submit' value='Se connecter'></form></body></html>");
      return;
    }
    WebUI_Status st{}; if (cbGetStatus) cbGetStatus(&st);
    String page = "<html><head><meta charset='utf-8'></head><body>";
    page += "<h1>Contrôle moteur</h1>";
    page += "<p>Paramètres actuels :</p><form id='frmParams'>";
    page += "Tours: <input type='number' step='0.1' name='turns' value='"+String(openTurnsDisplay,2)+"'><br>";
    page += "Vitesse (steps/s): <input type='number' step='1' name='speed' value='"+String(speedDisplay,0)+"'><br>";
    page += "Accélération (steps^2/s): <input type='number' step='1' name='accel' value='"+String(accelDisplay,0)+"'><br>";
    page += "<button type='submit' id='saveParams'>Mettre à jour</button></form>";
    page += "<button onclick=\"fetch('/open')\">Ouvrir</button> ";
    page += "<button onclick=\"fetch('/close')\">Fermer</button> ";
    page += "<button onclick=\"fetch('/stop')\">Stop</button> ";
    page += "<button type='button' id='btnRefresh'>Refresh</button>";
    page += "<p>Cycles complétés : <span id='cycles'>" + String(st.cycles) + "</span></p>";
    page += "<h2>Informations système</h2>";
    page += "<style>#sys{border-collapse:collapse}#sys th,#sys td{border:1px solid #ccc;padding:4px 8px;text-align:left}</style>";
    page += "<table id='sys'><tbody>";
    page += "<tr><th>Température</th><td><span id='temp'>" + String(st.tempC,2) + " &deg;C</span></td></tr>";
    page += "<tr><th>Dernière calibration</th><td><span id='calib'>" + String((millis()-st.lastCalibMs)/1000) + " s</span></td></tr>";
    page += "<tr><th>Position</th><td><span id='pos'>" + String(st.posTurns,2) + " tours</span></td></tr>";
    page += "<tr><th>Vitesse</th><td><span id='speed'>" + String(speedDisplay,0) + " steps/s</span></td></tr>";
    page += "<tr><th>Accélération</th><td><span id='accel'>" + String(accelDisplay,0) + " steps^2/s</span></td></tr>";
    page += "</tbody></table>";
    page += "<h2>Logs</h2><pre id='log'></pre>";
    page += "<h2>Statistiques système</h2><ul>";
    page += "<li>Uptime: " + String(st.uptimeSec) + " s</li>";
    page += "<li>RAM utilisée: " + String(st.usedRamPercent) + " %</li>";
    page += "<li>Fréquence CPU: " + String(st.cpuMHz) + " MHz</li>";
    page += "<li>Identifiant du chip: " + String(st.chipId) + "</li>";
    page += "<li>IP locale: " + st.ip.toString() + "</li>";
    page += "</ul>";
    page += "<script>";
    page += "let statusTag=null, logsTag=null;";
    page += "function pollStatus(force){ const url='/status'+(force?('?t='+Date.now()):''); const opt= force? {} : (statusTag? {headers:{'If-None-Match':statusTag}}:{});";
    page += "fetch(url,opt).then(r=>{ if(r.status===304) return null; statusTag=r.headers.get('ETag')||statusTag; return r.json(); })";
    page += ".then(st=>{ if(!st) return; document.getElementById('temp').innerText=st.temp.toFixed(2)+' \\u00B0C';";
    page += "document.getElementById('calib').innerText=st.lastCalib+' s'; document.getElementById('pos').innerText=st.pos.toFixed(2)+' tours';";
    page += "const c=document.getElementById('cycles'); if(c) c.innerText=st.cycles; const sp=document.getElementById('speed'); if(sp) sp.innerText=Math.round(st.speed)+' steps/s';";
    page += "const ac=document.getElementById('accel'); if(ac) ac.innerText=Math.round(st.accel)+' steps^2/s';";
    page += "}).catch(()=>{}).finally(()=>setTimeout(()=>pollStatus(false),5000)); }";
    page += "function pollLogs(force){ const url='/logs'+(force?('?t='+Date.now()):''); const opt= force? {} : (logsTag? {headers:{'If-None-Match':logsTag}}:{});";
    page += "fetch(url,opt).then(r=>{ if(r.status===304) return null; logsTag=r.headers.get('ETag')||logsTag; return r.text(); })";
    page += ".then(t=>{ if(t!=null) document.getElementById('log').innerText=t; }).catch(()=>{}).finally(()=>setTimeout(()=>pollLogs(false),3000)); }";
    page += "function saveParams(){ const f=document.getElementById('frmParams'); const q=new URLSearchParams(new FormData(f)).toString();";
    page += "fetch('/set?'+q).then(r=>{ if(!r.ok) throw 0; return r.json(); }).then(()=>{ pollStatus(true); pollLogs(true); }).catch(()=>{}); }";
    page += "function forceRefresh(){ statusTag=null; logsTag=null; pollStatus(true); pollLogs(true); }";
    page += "document.addEventListener('DOMContentLoaded',()=>{ document.getElementById('frmParams').addEventListener('submit',(e)=>{e.preventDefault();saveParams();});";
    page += "const b=document.getElementById('btnRefresh'); if(b) b.addEventListener('click', forceRefresh); pollStatus(true); pollLogs(true); });";
    page += "</script>";
    page += "</body></html>";
    server.send(200, "text/html", page);
  }

  void handleOpen()  { if (!isAuthenticated) { server.send(403,"text/plain","Non autorisé"); return; } if (cbOpen)  cbOpen();  pushLog("[CMD] Ouverture"); server.send(200,"text/plain","OPEN"); }
  void handleClose() { if (!isAuthenticated) { server.send(403,"text/plain","Non autorisé"); return; } if (cbClose) cbClose(); pushLog("[CMD] Fermeture");  server.send(200,"text/plain","CLOSE"); }
  void handleStop()  { if (!isAuthenticated) { server.send(403,"text/plain","Non autorisé"); return; } if (cbStop)  cbStop();  pushLog("[ESTOP] Commande d'arrêt reçue"); server.send(200,"text/plain","STOP"); }

  void handleSet() {
    if (!isAuthenticated) { server.send(403,"text/plain","Non autorisé"); return; }
    bool changed = false;
    if (server.hasArg("turns")) { float v = server.arg("turns").toFloat(); if (v > 0.0f) { openTurnsDisplay = v; if (cbSetTurns) cbSetTurns(v); changed = true; } }
    if (server.hasArg("speed")) { float v = server.arg("speed").toFloat(); if (v > 0.0f) { speedDisplay     = v; if (cbSetSpeed) cbSetSpeed(v); changed = true; } }
    if (server.hasArg("accel")) { float v = server.arg("accel").toFloat(); if (v > 0.0f) { accelDisplay     = v; if (cbSetAccel) cbSetAccel(v); changed = true; } }
    String json = "{" "\"turns\":" + String(openTurnsDisplay,2) + "," "\"speed\":" + String(speedDisplay,0) + "," "\"accel\":" + String(accelDisplay,0) + "}";
    server.send(changed ? 200 : 400, "application/json", json);
  }

  void handleLogs() {
    if (!isAuthenticated) { server.send(403,"text/plain","Non autorisé"); return; }
    String inm; if (server.hasHeader("If-None-Match")) inm = server.header("If-None-Match");
    String currentTag = String(logsVer);
    server.sendHeader("Cache-Control","no-cache");
    if (inm == currentTag) { server.send(304); return; }
    server.sendHeader("ETag", currentTag);
    server.send(200,"text/plain",logs);
  }

  void handleStatus() {
    if (!isAuthenticated) { server.send(403, "application/json", "{\"error\":\"Non autorisé\"}"); return; }
    WebUI_Status st{}; if (cbGetStatus) cbGetStatus(&st);
    String currentTag = String((unsigned long)((st.tempC*10.0f) + st.posTurns*100.0f + st.cycles + st.lastCalibMs + speedDisplay + accelDisplay));
    String inm; if (server.hasHeader("If-None-Match")) inm = server.header("If-None-Match");
    server.sendHeader("Cache-Control","no-cache");
    if (inm == currentTag) { server.send(304); return; }
    String json = "{" "\"temp\":" + String(st.tempC,2) + "," "\"lastCalib\":" + String((millis()-st.lastCalibMs)/1000) + "," "\"pos\":" + String(st.posTurns,2) + "," "\"cycles\":" + String(st.cycles) + "," "\"speed\":" + String(speedDisplay,0) + "," "\"accel\":" + String(accelDisplay,0) + "}";
    server.sendHeader("ETag", currentTag);
    server.send(200, "application/json", json);
  }
}

void WebUI::setCallbacks(VoidCb onOpen, VoidCb onClose, VoidCb onStop,
                         SetFloatCb onSetTurns, SetFloatCb onSetSpeed,
                         SetFloatCb onSetAccel, GetStatusCb getStatus) {
  cbOpen = onOpen; cbClose = onClose; cbStop = onStop;
  cbSetTurns = onSetTurns; cbSetSpeed = onSetSpeed; cbSetAccel = onSetAccel;
  cbGetStatus = getStatus;
}

void WebUI::begin(const char* ssid, const char* wifiPwd) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, wifiPwd);
  Serial.print("[START] Connexion au WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println(); Serial.print("Connecté, IP: "); Serial.println(WiFi.localIP());
  pushLog("[START] WiFi connecté: " + WiFi.localIP().toString());

  server.on("/", handleRoot);
  server.on("/login", handleLogin);
  server.on("/open", handleOpen);
  server.on("/close", handleClose);
  server.on("/stop", handleStop);
  server.on("/set", handleSet);
  server.on("/logs", handleLogs);
  server.on("/status", handleStatus);

  server.begin();
  Serial.println("[START] Serveur HTTP démarré");
  pushLog("[START] Serveur démarré");
}

void WebUI::loop() { server.handleClient(); }
void WebUI::addLog(const String& msg) { pushLog(msg); }
void WebUI::setOpenTurns(float v) { openTurnsDisplay = v; }
void WebUI::setSpeedDisplay(float v) { speedDisplay = v; }
void WebUI::setAccelDisplay(float v) { accelDisplay = v; }
