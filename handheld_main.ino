#include <Arduino.h>
#include <ESP32Encoder.h>
#include <Keypad.h>

#include "DCC.h"
#include "UI.h"
#include "inputs.h"

// --- Hardware Pin Definitions ---
const uint8_t PIN_ENCODER_1_A = 19;
const uint8_t PIN_ENCODER_1_B = 21;
const uint8_t PIN_ENCODER_1_SWITCH = 22;  // Engine 1 Direction Toggle

const uint8_t PIN_ENCODER_2_A = 4;
const uint8_t PIN_ENCODER_2_B = 0;
const uint8_t PIN_ENCODER_2_SWITCH = 2;   // Engine 2 Direction Toggle

const uint8_t PIN_BUTTON_SELECT_E1 = 15;  // Route Numpad/DC Switch to Engine 1
const uint8_t PIN_BUTTON_SELECT_E2 = 26;  // Route Numpad/DC Switch to Engine 2

// NOTE: GPIO16 is used by the OLED's PIN_OLED_DATA_COMMAND line (see ui.cpp) -- it was
// previously double-booked with this switch, which would corrupt display
// output whenever the switch was toggled. Moved to GPIO36, an input-only
// pin with no internal pull resistor, same as the E-stop pins below.
const uint8_t PIN_SWITCH_DC_TOGGLE = 36;  // Physical SPST/SPDT Switch for DC / DCC Mode (needs external 10k pull-up resistor)

const uint8_t PIN_BUTTON_ESTOP_E1 = 34;   // E-Stop Engine 1 (Needs external 10k pull-up resistor)
const uint8_t PIN_BUTTON_ESTOP_E2 = 35;   // E-Stop Engine 2 (Needs external 10k pull-up resistor)

// --- Memory for Restoring Addresses when Exiting DC Mode ---
static uint16_t saved_dcc_addr[2] = {4401, 8302};
static int8_t dc_mode_owner_idx = -1; // Which engine (0/1) the DC switch currently owns, -1 = none

// --- 3x4 Matrix Keypad Configuration (7 Pins Total) ---
const byte ROWS = 4;
const byte COLS = 3;

const uint8_t PIN_KEYPAD_ROW_1 = 13;
const uint8_t PIN_KEYPAD_ROW_2 = 12;
const uint8_t PIN_KEYPAD_ROW_3 = 14;
const uint8_t PIN_KEYPAD_ROW_4 = 27;

const uint8_t PIN_KEYPAD_COL_1 = 32;
const uint8_t PIN_KEYPAD_COL_2 = 33;
const uint8_t PIN_KEYPAD_COL_3 = 25;

byte rowPins[ROWS] = {PIN_KEYPAD_ROW_1, PIN_KEYPAD_ROW_2, PIN_KEYPAD_ROW_3, PIN_KEYPAD_ROW_4};
byte colPins[COLS] = {PIN_KEYPAD_COL_1, PIN_KEYPAD_COL_2, PIN_KEYPAD_COL_3};

char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};

ESP32Encoder enc1;
ESP32Encoder enc2;
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Debounced + fault-aware wrappers for every button/switch
DebouncedInput inEnc1Sw, inEnc2Sw, inSel1, inSel2, inDcToggle, inEstop1, inEstop2;

// Encoders aren't digital edges, so they get their own glitch guard: a real
// hand turning a knob can't produce a huge count delta inside one 10ms loop
// iteration. If that happens repeatedly, treat the encoder as unreliable
// rather than sending the loco to full speed on a noise spike.
static const int64_t MAX_ENCODER_STEP_PER_LOOP = 30;
FaultTracker enc1Tracker, enc2Tracker;
static const unsigned long ENCODER_RECOVERY_QUIET_MS = 3000;

// Hold BTN_SELECT_E1 + BTN_SELECT_E2 together for this long to enter/exit
// debug/test mode -- see the "DEBUG MODE TOGGLE" section in loop() below.
static const unsigned long DEBUG_HOLD_MS = 1500;

