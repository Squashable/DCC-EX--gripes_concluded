#include "dcc.h"
#include <WiFi.h>

// --- DCC-EX Wireless Access Point Credentials ---
const char* WIFI_SSID = "DCCEX_CSB1";  // Change to match your CSB1 Wi-Fi SSID
const char* WIFI_PASS = "PASS_CSB1";   // CSB1 Wi-Fi password (or "" if open)
const char* CSB1_IP   = "192.168.4.1"; // CSB1 Default Access Point IP
const uint16_t CSB1_PORT = 2560;       // Standard DCC-EX / WiThrottle port

// --- DC mode configuration ---
// DC mode tells the CSB1 to run one track in DC mode with a given cab id:
//   <= A DC 3>   (track A -> DC, driven as cab 3)    ...and <= A DCC> to go back.
// Check these against the track your DC loco is actually wired to, and confirm
// the syntax against the DCC-EX command reference for your CSB1 firmware.
static const char     DC_TRACK_LETTER = 'A';
static const uint16_t DC_CAB_ID       = 3;

WiFiClient dccClient;

EngineState engines[2] = {
  {4401, 0, true, 0}, // Default Engine 1 (Address 4401)
  {8302, 0, true, 0}  // Default Engine 2 (Address 8302)
};

uint8_t active_focus_idx = 0;
bool dccLinkUp = false;

bool dcModeActive = false;
EngineState dcEngine = {DC_CAB_ID, 0, true, 0};

// --- Reconnect tuning ---
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;    // Give up on boot-time WiFi.begin() after this long
static const unsigned long WIFI_RETRY_INTERVAL_MS = 10000;     // Restart a stalled Wi-Fi attempt no more often than this
static const unsigned long RECONNECT_RETRY_INTERVAL_MS = 3000; // How often the TCP socket is retried once Wi-Fi is up
static const int32_t TCP_CONNECT_TIMEOUT_MS = 1000;            // Keep a failed connect() from freezing the loop for seconds
static unsigned long lastReconnectAttempt = 0;
static unsigned long lastWifiBegin = 0;

// Track power is switched on only the first time we connect after boot. Doing
// it on every reconnect would override someone deliberately cutting power.
static bool powerOnSent = false;

// Set when a DC/DCC track-mode change could not be sent (link was down), so
// resyncAfterReconnect() knows the CSB1 is out of step with the handheld.
static bool trackModeDirty = false;

// NOTE ON COMMAND FORMAT: throttle uses <t CAB SPEED DIRECTION> and functions
// use <F CAB FUNC STATE> (capital F -- lowercase <f> is the legacy byte-based
// form). If your CSB1 firmware is older, test these against its serial console.

static bool openCSB1Socket() {
  if (dccClient.connect(CSB1_IP, CSB1_PORT, TCP_CONNECT_TIMEOUT_MS)) {
    Serial.println("DCC-EX TCP Socket Open!");
    if (!powerOnSent) {
      sendDCCCommand("1"); // Enable Track Power (<1>) -- first connect after boot only
      powerOnSent = true;
    }
    return true;
  }
  Serial.println("Connection to CSB1 TCP Socket Failed!");
  return false;
}

void initDCC() {
  Serial.begin(115200);

  // Connect to the CSB1 Base Station over Wi-Fi (bounded wait so we don't hang forever
  // if the CSB1 AP isn't up yet -- maintainDCCConnection() will keep retrying afterward)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lastWifiBegin = millis();

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to DCC-EX Base Station via Wi-Fi!");
    dccLinkUp = openCSB1Socket();
  } else {
    Serial.println("\nWi-Fi connect timed out -- will keep retrying in the background.");
    dccLinkUp = false;
  }

  lastReconnectAttempt = millis();
}

// The CSB1 broadcasts status back to every client (power state, loco updates).
// Nothing here uses it, but if it is never read the TCP receive buffer fills
// and the link eventually stalls. Read and discard a bounded amount per call.
static void drainRx() {
  uint8_t sink[64];
  for (int i = 0; i < 8 && dccClient.available() > 0; i++) {
    dccClient.read(sink, sizeof(sink));
  }
}

