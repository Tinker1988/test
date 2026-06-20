// =====================================================================
//  BOOM BARRIER — MOTOR / RELAY TEST FIRMWARE (ESP32)
// =====================================================================
//  Purpose: bench-test the two boom-barrier motor controllers via their
//  relay trigger lines, driven either from:
//    - a Wi-Fi web dashboard (SoftAP "BoomBarrierTest", http://192.168.4.1)
//    - the serial monitor (type commands, see SERIAL COMMANDS below)
//
//  Each gate is driven like an industrial barrier head: three MOMENTARY
//  dry-contact inputs (OPEN / CLOSE / STOP). We pulse the matching relay
//  for RELAY_PULSE_MS, then release it. A small state machine tracks the
//  arm position and (optionally) auto-closes after AUTO_CLOSE_MS.
//
//  --------------------------------------------------------------------
//  PIN MAP (relay module IN pins -> ESP32 GPIO). Adjust to your board.
//  --------------------------------------------------------------------
//   Gate 1 (Entry):   OPEN=GPIO25   CLOSE=GPIO26   STOP=GPIO27
//   Gate 2 (Exit):    OPEN=GPIO32   CLOSE=GPIO33   STOP=GPIO14
//   Relay VCC -> 5V (VIN), GND -> GND (COMMON GROUND required).
//   For opto-isolated boards, JD-VCC -> 3V3.
//
//  Most cheap relay boards are ACTIVE-LOW (IN pulled LOW energizes the
//  coil). Set RELAY_ACTIVE_LOW accordingly.
//
//  SERIAL COMMANDS (115200 baud, newline-terminated):
//   o1 / c1 / s1   -> gate 1 open / close / stop
//   o2 / c2 / s2   -> gate 2 open / close / stop
//   ?              -> print current state
// =====================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// ── Relay pin map ────────────────────────────────────
static const int G1_OPEN_PIN  = 25;
static const int G1_CLOSE_PIN = 26;
static const int G1_STOP_PIN  = 27;
static const int G2_OPEN_PIN  = 32;
static const int G2_CLOSE_PIN = 33;
static const int G2_STOP_PIN  = 14;

// ── Relay polarity ───────────────────────────────────
// true  = active-low board (IN driven LOW triggers the relay) — most common
// false = active-high board
static const bool RELAY_ACTIVE_LOW = true;

static inline int activeLevel() { return RELAY_ACTIVE_LOW ? LOW  : HIGH; }
static inline int idleLevel()   { return RELAY_ACTIVE_LOW ? HIGH : LOW;  }

// ── Wi-Fi SoftAP (dashboard access point) ────────────
static const char* AP_SSID = "BoomBarrierTest";
static const char* AP_PASS = "barrier1234";   // >= 8 chars for WPA2

WebServer server(80);

// ── Gate state tracking ──────────────────────────────
enum GateState { GATE_IDLE, GATE_OPENING, GATE_OPEN, GATE_CLOSING };

GateState gate1State = GATE_IDLE;
GateState gate2State = GATE_IDLE;

uint32_t gate1PulseOffAt  = 0;   // when to release the relay pulse
uint32_t gate1AutoCloseAt = 0;   // when to send close command
uint32_t gate2PulseOffAt  = 0;
uint32_t gate2AutoCloseAt = 0;

// Timing config
static const uint32_t RELAY_PULSE_MS   = 300;    // duration of the trigger pulse
static const uint32_t AUTO_CLOSE_MS    = 5000;   // hold open for 5 seconds then close
static const bool     AUTO_CLOSE_ENABLED = true;

// ── Pulse helpers ────────────────────────────────────
static void pulsePin(int pin) {
  digitalWrite(pin, activeLevel());
}

static void releasePin(int pin) {
  digitalWrite(pin, idleLevel());
}

// ── Gate 1 commands ──────────────────────────────────
static void gate1Open() {
  releasePin(G1_CLOSE_PIN);
  releasePin(G1_STOP_PIN);
  pulsePin(G1_OPEN_PIN);                          // momentary trigger
  gate1PulseOffAt  = millis() + RELAY_PULSE_MS;
  gate1AutoCloseAt = millis() + AUTO_CLOSE_MS;
  gate1State = GATE_OPENING;
  Serial.println("[G1] → OPENING");
}

static void gate1Close() {
  releasePin(G1_OPEN_PIN);
  releasePin(G1_STOP_PIN);
  pulsePin(G1_CLOSE_PIN);                         // momentary trigger
  gate1PulseOffAt = millis() + RELAY_PULSE_MS;
  gate1State = GATE_CLOSING;
  Serial.println("[G1] → CLOSING");
}

static void gate1Stop() {
  releasePin(G1_OPEN_PIN);
  releasePin(G1_CLOSE_PIN);
  pulsePin(G1_STOP_PIN);                          // momentary trigger
  gate1PulseOffAt = millis() + RELAY_PULSE_MS;
  gate1State = GATE_IDLE;
  Serial.println("[G1] → STOPPED");
}

