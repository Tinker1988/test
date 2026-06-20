// =====================================================================
//  BOOM BARRIER — DUAL RELAY TEST FIRMWARE (ESP32)
// =====================================================================
//  Two relays, MAINTAINED control (one relay per barrier):
//      relay ENERGIZED  -> barrier OPEN
//      relay RELEASED   -> barrier CLOSED
//  "Hold" just means leave the relay where it is — with a maintained
//  relay there are only these two positions.
//
//  Control surfaces (full build):
//    - Wi-Fi web dashboard  (SoftAP "BoomBarrierTest", http://192.168.4.1)
//    - serial monitor: o1/c1 = barrier 1 open/close, o2/c2 = barrier 2,
//                      ? = status
//
//  --------------------------------------------------------------------
//  WIRING
//  --------------------------------------------------------------------
//    Relay 1 IN -> GPIO25      Relay 2 IN -> GPIO26   (change below)
//    Relay VCC  -> 5V (VIN)    Relay GND  -> GND      (common ground)
//    Most cheap relay boards are ACTIVE-LOW (IN driven LOW = energized).
//    If OPEN/CLOSE come out swapped, flip RELAY_ACTIVE_LOW.
//
//  --------------------------------------------------------------------
//  COMPONENT TEST BUILD
//  --------------------------------------------------------------------
//  Building with -DTEST_relay (i.e. `make test-relay`) flashes a tiny
//  firmware that just toggles both relays OPEN/CLOSE on a loop — no
//  Wi-Fi, no web server — so you can confirm the relays and wiring.
// =====================================================================

#if defined(TEST_relay)
  #define TEST_MODE
#endif

#include <Arduino.h>
#ifndef TEST_MODE
#include <WiFi.h>
#include <WebServer.h>
#endif

// ── Relay config ─────────────────────────────────────
static const int  RELAY_PIN[2]     = { 25, 26 };  // relay 1, relay 2 input pins
static const bool RELAY_ACTIVE_LOW = true;         // true = IN LOW energizes the coil

static inline int activeLevel() { return RELAY_ACTIVE_LOW ? LOW  : HIGH; }
static inline int idleLevel()   { return RELAY_ACTIVE_LOW ? HIGH : LOW;  }

// ── Barrier state ────────────────────────────────────
// Index 0 = barrier 1, index 1 = barrier 2.
bool barrierOpen[2] = { false, false };   // false = CLOSED, true = OPEN

// Drive one barrier. `i` is the 0-based relay index.
static void setBarrier(int i, bool open) {
  digitalWrite(RELAY_PIN[i], open ? activeLevel() : idleLevel());
  barrierOpen[i] = open;
  Serial.printf("[BARRIER %d] %s\n", i + 1, open ? "OPEN" : "CLOSED");
}

static const char* stateName(int i) { return barrierOpen[i] ? "OPEN" : "CLOSED"; }

#ifndef TEST_MODE
// =====================================================================
//  Wi-Fi SoftAP + web dashboard
// =====================================================================
static const char* AP_SSID = "BoomBarrierTest";
static const char* AP_PASS = "barrier1234";   // >= 8 chars for WPA2

WebServer server(80);

