#include "webui.h"

#include <WiFi.h>
#include <WebServer.h>

#include "app.h"

namespace WebUI {

static const char* AP_SSID = "openBPMcount";
static const char* AP_PASS = "beatmatch";   // >=8 chars required for WPA2

static WebServer server(80);
static bool   running = false;
static char   ipStr[20] = "0.0.0.0";
static float  liveBpm = 0.0f;

// ---------------------------------------------------------------------------
static String bpmText(float b) {
  if (b <= 0.0f) return String("--");
  return String(b, 1);
}

static String htmlEscape(const char* s) {
  String out;
  for (const char* p = s; *p; p++) {
    switch (*p) {
      case '&': out += "&amp;";  break;
      case '<': out += "&lt;";   break;
      case '>': out += "&gt;";   break;
      case '"': out += "&quot;"; break;
      default:  out += *p;       break;
    }
  }
  return out;
}

static void handleRoot() {
  String h;
  h.reserve(3000);
  h += F("<!doctype html><html><head><meta charset=utf-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>openBPMcount</title><style>"
         "body{background:#111;color:#eee;font-family:system-ui,sans-serif;margin:0;padding:16px}"
         "h1{font-size:20px;margin:0 0 4px}.live{color:#4ade80;font-size:15px;margin-bottom:16px}"
         "table{width:100%;border-collapse:collapse}"
         "td,th{padding:8px 6px;border-bottom:1px solid #333;text-align:left;font-size:15px}"
         "th{color:#888;font-weight:500;font-size:13px}"
         ".b{color:#4ade80;font-variant-numeric:tabular-nums;white-space:nowrap}"
         "input{background:#222;border:1px solid #444;color:#eee;padding:6px;border-radius:4px;width:100%;font-size:15px}"
         "button{background:#2563eb;border:0;color:#fff;padding:7px 12px;border-radius:4px;font-size:14px}"
         "a{color:#60a5fa}form{display:flex;gap:6px}"
         "</style></head><body><h1>openBPMcount</h1>");
  h += "<div class=live>live: " + bpmText(liveBpm) + " BPM</div>";
  h += F("<table><tr><th>#</th><th>BPM</th><th>Name</th><th></th></tr>");

  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    h += "<tr><td>";
    h += (char)('A' + i);
    h += "</td><td class=b>";
    h += bpmText(Lib::slots[i].bpm);
    h += "</td><td><form action=/set method=get><input type=hidden name=i value=";
    h += i;
    h += "><input name=n maxlength=16 value=\"";
    h += htmlEscape(Lib::slots[i].name);
    h += "\" placeholder=\"name this record\"><button>Save</button></form></td>";
    h += "<td><a href=/clear?i=";
    h += i;
    h += ">clear</a></td></tr>";
  }

  h += F("</table><p><a href=/csv>Download CSV</a></p></body></html>");
  server.send(200, "text/html", h);
}

static void handleSet() {
  if (server.hasArg("i") && server.hasArg("n")) {
    int i = server.arg("i").toInt();
    if (i >= 0 && i < NUM_SLOTS) Lib::setName((uint8_t)i, server.arg("n").c_str());
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleClear() {
  if (server.hasArg("i")) {
    int i = server.arg("i").toInt();
    if (i >= 0 && i < NUM_SLOTS) Lib::clear((uint8_t)i);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleCsv() {
  String csv = F("slot,bpm,name\n");
  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    csv += (char)('A' + i);
    csv += ',';
    csv += (Lib::slots[i].bpm > 0.0f) ? String(Lib::slots[i].bpm, 2) : String("");
    csv += ',';
    csv += Lib::slots[i].name;
    csv += '\n';
  }
  server.sendHeader("Content-Disposition", "attachment; filename=openbpmcount.csv");
  server.send(200, "text/csv", csv);
}

// ---------------------------------------------------------------------------
void start() {
  if (running) return;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

  server.on("/",      handleRoot);
  server.on("/set",   handleSet);
  server.on("/clear", handleClear);
  server.on("/csv",   handleCsv);
  server.begin();
  running = true;
}

void stop() {
  if (!running) return;
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  running = false;
}

void loop() {
  if (running) server.handleClient();
}

bool        isOn()     { return running; }
const char* ssid()     { return AP_SSID; }
const char* password() { return AP_PASS; }
const char* ip()       { return ipStr; }
void setLiveBpm(float b) { liveBpm = b; }

} // namespace WebUI
