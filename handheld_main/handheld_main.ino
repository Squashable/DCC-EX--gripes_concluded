#include <Arduino.h>
#include <ESP32Encoder.h>
#include <Keypad.h>

#include "dcc.h"
#include "ui.h"
#include "inputs.h"

// --- Hardware Pin Definitions ---
const uint8_t PIN_ENCODER_1_A = 19;
const uint8_t PIN_ENCODER_1_B = 21;
const uint8_t PIN_ENCODER_1_SWITCH = 22;  // Engine 1 Direction Toggle (also the DC-mode direction toggle)

const uint8_t PIN_ENCODER_2_A = 4;
const uint8_t PIN_ENCODER_2_B = 0;
const uint8_t PIN_ENCODER_2_SWITCH = 2;   // Engine 2 Direction Toggle

const uint8_t PIN_BUTTON_SELECT_E1 = 15;  // Route Numpad to Engine 1
const uint8_t PIN_BUTTON_SELECT_E2 = 26;  // Route Numpad to Engine 2

// NOTE: GPIO16 is used by the OLED's PIN_OLED_DATA_COMMAND line (see ui.cpp) -- it was
// previously double-booked with this switch, which would corrupt display
// output whenever the switch was toggled. Moved to GPIO36, an input-only
// pin with no internal pull resistor, same as the E-stop pins below.
const uint8_t PIN_SWITCH_DC_TOGGLE = 36;  // Physical SPST/SPDT Switch for DC / DCC Mode, LOW = DC (needs external 10k pull-up resistor)

const uint8_t PIN_BUTTON_ESTOP_E1 = 34;   // E-Stop Engine 1 (Needs external 10k pull-up resistor)
const uint8_t PIN_BUTTON_ESTOP_E2 = 35;   // E-Stop Engine 2 (Needs external 10k pull-up resistor)

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

// Reads how far an encoder has moved since the last call. Uses a running
// difference rather than getCount()+setCount(0), which could drop ticks that
// arrive between the two calls.
static int64_t lastEncCount[2] = {0, 0};
static int64_t readEncoderDelta(ESP32Encoder &enc, uint8_t i) {
  int64_t count = enc.getCount();
  int64_t delta = count - lastEncCount[i];
  lastEncCount[i] = count;
  return delta;
}

static void clearKeypadEntry() {
  editing_loco_id = false;
  programming_mode = false;
  num_buffer[0] = '\0';
}

