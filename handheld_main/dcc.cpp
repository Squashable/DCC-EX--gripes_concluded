#include "dcc.h"
#include <WiFi.h>

// --- DCC-EX Wireless Access Point Credentials ---
const char* WIFI_SSID = "DCCEX_CSB1";  // Change to match your CSB1 Wi-Fi SSID
const char* WIFI_PASS = "PASS_CSB1";   // CSB1 Wi-Fi password (or "" if open)
const char* CSB1_IP   = "192.168.4.1"; // CSB1 Default Access Point IP
const uint16_t CSB1_PORT = 2560;       // Standard DCC-EX / WiThrottle port

// --- DC mode configuration ---
// DC mode tells the CSB1 to run one track in DC mode with a given cab id:
//   <= A DC 3>   (track A -> DC, driven as cab 3)    ...and <= A MAIN> to go back.
// DCC-EX turns track power off whenever a track's mode changes, so <1 A> is
// sent afterwards to switch it back on. This is the SAME track that normally
// runs your DCC locos: the DC switch flips it to DC and back to MAIN. Set the
// letter to that track (never your PROG track).
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

// Programming-track state (see dcc.h)
ProgState progState = PROG_IDLE;
uint16_t progAddr = 0;
static unsigned long progStartMs = 0;
static bool progReadSent = false;
static const unsigned long PROG_VERIFY_AFTER_MS = 3000;   // if the write gave no reply, ask <R> after this long
static const unsigned long PROG_TIMEOUT_MS = 30000;       // give up entirely after this long

// Set when a DC/DCC track-mode change could not be sent (link was down), so
// resyncAfterReconnect() knows the CSB1 is out of step with the handheld.
static bool trackModeDirty = false;

// Stops that could not be sent because the link was down. Slot 0/1 = DCC
// engines, slot 2 = the DC loco. They are replayed (before any track-mode
// change) when the link comes back, so the layout never keeps running a loco
// the screen shows as stopped.
static const uint8_t DC_SLOT = 2;
static bool     pendingStop[3]      = {false, false, false};
static bool     pendingEmergency[3] = {false, false, false};
static uint16_t pendingAddr[3]      = {0, 0, 0};
static bool     pendingDirFwd[3]    = {true, true, true};

// NOTE ON COMMAND FORMAT: throttle uses <t CAB SPEED DIRECTION> and functions
// use <F CAB FUNC STATE> (capital F -- lowercase <f> is the legacy byte-based
// form). If your CSB1 firmware is older, test these against its serial console.