void setup() {
  // Pin Modes Setup
  pinMode(PIN_ENCODER_1_SWITCH, INPUT_PULLUP);
  pinMode(PIN_ENCODER_2_SWITCH, INPUT_PULLUP);
  pinMode(PIN_BUTTON_SELECT_E1, INPUT_PULLUP);
  pinMode(PIN_BUTTON_SELECT_E2, INPUT_PULLUP);
  // GPIO34/35/36 are input-only pins with no internal pull resistor -- INPUT_PULLUP
  // here would silently do nothing. These three all need an external 10k pull-up
  // to 3.3V per the hardware notes.
  pinMode(PIN_SWITCH_DC_TOGGLE, INPUT);
  pinMode(PIN_BUTTON_ESTOP_E1, INPUT);
  pinMode(PIN_BUTTON_ESTOP_E2, INPUT);

  // Debounce + chatter/fault detection for every button and switch.
  // Tune debounceMs / chatterThreshold here if a particular switch is noisier
  // or bouncier than the defaults expect.
  inEnc1Sw.begin(PIN_ENCODER_1_SWITCH);
  inEnc2Sw.begin(PIN_ENCODER_2_SWITCH);
  inSel1.begin(PIN_BUTTON_SELECT_E1);
  inSel2.begin(PIN_BUTTON_SELECT_E2);
  inDcToggle.begin(PIN_SWITCH_DC_TOGGLE, 40); // slightly longer debounce: mechanical toggle switch
  inEstop1.begin(PIN_BUTTON_ESTOP_E1);
  inEstop2.begin(PIN_BUTTON_ESTOP_E2);

  initUI(); // Initialize LGFX OLED display first so we're not staring at a blank screen
  showConnectingScreen();

  initDCC(); // Connects over Wi-Fi to DCC-EX CSB1 Base Station (bounded wait, see dcc.cpp)

  // Hardware Encoder Setup
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  enc1.attachHalfQuad(PIN_ENCODER_1_A, PIN_ENCODER_1_B);
  enc2.attachHalfQuad(PIN_ENCODER_2_A, PIN_ENCODER_2_B);
  enc1.setCount(0);
  enc2.setCount(0);

  renderGUI();
}