// ── Gate 2 commands (identical pattern) ─────────────
static void gate2Open() {
  releasePin(G2_CLOSE_PIN);
  releasePin(G2_STOP_PIN);
  pulsePin(G2_OPEN_PIN);
  gate2PulseOffAt  = millis() + RELAY_PULSE_MS;
  gate2AutoCloseAt = millis() + AUTO_CLOSE_MS;
  gate2State = GATE_OPENING;
  Serial.println("[G2] → OPENING");
}

static void gate2Close() {
  releasePin(G2_OPEN_PIN);
  releasePin(G2_STOP_PIN);
  pulsePin(G2_CLOSE_PIN);
  gate2PulseOffAt = millis() + RELAY_PULSE_MS;
  gate2State = GATE_CLOSING;
  Serial.println("[G2] → CLOSING");
}

static void gate2Stop() {
  releasePin(G2_OPEN_PIN);
  releasePin(G2_CLOSE_PIN);
  pulsePin(G2_STOP_PIN);
  gate2PulseOffAt = millis() + RELAY_PULSE_MS;
  gate2State = GATE_IDLE;
  Serial.println("[G2] → STOPPED");
}

// ── Tick — call this every loop() ───────────────────
static void tickGates() {
  uint32_t now = millis();

  // ── Gate 1 pulse release ──
  if ((gate1State == GATE_OPENING || gate1State == GATE_CLOSING)
       && (int32_t)(now - gate1PulseOffAt) >= 0) {
    releasePin(G1_OPEN_PIN);
    releasePin(G1_CLOSE_PIN);
    releasePin(G1_STOP_PIN);

    if (gate1State == GATE_OPENING) {
      gate1State = GATE_OPEN;      // pulse sent, arm should be rising
      Serial.println("[G1] pulse done → OPEN state");
    } else {
      gate1State = GATE_IDLE;      // close pulse sent, arm descending
      Serial.println("[G1] pulse done → IDLE state");
    }
  }

  // ── Gate 1 auto-close ──
  if (AUTO_CLOSE_ENABLED
      && gate1State == GATE_OPEN
      && (int32_t)(now - gate1AutoCloseAt) >= 0) {
    Serial.println("[G1] auto-close triggered");
    gate1Close();
  }

  // ── Gate 2 pulse release ──
  if ((gate2State == GATE_OPENING || gate2State == GATE_CLOSING)
       && (int32_t)(now - gate2PulseOffAt) >= 0) {
    releasePin(G2_OPEN_PIN);
    releasePin(G2_CLOSE_PIN);
    releasePin(G2_STOP_PIN);

    if (gate2State == GATE_OPENING) {
      gate2State = GATE_OPEN;
      Serial.println("[G2] pulse done → OPEN state");
    } else {
      gate2State = GATE_IDLE;
      Serial.println("[G2] pulse done → IDLE state");
    }
  }

  // ── Gate 2 auto-close ──
  if (AUTO_CLOSE_ENABLED
      && gate2State == GATE_OPEN
      && (int32_t)(now - gate2AutoCloseAt) >= 0) {
    Serial.println("[G2] auto-close triggered");
    gate2Close();
  }
}

// =====================================================================
//  Pin init — drive every relay to its IDLE (released) level first so
//  nothing fires while the ESP32 boots.
// =====================================================================
static void initRelays() {
  const int pins[] = { G1_OPEN_PIN, G1_CLOSE_PIN, G1_STOP_PIN,
                       G2_OPEN_PIN, G2_CLOSE_PIN, G2_STOP_PIN };
  for (int p : pins) {
    pinMode(p, OUTPUT);
    digitalWrite(p, idleLevel());
  }
}

// =====================================================================
//  Command dispatch — shared by web + serial
// =====================================================================
// gate = 1|2, action = "open"|"close"|"stop". Returns false on bad args.
static bool runCommand(int gate, const String& action) {
  if (gate == 1) {
    if      (action == "open")  gate1Open();
    else if (action == "close") gate1Close();
    else if (action == "stop")  gate1Stop();
    else return false;
    return true;
  }
  if (gate == 2) {
    if      (action == "open")  gate2Open();
    else if (action == "close") gate2Close();
    else if (action == "stop")  gate2Stop();
    else return false;
    return true;
  }
  return false;
}

static const char* stateName(GateState s) {
  switch (s) {
    case GATE_IDLE:    return "IDLE";
    case GATE_OPENING: return "OPENING";
    case GATE_OPEN:    return "OPEN";
    case GATE_CLOSING: return "CLOSING";
  }
  return "?";
}