static bool openCSB1Socket() {
  if (dccClient.connect(CSB1_IP, CSB1_PORT, TCP_CONNECT_TIMEOUT_MS)) {
    Serial.println("DCC-EX TCP Socket Open!");
    if (!powerOnSent) {
      sendDCCCommand("1"); // Enable Track Power (<1>) -- first connect after boot only
      powerOnSent = true;
    } else {
      // Reconnect: replay anything that couldn't be sent while offline. Done
      // here as well as in loop() because a drop that is repaired within a
      // single maintainDCCConnection() call is never seen as "down" by loop().
      resyncAfterReconnect();
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

// Handles one complete reply, the text between '<' and '>'. Only the loco
// address replies matter here: <w N> / <r N> with a single number, where a
// negative N is an error. Replies with extra fields (e.g. <r CV VALUE>) and
// all other broadcasts are ignored.
static void handleReply(const char* body) {
  if (progState != PROG_WRITING && progState != PROG_VERIFYING) return;
  if (body[0] != 'r' && body[0] != 'w') return;

  char* end = nullptr;
  long v = strtol(body + 1, &end, 10);
  if (end == body + 1) return;                 // no number
  while (*end == ' ') end++;
  if (*end != '\0') return;                    // more fields -> not an address reply

  progState = (v == (long)progAddr) ? PROG_OK : PROG_FAIL;
}

// The CSB1 broadcasts status back to every client (power state, loco updates).
// If it is never read the TCP receive buffer fills and the link eventually
// stalls, so read a bounded amount per call and pick out the programming
// replies as they go past.
static void drainRx() {
  static char rxBuf[40];
  static uint8_t rxLen = 0;
  static bool inFrame = false;

  for (int n = 0; n < 256 && dccClient.available() > 0; n++) {
    int c = dccClient.read();
    if (c < 0) break;
    if (c == '<') {
      inFrame = true;
      rxLen = 0;
    } else if (c == '>' && inFrame) {
      rxBuf[rxLen] = '\0';
      inFrame = false;
      handleReply(rxBuf);
    } else if (inFrame) {
      if (rxLen < sizeof(rxBuf) - 1) rxBuf[rxLen++] = (char)c;
      else inFrame = false;                    // overlong frame: drop it
    }
  }
}

// Sends <R> to confirm a write if the CSB1 hasn't replied yet, and times out a
// programming attempt that never completes.
static void serviceProgramming() {
  if (progState != PROG_WRITING && progState != PROG_VERIFYING) return;
  unsigned long now = millis();
  if (now - progStartMs >= PROG_TIMEOUT_MS) {
    progState = PROG_FAIL;
    return;
  }
  if (!progReadSent && now - progStartMs >= PROG_VERIFY_AFTER_MS) {
    progReadSent = true;
    progState = PROG_VERIFYING;
    sendDCCCommand("R");
  }
}

bool programmingBusy() {
  return progState == PROG_WRITING || progState == PROG_VERIFYING;
}

void startProgramAddress(uint16_t addr) {
  progAddr = addr;
  progReadSent = false;
  progStartMs = millis();
  char cmd[24];
  snprintf(cmd, sizeof(cmd), "W %u", addr);
  progState = sendDCCCommand(cmd) ? PROG_WRITING : PROG_FAIL;
}

void clearProgResult() {
  if (progState == PROG_OK || progState == PROG_FAIL) progState = PROG_IDLE;
}

// Call this once per loop() iteration. Non-blocking apart from a short, bounded
// TCP connect attempt every RECONNECT_RETRY_INTERVAL_MS while the socket is down.
void maintainDCCConnection() {
  drainRx();
  serviceProgramming();

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

// Stops the loco at e.address (slot = 0/1 engine, DC_SLOT for the DC loco).
// emergency = true sends speed -1 (E-stop), false sends speed 0. If the command
// can't be sent the stop is remembered and replayed on reconnect.
static void sendStop(uint8_t slot, EngineState &e, bool emergency) {
  e.speed = 0;
  char cmd[32];
  // Speed -1 triggers Emergency Stop in DCC-EX
  snprintf(cmd, sizeof(cmd), "t %u %d %d", e.address, emergency ? -1 : 0, e.direction_fwd ? 1 : 0);
  if (sendDCCCommand(cmd)) {
    pendingStop[slot] = false;
  } else {
    // Keep an earlier pending emergency stop even if a later plain stop fails too
    pendingEmergency[slot] = pendingStop[slot] ? (pendingEmergency[slot] || emergency) : emergency;
    pendingStop[slot] = true;
    pendingAddr[slot] = e.address;
    pendingDirFwd[slot] = e.direction_fwd;
  }
}

static void sendEmergencyStop(uint8_t slot, EngineState &e) {
  sendStop(slot, e, true);
}

void updateLocoThrottle(uint8_t idx) {
  sendThrottle(engines[idx]);
}

void changeEngineAddress(uint8_t idx, uint16_t newAddr) {
  sendStop(idx, engines[idx], false); // stop the loco we are about to stop controlling
  engines[idx].address = newAddr;
  engines[idx].speed = 0;
  engines[idx].functions = 0;
  updateLocoThrottle(idx);
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
  sendEmergencyStop(idx, engines[idx]);
}

// ---------------------------------------------------------------------------
// DC mode (single engine)
// ---------------------------------------------------------------------------
static void sendTrackMode(bool dc) {
  char cmd[32];
  if (dc) {
    snprintf(cmd, sizeof(cmd), "= %c DC %u", DC_TRACK_LETTER, DC_CAB_ID);
  } else {
    snprintf(cmd, sizeof(cmd), "= %c MAIN", DC_TRACK_LETTER);
  }
  trackModeDirty = !sendDCCCommand(cmd);
  if (!trackModeDirty) {
    // DCC-EX switches track power off whenever the mode changes -- turn this track back on.
    snprintf(cmd, sizeof(cmd), "1 %c", DC_TRACK_LETTER);
    sendDCCCommand(cmd);
  }
}

void enterDCMode() {
  // Make sure both DCC engines are stopped before the track changes mode.
  for (uint8_t e = 0; e < 2; e++) {
    sendStop(e, engines[e], false);
  }
  sendTrackMode(true);
  dcEngine.speed = 0;
  dcModeActive = true;
  updateDCThrottle();
}

void exitDCMode() {
  sendStop(DC_SLOT, dcEngine, false);
  sendTrackMode(false);
  dcModeActive = false;
}

void updateDCThrottle() {
  sendThrottle(dcEngine);
}

void sendDCEStop() {
  sendEmergencyStop(DC_SLOT, dcEngine);
}

void stopAllThrottles() {
  if (dcModeActive) {
    sendStop(DC_SLOT, dcEngine, false);
  } else {
    for (uint8_t e = 0; e < 2; e++) {
      sendStop(e, engines[e], false);
    }
  }
}

// Called each time the link comes back. Throttle/direction/function input is
// ignored while offline (see loop()), so those already match the CSB1. What can
// be out of step: stops (E-stop, debug toggle, address change, DC switch) that
// were made while offline, and a DC/DCC track-mode change. Stops go first, so
// nothing is still moving when the track mode changes.
void resyncAfterReconnect() {
  for (uint8_t s = 0; s < 3; s++) {
    if (!pendingStop[s]) continue;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "t %u %d %d", pendingAddr[s], pendingEmergency[s] ? -1 : 0,
             pendingDirFwd[s] ? 1 : 0);
    if (sendDCCCommand(cmd)) pendingStop[s] = false;
  }
  if (!trackModeDirty) return;
  sendTrackMode(dcModeActive);
  if (dcModeActive) updateDCThrottle();
}