// Select button for engine idx. First click selects that side (keypad now
// controls it). Clicking the already-selected side again starts typing a new
// address for it; clicking once more cancels.
static void handleSelect(uint8_t idx) {
  clearProgResult();
  if (active_focus_idx == idx && !editing_loco_id && !programming_mode) {
    editing_loco_id = true;
    num_buffer[0] = '\0';
  } else {
    active_focus_idx = idx;
    clearKeypadEntry();
  }
}

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

  initUI(); // Initialize the OLED display first so we're not staring at a blank screen
  showConnectingScreen();

  initDCC(); // Connects over Wi-Fi to DCC-EX CSB1 Base Station (bounded wait, see dcc.cpp)

  // Hardware Encoder Setup
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  enc1.attachHalfQuad(PIN_ENCODER_1_A, PIN_ENCODER_1_B);
  enc2.attachHalfQuad(PIN_ENCODER_2_A, PIN_ENCODER_2_B);
  enc1.setCount(0);
  enc2.setCount(0);
  lastEncCount[0] = 0;
  lastEncCount[1] = 0;

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
    if (dccLinkUp) resyncAfterReconnect(); // link just came back
    last_link_state = dccLinkUp;
    needs_redraw = true; // refresh the link-status indicator
  }

  // While the link is down, throttle / direction / function input is ignored so
  // the screen always shows what the layout is actually doing (the loco keeps
  // running at its last commanded speed). E-stop is still evaluated below, but
  // can't reach the layout until the link returns -- the UI shows a NO LINK banner.
  const bool linkOk = dccLinkUp;

  // Programming results arrive from the CSB1 (or time out) on their own.
  static ProgState last_prog_state = PROG_IDLE;
  if (progState != last_prog_state) {
    last_prog_state = progState;
    needs_redraw = true;
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
      stopAllThrottles();

      if (debugMode) {
        debugSnap.enc1Count = 0;
        debugSnap.enc2Count = 0;
        debugSnap.lastKey = '-';
        clearKeypadEntry();
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
  //
  // DC mode: encoder 1 is the single throttle, encoder 2 is unused.
  int64_t val1 = readEncoderDelta(enc1, 0);
  if (val1 != 0) {
    if (abs(val1) > MAX_ENCODER_STEP_PER_LOOP) {
      noteFaultActivity(enc1Tracker, now, 3, 1000); // 3 big jumps within 1s = faulty
    } else if (!enc1Tracker.faulty) {
      if (debugMode) {
        debugSnap.enc1Count += val1; // display only -- not forwarded to the layout
      } else if (linkOk) {
        if (dcModeActive) {
          dcEngine.speed = constrain(dcEngine.speed + (val1 * 2), 0, 126);
          updateDCThrottle();
        } else {
          engines[0].speed = constrain(engines[0].speed + (val1 * 2), 0, 126);
          updateLocoThrottle(0);
        }
      }
    }
    needs_redraw = true;
  }
  updateFaultRecovery(enc1Tracker, now, ENCODER_RECOVERY_QUIET_MS);

  int64_t val2 = readEncoderDelta(enc2, 1);
  if (val2 != 0) {
    if (abs(val2) > MAX_ENCODER_STEP_PER_LOOP) {
      noteFaultActivity(enc2Tracker, now, 3, 1000);
    } else if (!enc2Tracker.faulty) {
      if (debugMode) {
        debugSnap.enc2Count += val2;
      } else if (linkOk && !dcModeActive) {
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
  if (!debugMode && linkOk) {
    if (inEnc1Sw.pressedEdge()) {
      if (dcModeActive) {
        dcEngine.direction_fwd = !dcEngine.direction_fwd;
        updateDCThrottle();
      } else {
        engines[0].direction_fwd = !engines[0].direction_fwd;
        updateLocoThrottle(0);
      }
      needs_redraw = true;
    }
    if (inEnc2Sw.pressedEdge() && !dcModeActive) {
      engines[1].direction_fwd = !engines[1].direction_fwd;
      updateLocoThrottle(1);
      needs_redraw = true;
    }
  }

  // ==========================================
  // 3. TARGET FOCUS BUTTONS ([1] AND [2]) -- suppressed in debug mode and DC mode
  // ==========================================
  if (!debugMode && !dcModeActive) {
    if (inSel1.pressedEdge()) {
      handleSelect(0); // Engine 1: select it, or (if already selected) edit its address
      needs_redraw = true;
    }
    if (inSel2.pressedEdge()) {
      handleSelect(1); // Engine 2
      needs_redraw = true;
    }
  }

  // ==========================================
  // 4. PHYSICAL DC / DCC TOGGLE SWITCH -- suppressed in debug mode
  // ==========================================
  // Level-driven rather than edge-driven: the handheld's mode always follows
  // the switch position, so a switch that was already in the DC position at
  // power-up (or that moved while the input was flagged faulty) can't leave
  // the handheld disagreeing with the switch.
  //
  // DC mode = ONE DC loco on encoder 1 with a basic screen. The two DCC
  // engines are stopped on entry and left untouched until the switch is
  // thrown back.
  if (!debugMode) {
    bool wantDC = (inDcToggle.state() == LOW);
    if (wantDC && !dcModeActive) {
      enterDCMode();
      clearKeypadEntry();
      needs_redraw = true;
    } else if (!wantDC && dcModeActive) {
      exitDCMode();
      clearKeypadEntry();
      needs_redraw = true;
    }
  }

  /* ==========================================
     5. EMERGENCY STOP BUTTONS -- always live, even in debug mode. Debug
      mode is for testing that controls report correctly, not for
      disabling the one control that should never stop working.

      Fail-safe: an E-stop that starts chattering (faultStartedEdge) is
      treated as a press rather than ignored, so a failing button stops the
      loco instead of silently doing nothing. In DC mode either button stops
      the single DC loco.
     ==========================================
   */
  if (inEstop1.pressedEdge() || inEstop1.faultStartedEdge() || inEstop1.faultPressEdge()) {
    if (dcModeActive) sendDCEStop(); else sendEStop(0);
    needs_redraw = true;
  }
  if (inEstop2.pressedEdge() || inEstop2.faultStartedEdge() || inEstop2.faultPressEdge()) {
    if (dcModeActive) sendDCEStop(); else sendEStop(1);
    needs_redraw = true;
  }

  // ==========================================
  // 6. NUMPAD HANDLER (FUNCTIONS & ADDR EDIT, OR DEBUG CAPTURE)
  // ==========================================
  // Always read the key so it can't queue up; ignored in DC mode (no
  // addresses or functions on plain DC track).
  char key = keypad.getKey();
  if (key) {
    if (debugMode) {
      debugSnap.lastKey = key; // just record it -- no address/function side effects
    } else if (!dcModeActive) {
      clearProgResult(); // any key dismisses a programming result

      if (key >= '0' && key <= '9') {
        size_t len = strlen(num_buffer);
        size_t max_len = (editing_loco_id || programming_mode) ? 4 : 2; // 4 digits for an address, 2 for a function
        if (len < max_len) {
          num_buffer[len] = key;
          num_buffer[len + 1] = '\0';
        }
      }
      else if (key == '*') {
        // Toggle decoder programming: type a new address, then # writes it
        // into the loco on the programming track.
        if (programming_mode) {
          programming_mode = false;
        } else {
          editing_loco_id = false;
          programming_mode = true;
        }
        num_buffer[0] = '\0';
      }
      else if (key == '#') {
        if (editing_loco_id) {
          // SAVE NEW HANDHELD ADDRESS for the selected engine
          if (strlen(num_buffer) > 0) {
            uint16_t new_addr = atoi(num_buffer);
            if (new_addr > 0) {
              // Stops the loco at the OLD address first, then switches
              changeEngineAddress(active_focus_idx, new_addr);
            }
          }
          editing_loco_id = false;
        }
        else if (programming_mode) {
          // WRITE NEW ADDRESS INTO THE LOCO DECODER (loco must be on the PROG track)
          if (strlen(num_buffer) > 0 && !programmingBusy()) {
            uint16_t new_addr = atoi(num_buffer);
            if (new_addr > 0) {
              startProgramAddress(new_addr);
            }
          }
          programming_mode = false;
        }
        else {
          // TRIGGER DCC SOUND/LIGHT FUNCTION (ignored while the link is down so
          // the on-screen function list can't drift from the decoder)
          if (strlen(num_buffer) > 0 && linkOk) {
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