// =====================================================================
//  Web dashboard
// =====================================================================
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
  header { padding:20px; text-align:center; background:#161b22; border-bottom:1px solid #30363d; }
  header h1 { margin:0; font-size:20px; }
  header small { color:#8b949e; }
  .wrap { max-width:760px; margin:0 auto; padding:20px; }
  .gates { display:grid; grid-template-columns:1fr 1fr; gap:18px; }
  @media (max-width:560px){ .gates{ grid-template-columns:1fr; } }
  .card { background:#161b22; border:1px solid #30363d; border-radius:12px; padding:18px; }
  .card h2 { margin:0 0 4px; font-size:16px; }
  .state { font-size:13px; color:#8b949e; margin-bottom:14px; }
  .badge { display:inline-block; padding:3px 10px; border-radius:999px; font-weight:600; font-size:12px; }
  .b-IDLE    { background:#21262d; color:#8b949e; }
  .b-OPENING { background:#1f6feb33; color:#58a6ff; }
  .b-OPEN    { background:#23863633; color:#3fb950; }
  .b-CLOSING { background:#9e6a0333; color:#d29922; }
  .btns { display:grid; grid-template-columns:1fr 1fr 1fr; gap:10px; }
  button { cursor:pointer; border:0; border-radius:10px; padding:14px 8px; font-size:15px; font-weight:600; color:#fff; }
  button:active { transform:translateY(1px); }
  .open  { background:#238636; }
  .close { background:#1f6feb; }
  .stop  { background:#da3633; }
  .allstop { width:100%; margin-top:18px; padding:16px; background:#da3633; }
  footer { text-align:center; color:#8b949e; font-size:12px; padding:16px; }
</style>
</head>
<body>
<header>
  <h1>Boom Barrier — Relay Test</h1>
  <small>Live motor/relay control</small>
</header>
<div class="wrap">
  <div class="gates">
    <div class="card">
      <h2>Gate 1 · Entry</h2>
      <div class="state">State: <span id="s1" class="badge b-IDLE">IDLE</span></div>
      <div class="btns">
        <button class="open"  onclick="cmd(1,'open')">OPEN</button>
        <button class="close" onclick="cmd(1,'close')">CLOSE</button>
        <button class="stop"  onclick="cmd(1,'stop')">STOP</button>
      </div>
    </div>
    <div class="card">
      <h2>Gate 2 · Exit</h2>
      <div class="state">State: <span id="s2" class="badge b-IDLE">IDLE</span></div>
      <div class="btns">
        <button class="open"  onclick="cmd(2,'open')">OPEN</button>
        <button class="close" onclick="cmd(2,'close')">CLOSE</button>
        <button class="stop"  onclick="cmd(2,'stop')">STOP</button>
      </div>
    </div>
  </div>
  <button class="allstop stop" onclick="cmd(1,'stop');cmd(2,'stop')">EMERGENCY STOP (BOTH)</button>
</div>
<footer>ESP32 SoftAP · refreshes every second</footer>
<script>
function setBadge(el, st){ el.textContent = st; el.className = 'badge b-' + st; }
function cmd(gate, action){
  fetch('/cmd?gate=' + gate + '&action=' + action).then(refresh);
}
function refresh(){
  fetch('/status').then(r => r.json()).then(d => {
    setBadge(document.getElementById('s1'), d.g1);
    setBadge(document.getElementById('s2'), d.g2);
  }).catch(()=>{});
}
setInterval(refresh, 1000);
refresh();
</script>
</body>
</html>)HTML";

static void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

static void handleStatus() {
  String j = "{\"g1\":\"";
  j += stateName(gate1State);
  j += "\",\"g2\":\"";
  j += stateName(gate2State);
  j += "\"}";
  server.send(200, "application/json", j);
}

static void handleCmd() {
  int gate = server.arg("gate").toInt();
  String action = server.arg("action");
  if (runCommand(gate, action)) {
    server.send(200, "application/json", "{\"ok\":true}");
  } else {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"bad gate/action\"}");
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

// =====================================================================
//  Serial command interface (for testing without the web UI)
// =====================================================================
static void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  line.toLowerCase();
  if (!line.length()) return;

  if (line == "?") {
    Serial.printf("[STATUS] G1=%s  G2=%s\n",
                  stateName(gate1State), stateName(gate2State));
    return;
  }

  // Format: <action-letter><gate-digit>  e.g. o1, c2, s1
  char a = line[0];
  int  g = (line.length() > 1) ? (line[1] - '0') : 0;
  String action = (a == 'o') ? "open" : (a == 'c') ? "close" : (a == 's') ? "stop" : "";
  if (!action.length() || !runCommand(g, action)) {
    Serial.printf("[SERIAL] unknown cmd '%s' (use o1/c1/s1/o2/c2/s2 or ?)\n",
                  line.c_str());
  }
}

// =====================================================================
//  Arduino entry points
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[BOOT] Boom Barrier relay test firmware");

  initRelays();   // safe state before anything else

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[AP] SSID '%s'  pass '%s'\n", AP_SSID, AP_PASS);
  Serial.printf("[AP] dashboard: http://%s\n", WiFi.softAPIP().toString().c_str());

  startWebServer();

  Serial.println("[READY] serial cmds: o1 c1 s1 o2 c2 s2  (? for status)");
}

void loop() {
  server.handleClient();
  handleSerial();
  tickGates();
}