static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Boom Barrier — Relay Test</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body { margin:0; font-family: system-ui, sans-serif; background:#0e1116; color:#e6e6e6; }
  header { padding:20px; text-align:center; }
  header h1 { margin:0; font-size:20px; }
  .wrap { max-width:640px; margin:0 auto; padding:0 16px 24px; }
  .gates { display:grid; grid-template-columns:1fr 1fr; gap:16px; }
  @media (max-width:520px){ .gates{ grid-template-columns:1fr; } }
  .card { background:#161b22; border:1px solid #30363d; border-radius:12px;
          padding:20px; text-align:center; }
  .card h2 { margin:0 0 6px; font-size:16px; }
  .state { font-size:13px; color:#8b949e; margin-bottom:14px; }
  .badge { display:inline-block; padding:4px 12px; border-radius:999px; font-weight:600; }
  .b-OPEN   { background:#23863633; color:#3fb950; }
  .b-CLOSED { background:#21262d;   color:#8b949e; }
  .btns { display:grid; grid-template-columns:1fr 1fr; gap:10px; }
  button { cursor:pointer; border:0; border-radius:10px; padding:14px; font-size:15px;
           font-weight:600; color:#fff; }
  button:active { transform:translateY(1px); }
  .open  { background:#238636; }
  .close { background:#1f6feb; }
</style>
</head>
<body>
<header><h1>Boom Barrier — Relay Test</h1></header>
<div class="wrap">
  <div class="gates">
    <div class="card">
      <h2>Barrier 1</h2>
      <div class="state">State: <span id="st1" class="badge b-CLOSED">CLOSED</span></div>
      <div class="btns">
        <button class="open"  onclick="cmd(1,'open')">OPEN</button>
        <button class="close" onclick="cmd(1,'close')">CLOSE</button>
      </div>
    </div>
    <div class="card">
      <h2>Barrier 2</h2>
      <div class="state">State: <span id="st2" class="badge b-CLOSED">CLOSED</span></div>
      <div class="btns">
        <button class="open"  onclick="cmd(2,'open')">OPEN</button>
        <button class="close" onclick="cmd(2,'close')">CLOSE</button>
      </div>
    </div>
  </div>
</div>
<script>
function setBadge(id, st){ var e=document.getElementById(id); e.textContent=st; e.className='badge b-'+st; }
function cmd(g, a){ fetch('/cmd?gate='+g+'&action='+a).then(refresh); }
function refresh(){
  fetch('/status').then(r=>r.json()).then(d=>{ setBadge('st1',d.g1); setBadge('st2',d.g2); }).catch(()=>{});
}
setInterval(refresh, 1000); refresh();
</script>
</body>
</html>)HTML";

static void handleRoot()   { server.send_P(200, "text/html", INDEX_HTML); }

static void handleStatus() {
  String j = "{\"g1\":\"";
  j += stateName(0);
  j += "\",\"g2\":\"";
  j += stateName(1);
  j += "\"}";
  server.send(200, "application/json", j);
}

static void handleCmd() {
  int gate = server.arg("gate").toInt();      // 1 or 2
  String action = server.arg("action");
  if ((gate == 1 || gate == 2) && (action == "open" || action == "close")) {
    setBarrier(gate - 1, action == "open");
    server.send(200, "application/json", "{\"ok\":true}");
  } else {
    server.send(400, "application/json", "{\"ok\":false}");
  }
}

static void startWebServer() {
  server.on("/",       HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/cmd",    HTTP_GET, handleCmd);
  server.onNotFound([](){ server.send(404, "text/plain", "not found"); });
  server.begin();
  Serial.println("[WEB] dashboard at http://192.168.4.1");
}

// ── Serial control: o1/c1 = barrier 1, o2/c2 = barrier 2, ? = status ──
static void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  line.toLowerCase();
  if (!line.length()) return;

  if (line == "?") {
    Serial.printf("[STATUS] B1=%s  B2=%s\n", stateName(0), stateName(1));
    return;
  }

  // Format: <o|c><1|2>  e.g. o1, c2
  char a = line[0];
  int  g = (line.length() > 1) ? (line[1] - '0') : 0;
  if ((a == 'o' || a == 'c') && (g == 1 || g == 2)) {
    setBarrier(g - 1, a == 'o');
  } else {
    Serial.println("[SERIAL] use o1/c1 o2/c2 (open/close) or ? (status)");
  }
}
#endif // !TEST_MODE

// =====================================================================
//  Arduino entry points
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  for (int i = 0; i < 2; i++) {
    pinMode(RELAY_PIN[i], OUTPUT);
    setBarrier(i, false);   // start CLOSED (known, safe state)
  }

#ifdef TEST_MODE
  Serial.println("\n[TEST] relay — toggling both relays OPEN/CLOSE on a loop");
#else
  Serial.println("\n[BOOT] Boom Barrier dual-relay firmware");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[AP] SSID '%s'  pass '%s'\n", AP_SSID, AP_PASS);
  Serial.printf("[AP] dashboard: http://%s\n", WiFi.softAPIP().toString().c_str());
  startWebServer();
  Serial.println("[READY] serial: o1/c1 o2/c2 (open/close), ? (status)");
#endif
}

void loop() {
#ifdef TEST_MODE
  setBarrier(0, true);  setBarrier(1, true);
  delay(3000);          // both held open
  setBarrier(0, false); setBarrier(1, false);
  delay(3000);          // both held closed
#else
  server.handleClient();
  handleSerial();
#endif
}
