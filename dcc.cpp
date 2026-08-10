#include "DCC.h"
#include <WiFi.h>

// --- DCC-EX Wireless Access Point Credentials ---
const char* WIFI_SSID = "DCCEX_CSB1";  // Change to match your CSB1 Wi-Fi SSID
const char* WIFI_PASS = "PASS_CSB1";   // CSB1 Wi-Fi password (or "" if open)
const char* CSB1_IP   = "192.168.4.1"; // CSB1 Default Access Point IP
const uint16_t CSB1_PORT = 2560;       // Standard WiThrottle / DCC-EX Port

WiFiClient dccClient;

EngineState engines[2] = {
  {4401, 0, true, 0}, // Default Engine 1 (Address 4401)
  {8302, 0, true, 0}  // Default Engine 2 (Address 8302)
};

uint8_t active_focus_idx = 0;
bool dccLinkUp = false;

// --- Reconnect tuning ---
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000; // Give up on boot-time WiFi.begin() after this long
static const unsigned long RECONNECT_RETRY_INTERVAL_MS = 3000; // How often maintainDCCConnection() retries a dropped link
static unsigned long lastReconnectAttempt = 0;

// NOTE ON COMMAND FORMAT: Some DCC-EX / CS firmware versions expect an optional
// legacy REGISTER slot before CAB: <t REGISTER CAB SPEED DIRECTION>. Most current
// builds accept the 3-arg form used below (<t CAB SPEED DIRECTION>), but if your
// CSB1 silently ignores throttle commands, test both forms against your CS's
// serial console before assuming this is a wiring/Wi-Fi problem.

static bool openCSB1Socket() {
  if (dccClient.connect(CSB1_IP, CSB1_PORT)) {
    Serial.println("DCC-EX TCP Socket Open!");
    sendDCCCommand("1"); // Enable Track Power (<1>)
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

// Call this once per loop() iteration. Non-blocking: only actually attempts
// reconnect work every RECONNECT_RETRY_INTERVAL_MS, and never uses delay().
void maintainDCCConnection() {
  bool linkGood = (WiFi.status() == WL_CONNECTED) && dccClient.connected();

  if (linkGood) {
    dccLinkUp = true;
    return;
  }

  dccLinkUp = false;

  unsigned long now = millis();
  if (now - lastReconnectAttempt < RECONNECT_RETRY_INTERVAL_MS) {
    return; // Not time to retry yet -- avoid hammering WiFi.begin()/connect()
  }
  lastReconnectAttempt = now;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi link down, retrying...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    return; // Give the next retry interval a chance to actually connect
  }

  if (!dccClient.connected()) {
    Serial.println("CSB1 TCP socket down, retrying...");
    dccClient.stop();
    dccLinkUp = openCSB1Socket();
  }
}

void sendDCCCommand(const char* cmd) {
  // Fixed-size stack buffer instead of Arduino String concatenation: every
  // throttle nudge used to allocate+free a String on the heap here. On a
  // device left running for hours with someone constantly turning a knob,
  // that repeated alloc/free churn is exactly what fragments the heap over
  // time. This path now does zero heap allocation.
  char packet[48];
  snprintf(packet, sizeof(packet), "<%s>", cmd);

  Serial.print("TX: ");
  Serial.println(packet);

  // Transmit wirelessly to CSB1
  if (dccClient.connected()) {
    dccClient.print(packet);
  }
}

void updateLocoThrottle(uint8_t idx) {
  // Format: <t CAB SPEED DIRECTION> -- see command-format note above initDCC()
  int dir = engines[idx].direction_fwd ? 1 : 0;
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "t %u %u %d", engines[idx].address, engines[idx].speed, dir);
  sendDCCCommand(cmd);
}

void toggleDCCFunction(uint8_t idx, uint8_t func_num) {
  engines[idx].functions ^= (1UL << func_num);
  bool is_on = (engines[idx].functions & (1UL << func_num)) != 0;

  // Format: <f CAB FN_NUM STATE>
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "f %u %u %d", engines[idx].address, func_num, is_on ? 1 : 0);
  sendDCCCommand(cmd);
}

void sendEStop(uint8_t idx) {
  engines[idx].speed = 0;
  // Speed -1 triggers Emergency Stop in DCC-EX
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "t %u -1 %d", engines[idx].address, engines[idx].direction_fwd ? 1 : 0);
  sendDCCCommand(cmd);
}