void loop() {
  bool needs_redraw = false;
  unsigned long now = millis();

  // ==========================================
  // 0. WI-FI / CSB1 LINK MAINTENANCE
  // ==========================================
  // Non-blocking: only actually does work every few seconds if the link is down.
  static bool last_link_state = true;
  maintainDCCConnection();
  if (dccLinkUp != last_link_state) {
    last_link_state = dccLinkUp;
    needs_redraw = true; // refresh the link-status dot in the corner
  }

  // Update every debounced input once per loop, before we act on any of them
  inEnc1Sw.update();
  inEnc2Sw.update();
  inSel1.update();
  inSel2.update();
  inDcToggle.update();
  inEstop1.update();
  inEstop2.update();

  // ==========================================
  // DEBUG MODE TOGGLE (hold SEL1 + SEL2 together for 1.5s)
  // ==========================================
  // Same gesture enters and exits: hold both engine-select buttons for
  // DEBUG_HOLD_MS. Lets you verify every button, switch, and encoder
  // reports correctly on a freshly-built handheld without it being able to
  // send real throttle/function commands to the layout while you do it.
  static unsigned long bothSelectHeldSince = 0;
  static bool debugToggleArmed = true; // requires a full release before it can trigger again

  bool sel1Held = (inSel1.state() == LOW);
  bool sel2Held = (inSel2.state() == LOW);

  if (sel1Held && sel2Held) {
    if (bothSelectHeldSince == 0) bothSelectHeldSince = now;
    if (debugToggleArmed && (now - bothSelectHeldSince) >= DEBUG_HOLD_MS) {
      debugMode = !debugMode;
      debugToggleArmed = false;
      needs_redraw = true;

      // Always land in a known-safe state on the transition either way --
      // debug mode intentionally stops forwarding encoder/keypad input to
      // the layout, so make sure nothing was left rolling when that happened.
      for (uint8_t e = 0; e < 2; e++) {
        engines[e].speed = 0;
        updateLocoThrottle(e);
      }

      if (debugMode) {
        debugSnap.enc1Count = 0;
        debugSnap.enc2Count = 0;
        debugSnap.lastKey = '-';
        editing_loco_id = false;
        num_buffer[0] = '\0';
      }
    }
  } else {
    bothSelectHeldSince = 0;
    debugToggleArmed = true;
  }

  // ==========================================
  // 1. ROTARY ENCODERS (SPEED CONTROL, OR DEBUG CAPTURE)
  // ==========================================
  // Glitch guard: a real hand can't move the count by a huge amount inside
  // one 10ms loop. If that happens repeatedly, flag the encoder as faulty
  // and ignore its input rather than letting a noise spike jump the throttle.
  int64_t val1 = enc1.getCount();
  if (val1 != 0) {
    enc1.setCount(0);
    if (abs((int64_t)val1) > MAX_ENCODER_STEP_PER_LOOP) {
      noteFaultActivity(enc1Tracker, now, 3, 1000); // 3 big jumps within 1s = faulty
    } else if (!enc1Tracker.faulty) {
      if (debugMode) {
        debugSnap.enc1Count += val1; // display only -- not forwarded to the layout
      } else {
        engines[0].speed = constrain(engines[0].speed + (val1 * 2), 0, 126);
        updateLocoThrottle(0);
      }
    }
    needs_redraw = true;
  }
  updateFaultRecovery(enc1Tracker, now, ENCODER_RECOVERY_QUIET_MS);

  int64_t val2 = enc2.getCount();
  if (val2 != 0) {
    enc2.setCount(0);
    if (abs((int64_t)val2) > MAX_ENCODER_STEP_PER_LOOP) {
      noteFaultActivity(enc2Tracker, now, 3, 1000);
    } else if (!enc2Tracker.faulty) {
      if (debugMode) {
        debugSnap.enc2Count += val2;
      } else {
        engines[1].speed = constrain(engines[1].speed + (val2 * 2), 0, 126);
        updateLocoThrottle(1);
      }
    }
    needs_redraw = true;
  }
  updateFaultRecovery(enc2Tracker, now, ENCODER_RECOVERY_QUIET_MS);

  // ==========================================
  // 2. ENCODER PUSH BUTTONS (DIRECTION TOGGLES) -- suppressed in debug mode,
  //    raw switch state is shown directly on the debug screen instead
  // ==========================================
  if (!debugMode) {
    if (inEnc1Sw.pressedEdge()) {
      engines[0].direction_fwd = !engines[0].direction_fwd;
      updateLocoThrottle(0);
      needs_redraw = true;
    }
    if (inEnc2Sw.pressedEdge()) {
      engines[1].direction_fwd = !engines[1].direction_fwd;
      updateLocoThrottle(1);
      needs_redraw = true;
    }
  }

  // ==========================================
  // 3. TARGET FOCUS BUTTONS ([1] AND [2]) -- suppressed in debug mode
  // ==========================================
  if (!debugMode) {
    if (inSel1.pressedEdge()) {
      active_focus_idx = 0; // Route keypad commands to Engine 1
      editing_loco_id = false;
      num_buffer[0] = '\0';
      needs_redraw = true;
    }
    if (inSel2.pressedEdge()) {
      active_focus_idx = 1; // Route keypad commands to Engine 2
      editing_loco_id = false;
      num_buffer[0] = '\0';
      needs_redraw = true;
    }
  }

  // ==========================================
  // 4. PHYSICAL DC / DCC TOGGLE SWITCH -- suppressed in debug mode, raw
  //    switch state is shown directly on the debug screen instead
  // ==========================================
  // Edge-triggered on purpose: the switch applies to whichever engine is
  // focused at the *moment it is thrown*, and only that engine is restored
  // when it's thrown back -- so later pressing [1]/[2] to look at the other
  // engine can't accidentally drag it into DC mode too.
  if (!debugMode) {
    if (inDcToggle.pressedEdge()) {
      // Switch just thrown to DC position -> convert the currently focused engine
      dc_mode_owner_idx = active_focus_idx;
      saved_dcc_addr[dc_mode_owner_idx] = engines[dc_mode_owner_idx].address;
      engines[dc_mode_owner_idx].address = 0;
      engines[dc_mode_owner_idx].speed = 0;      // Safety stop on mode change
      engines[dc_mode_owner_idx].functions = 0;  // Reset functions
      updateLocoThrottle(dc_mode_owner_idx);
      editing_loco_id = false;
      num_buffer[0] = '\0';
      needs_redraw = true;
    }
    else if (inDcToggle.releasedEdge()) {
      // Switch just thrown back to DCC position -> restore whichever engine
      // actually got switched to DC, not whatever happens to be focused now
      if (dc_mode_owner_idx != -1) {
        engines[dc_mode_owner_idx].address = saved_dcc_addr[dc_mode_owner_idx];
        engines[dc_mode_owner_idx].speed = 0;      // Safety stop on mode change
        engines[dc_mode_owner_idx].functions = 0;
        updateLocoThrottle(dc_mode_owner_idx);
        dc_mode_owner_idx = -1;
      }
      editing_loco_id = false;
      num_buffer[0] = '\0';
      needs_redraw = true;
    }
  }

  /* ==========================================
     5. EMERGENCY STOP BUTTONS -- always live, even in debug mode. Debug
      mode is for testing that controls report correctly, not for
      disabling the one control that should never stop working.
     ==========================================
   */
  if (inEstop1.pressedEdge()) {
    sendEStop(0);
    needs_redraw = true;
  }
  if (inEstop2.pressedEdge()) {
    sendEStop(1);
    needs_redraw = true;
  }

  // ==========================================
  // 6. NUMPAD HANDLER (FUNCTIONS & ADDR EDIT, OR DEBUG CAPTURE)
  // ==========================================
  char key = keypad.getKey();
  if (key) {
    if (debugMode) {
      debugSnap.lastKey = key; // just record it -- no address/function side effects
    } else {
    if (key >= '0' && key <= '9') {
      size_t len = strlen(num_buffer);
      size_t max_len = editing_loco_id ? 4 : 2; // Up to 4 digits for address, 2 for functions
      if (len < max_len) {
        num_buffer[len] = key;
        num_buffer[len + 1] = '\0';
      }
    } 
    else if (key == '*') {
      // Toggle Address Re-programming Mode (Only if not in DC mode)
      if (engines[active_focus_idx].address != 0) {
        editing_loco_id = !editing_loco_id;
        num_buffer[0] = '\0';
      }
    } 
    else if (key == '#') {
      if (editing_loco_id) {
        // SAVE NEW LOCO ADDRESS
        if (strlen(num_buffer) > 0) {
          uint16_t new_addr = atoi(num_buffer);
          if (new_addr > 0) {
            engines[active_focus_idx].address = new_addr;
            saved_dcc_addr[active_focus_idx] = new_addr; // Update saved address memory
            engines[active_focus_idx].speed = 0;
            engines[active_focus_idx].functions = 0;
            updateLocoThrottle(active_focus_idx);
          }
        }
        editing_loco_id = false;
      } 
      else {
        // TRIGGER DCC SOUND/LIGHT FUNCTION (Disabled in DC mode)
        if (strlen(num_buffer) > 0 && engines[active_focus_idx].address != 0) {
          uint8_t fn = atoi(num_buffer);
          if (fn <= 28) {
            toggleDCCFunction(active_focus_idx, fn);
          }
        }
      }
      num_buffer[0] = '\0';
    }
    }
    needs_redraw = true;
  }

  // ==========================================
  // 7. FAULT FLAG AGGREGATION
  // ==========================================
  // Pull each control's live faulty() state into the shared struct the UI reads.
  // Redraw whenever anything changes so a fault (or its recovery) shows up
  // on screen right away rather than waiting for unrelated user input.
  bool fault_state_changed =
      (faults.estop1     != inEstop1.faulty())    ||
      (faults.estop2     != inEstop2.faulty())    ||
      (faults.sel1       != inSel1.faulty())      ||
      (faults.sel2       != inSel2.faulty())      ||
      (faults.dcToggle   != inDcToggle.faulty())  ||
      (faults.enc1Sw     != inEnc1Sw.faulty())    ||
      (faults.enc2Sw     != inEnc2Sw.faulty())    ||
      (faults.enc1Sensor != enc1Tracker.faulty)   ||
      (faults.enc2Sensor != enc2Tracker.faulty);

  faults.estop1     = inEstop1.faulty();
  faults.estop2     = inEstop2.faulty();
  faults.sel1       = inSel1.faulty();
  faults.sel2       = inSel2.faulty();
  faults.dcToggle   = inDcToggle.faulty();
  faults.enc1Sw     = inEnc1Sw.faulty();
  faults.enc2Sw     = inEnc2Sw.faulty();
  faults.enc1Sensor = enc1Tracker.faulty;
  faults.enc2Sensor = enc2Tracker.faulty;

  if (fault_state_changed) {
    needs_redraw = true;
  }

  // ==========================================
  // 8. UI SCREEN REDRAW
  // ==========================================
  if (needs_redraw) {
    renderGUI();
  }

  delay(10);
}