// Call this once per loop() iteration. Non-blocking apart from a short, bounded
// TCP connect attempt every RECONNECT_RETRY_INTERVAL_MS while the socket is down.
void maintainDCCConnection() {
  drainRx();

  bool wifiUp = (WiFi.status() == WL_CONNECTED);
  if (wifiUp && dccClient.connected()) {
    dccLinkUp = true;
    return;
  }

  dccLinkUp = false;
  unsigned long now = millis();

  if (!wifiUp) {
    // Wi-Fi association can take several seconds. Only restart the attempt
    // occasionally, otherwise each retry cancels the one still in progress.
    if (now - lastWifiBegin >= WIFI_RETRY_INTERVAL_MS) {
      Serial.println("Wi-Fi link down, retrying...");
      lastWifiBegin = now;
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    return;
  }

  if (now - lastReconnectAttempt < RECONNECT_RETRY_INTERVAL_MS) {
    return; // Not time to retry yet
  }
  lastReconnectAttempt = now;

  Serial.println("CSB1 TCP socket down, retrying...");
  dccClient.stop();
  dccLinkUp = openCSB1Socket();
}

bool sendDCCCommand(const char* cmd) {
  // Fixed-size stack buffer instead of Arduino String concatenation: every
  // throttle nudge used to allocate+free a String on the heap here, which
  // fragments the heap on a device left running for hours.
  char packet[48];
  snprintf(packet, sizeof(packet), "<%s>", cmd);

  Serial.print("TX: ");
  Serial.println(packet);

  // Transmit wirelessly to CSB1
  if (dccClient.connected()) {
    dccClient.print(packet);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// DCC engines
// ---------------------------------------------------------------------------
static void sendThrottle(const EngineState &e) {
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "t %u %u %d", e.address, e.speed, e.direction_fwd ? 1 : 0);
  sendDCCCommand(cmd);
}

static void sendEmergencyStop(EngineState &e) {
  e.speed = 0;
  // Speed -1 triggers Emergency Stop in DCC-EX
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "t %u -1 %d", e.address, e.direction_fwd ? 1 : 0);
  sendDCCCommand(cmd);
}

void updateLocoThrottle(uint8_t idx) {
  sendThrottle(engines[idx]);
}

void toggleDCCFunction(uint8_t idx, uint8_t func_num) {
  engines[idx].functions ^= (1UL << func_num);
  bool is_on = (engines[idx].functions & (1UL << func_num)) != 0;

  // Format: <F CAB FUNC STATE>
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "F %u %u %d", engines[idx].address, func_num, is_on ? 1 : 0);
  sendDCCCommand(cmd);
}

void sendEStop(uint8_t idx) {
  sendEmergencyStop(engines[idx]);
}

// ---------------------------------------------------------------------------
// DC mode (single engine)
// ---------------------------------------------------------------------------
static void sendTrackMode(bool dc) {
  char cmd[32];
  if (dc) {
    snprintf(cmd, sizeof(cmd), "= %c DC %u", DC_TRACK_LETTER, DC_CAB_ID);
  } else {
    snprintf(cmd, sizeof(cmd), "= %c DCC", DC_TRACK_LETTER);
  }
  trackModeDirty = !sendDCCCommand(cmd);
}

void enterDCMode() {
  // Make sure both DCC engines are stopped before the track changes mode.
  for (uint8_t e = 0; e < 2; e++) {
    engines[e].speed = 0;
    updateLocoThrottle(e);
  }
  sendTrackMode(true);
  dcEngine.speed = 0;
  dcModeActive = true;
  updateDCThrottle();
}

void exitDCMode() {
  dcEngine.speed = 0;
  updateDCThrottle();
  sendTrackMode(false);
  dcModeActive = false;
}

void updateDCThrottle() {
  sendThrottle(dcEngine);
}

void sendDCEStop() {
  sendEmergencyStop(dcEngine);
}

void stopAllThrottles() {
  if (dcModeActive) {
    dcEngine.speed = 0;
    updateDCThrottle();
  } else {
    for (uint8_t e = 0; e < 2; e++) {
      engines[e].speed = 0;
      updateLocoThrottle(e);
    }
  }
}

// Called once each time the link comes back. Input is ignored while offline
// (see loop()), so throttle state already matches the CSB1; the only thing that
// can be out of step is a DC/DCC track-mode change made while offline.
void resyncAfterReconnect() {
  if (!trackModeDirty) return;
  sendTrackMode(dcModeActive);
  if (dcModeActive) updateDCThrottle();
}
