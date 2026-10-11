#include "ui.h"
#include "dcc.h"
#include "inputs.h"
#include <U8g2lib.h>
#include <SPI.h>

// ----------------------------------------------------------------------------
// Display driver
// ----------------------------------------------------------------------------
// Switched from LovyanGFX (which has no SSD1322 driver in any released
// version) to U8g2, which has an official, widely-used driver for exactly
// this 256x64 SSD1322 module.
//
// Wiring reference (SCK/MOSI are ESP32's default VSPI hardware pins, which is
// why they aren't passed to the constructor below -- U8g2's HW SPI mode uses
// the board's default SPI pins automatically):
//   OLED_SCK  -> GPIO18 (VSPI SCK)
//   OLED_MOSI -> GPIO23 (VSPI MOSI)
const uint8_t PIN_OLED_CHIP_SELECT = 5;
const uint8_t PIN_OLED_DATA_COMMAND = 16;
const uint8_t PIN_OLED_RESET = 17;

// "_F_" = full frame buffer (256x64 mono = 2KB RAM, fine on ESP32) rather than
// the page-buffer variants, so we can draw the whole UI then push it in one go
// with clearBuffer()/sendBuffer(), matching how the old sprite-based code worked.
U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ PIN_OLED_CHIP_SELECT, /* dc=*/ PIN_OLED_DATA_COMMAND, /* reset=*/ PIN_OLED_RESET);

// If HW SPI misbehaves on your specific board (a known occasional ESP32 quirk --
// see github.com/olikraus/u8g2/issues/934), swap the line above for this one
// and match the pins already wired for the display:
// U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI u8g2(U8G2_R0, /*clock=*/18, /*data=*/23, /*cs=*/PIN_OLED_CHIP_SELECT, /*dc=*/PIN_OLED_DATA_COMMAND, /*reset=*/PIN_OLED_RESET);

char num_buffer[6] = "";
bool editing_loco_id = false;
bool debugMode = false;
DebugSnapshot debugSnap;

// ----------------------------------------------------------------------------
// Layout constants
// ----------------------------------------------------------------------------
// 8px status strip (link + global faults) on top, two 28px engine rows below.
// 8 + 28 + 28 = 64, exactly the panel height.
static const int STATUS_STRIP_H = 8;
static const int ROW_H = 28;
static const int LINE1_BASELINE = 10; // offset from row top for address/direction/speed%
static const int LINE2_BASELINE = 24; // offset from row top for function list/fault text

static const uint8_t* FONT_NORMAL = u8g2_font_6x10_tf;
static const uint8_t* FONT_BIG_BOLD = u8g2_font_9x15B_tf; // used on the DC-mode screen
static const uint8_t* FONT_TINY = u8g2_font_5x7_tf;       // fits the 8px status strip

static char lineBuf[40];

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

// Returns a short fault description for engine i's own controls (estop,
// select button, encoder switch, encoder sensor), or nullptr if none.
// The DC toggle switch fault is global (not engine-specific) and is shown
// in the status strip instead, via a small icon.
static const char* engineFaultLabel(uint8_t i) {
  if (i == 0) {
    if (faults.estop1)     return "ESTOP1 FAULT - CHECK WIRING";
    if (faults.sel1)       return "SELECT-1 BTN FAULT";
    if (faults.enc1Sw)     return "ENC1 BUTTON FAULT";
    if (faults.enc1Sensor) return "ENC1 SENSOR NOISY - IGNORED";
  } else {
    if (faults.estop2)     return "ESTOP2 FAULT - CHECK WIRING";
    if (faults.sel2)       return "SELECT-2 BTN FAULT";
    if (faults.enc2Sw)     return "ENC2 BUTTON FAULT";
    if (faults.enc2Sensor) return "ENC2 SENSOR NOISY - IGNORED";
  }
  return nullptr;
}

static void drawRowFrame(int y, bool is_focused) {
  u8g2.drawFrame(0, y, 256, ROW_H - 1);
  if (is_focused) {
    // Display is monochrome, so focus is shown as a doubled/thicker border
    // rather than a highlight color.
    u8g2.drawFrame(1, y + 1, 254, ROW_H - 3);
  }
}

static void drawSpeedBar(int y, uint8_t speed) {
  const int bar_x = 115, bar_y = y + 4, bar_w = 85, bar_h = 8;
  float pct = speed / 126.0f;

  u8g2.drawFrame(bar_x, bar_y, bar_w, bar_h);
  if (pct > 0) {
    u8g2.drawBox(bar_x + 2, bar_y + 2, (int)((bar_w - 4) * pct), bar_h - 4);
  }

  u8g2.setFont(FONT_NORMAL);
  snprintf(lineBuf, sizeof(lineBuf), "%3d%%", (int)(pct * 100));
  u8g2.drawStr(205, y + LINE1_BASELINE, lineBuf);
}

static void drawDirection(int y, const EngineState &eng) {
  u8g2.setFont(FONT_NORMAL);
  u8g2.drawStr(65, y + LINE1_BASELINE, eng.direction_fwd ? "[FWD]" : "[REV]");
}

// Draws the fault/function-list second line. Fault text is shown as an
// inverted (filled-background) strip, since red isn't available on a
// monochrome display -- this is the closest equivalent "grab attention" cue.
static bool drawSecondLineFault(int i, int y) {
  const char* fault = engineFaultLabel(i);
  if (!fault) return false;

  u8g2.setDrawColor(1);
  u8g2.drawBox(1, y + LINE2_BASELINE - 9, 254, 11);
  u8g2.setDrawColor(0); // black text on the now-white strip
  u8g2.setFont(FONT_NORMAL);
  u8g2.drawStr(4, y + LINE2_BASELINE, fault);
  u8g2.setDrawColor(1);
  return true;
}

// Normal DCC-mode row: address/loco-ID entry, direction, speed, function list.
static void drawDccRow(int i, int y, bool is_focused) {
  const EngineState &eng = engines[i];

  u8g2.setFont(FONT_NORMAL);
  if (is_focused && editing_loco_id) {
    snprintf(lineBuf, sizeof(lineBuf), ">E%d:#%s_", i + 1, num_buffer);
  } else {
    snprintf(lineBuf, sizeof(lineBuf), "%sE%d:#%d", is_focused ? ">" : " ", i + 1, eng.address);
  }
  u8g2.drawStr(4, y + LINE1_BASELINE, lineBuf);

  drawDirection(y, eng);
  drawSpeedBar(y, eng.speed);

  if (drawSecondLineFault(i, y)) return; // fault banner takes priority over function list

  u8g2.setFont(FONT_NORMAL);
  if (is_focused && !editing_loco_id && strlen(num_buffer) > 0) {
    snprintf(lineBuf, sizeof(lineBuf), "F:[%s_]", num_buffer); // live typing buffer
  } else {
    char funcs[28] = "";
    int count = 0;
    for (int f = 0; f <= 28 && count < 8; f++) {
      if (eng.functions & (1UL << f)) {
        char tmp[4];
        snprintf(tmp, sizeof(tmp), "%d ", f);
        strncat(funcs, tmp, sizeof(funcs) - strlen(funcs) - 1);
        count++;
      }
    }
    snprintf(lineBuf, sizeof(lineBuf), "F:%s", count == 0 ? "NONE" : funcs);
  }
  u8g2.drawStr(4, y + LINE2_BASELINE, lineBuf);
}

// Status strip: link icon (right) and a small filled-square icon (left) for
// the DC-toggle-switch fault, since that's the one fault that isn't tied to
// a single engine's row. No text here -- an 8px strip is too short for
// readable text at any font size, so this is deliberately icon-only.
static void drawStatusStrip() {
  if (dccLinkUp) {
    u8g2.drawDisc(250, 4, 3);
  } else {
    u8g2.drawCircle(250, 4, 3);
  }

  if (faults.dcToggle) {
    u8g2.drawBox(2, 1, 5, 5); // see CONTROLS.md for what this icon means
  }

  if (!dccLinkUp) {
    // Throttle/function input is ignored while offline, so say so plainly.
    u8g2.setFont(FONT_TINY);
    u8g2.drawStr(12, 7, "NO LINK - CONTROLS NOT SENT");
  }
}

// ----------------------------------------------------------------------------
// DC mode screen: ONE DC loco, deliberately basic -- mode label, direction,
// a big speed bar and percentage. Encoder 1 = speed, its push = direction.
// ----------------------------------------------------------------------------
static const char* dcFaultLabel() {
  if (!dccLinkUp)        return "NO LINK - CONTROLS NOT SENT";
  if (faults.estop1)     return "ESTOP1 FAULT - CHECK WIRING";
  if (faults.estop2)     return "ESTOP2 FAULT - CHECK WIRING";
  if (faults.enc1Sw)     return "ENC1 BUTTON FAULT";
  if (faults.enc1Sensor) return "ENC1 SENSOR NOISY - IGNORED";
  if (faults.dcToggle)   return "DC SWITCH FAULT";
  return nullptr;
}

static void renderDcGUI() {
  u8g2.setFont(FONT_BIG_BOLD);
  u8g2.drawStr(4, 15, "DC MODE");
  u8g2.drawStr(150, 15, dcEngine.direction_fwd ? "[FWD]" : "[REV]");

  if (dccLinkUp) {
    u8g2.drawDisc(250, 6, 3);
  } else {
    u8g2.drawCircle(250, 6, 3);
  }

  const int bar_x = 4, bar_y = 24, bar_w = 200, bar_h = 16;
  float pct = dcEngine.speed / 126.0f;
  u8g2.drawFrame(bar_x, bar_y, bar_w, bar_h);
  if (dcEngine.speed > 0) {
    u8g2.drawBox(bar_x + 2, bar_y + 2, (int)((bar_w - 4) * pct), bar_h - 4);
  }
  snprintf(lineBuf, sizeof(lineBuf), "%3d%%", (int)(pct * 100));
  u8g2.drawStr(212, bar_y + 13, lineBuf);

  u8g2.setFont(FONT_NORMAL);
  const char* fault = dcFaultLabel();
  if (fault) {
    u8g2.setDrawColor(1);
    u8g2.drawBox(1, 44, 254, 11);
    u8g2.setDrawColor(0);
    u8g2.drawStr(4, 53, fault);
    u8g2.setDrawColor(1);
  } else {
    u8g2.drawStr(4, 52, "ENC1: SPEED / PUSH: DIRECTION");
  }
  u8g2.drawStr(4, 62, "THROW DC SWITCH TO RETURN TO DCC");
}

// ----------------------------------------------------------------------------
// Public entry points
// ----------------------------------------------------------------------------

void initUI() {
  u8g2.begin();
  u8g2.setFontMode(1);   // transparent glyph background (we clear the whole frame ourselves)
  u8g2.setDrawColor(1);
  u8g2.setBusClock(4000000);
  renderGUI();
}

// Call before initDCC() so the screen isn't blank during the Wi-Fi/CSB1 handshake.
void showConnectingScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(FONT_NORMAL);
  u8g2.drawStr(58, 36, "Connecting to CSB1...");
  u8g2.sendBuffer();
}

// ----------------------------------------------------------------------------
// Debug/test mode: live raw state of every button, switch, and encoder, plus
// the last keypad key seen. Entered by holding SEL1+SEL2 -- see handheld_main.ino.
// ----------------------------------------------------------------------------
static const char* dnUp(bool level) { return (level == LOW) ? "DN" : "UP"; }

static void appendFaultCode(char* buf, size_t bufSize, bool active, const char* code) {
  if (!active) return;
  strncat(buf, code, bufSize - strlen(buf) - 1);
}

static void renderDebugGUI() {
  u8g2.setFont(FONT_NORMAL);
  u8g2.drawStr(4, 9, "DEBUG MODE - HOLD SEL1+SEL2 TO EXIT");

  snprintf(lineBuf, sizeof(lineBuf), "ENC1 cnt=%-6ld  SW:%s", debugSnap.enc1Count, dnUp(inEnc1Sw.state()));
  u8g2.drawStr(4, 19, lineBuf);

  snprintf(lineBuf, sizeof(lineBuf), "ENC2 cnt=%-6ld  SW:%s", debugSnap.enc2Count, dnUp(inEnc2Sw.state()));
  u8g2.drawStr(4, 29, lineBuf);

  snprintf(lineBuf, sizeof(lineBuf), "SEL1:%s  SEL2:%s  DC:%s",
           dnUp(inSel1.state()), dnUp(inSel2.state()), dnUp(inDcToggle.state()));
  u8g2.drawStr(4, 39, lineBuf);

  snprintf(lineBuf, sizeof(lineBuf), "EST1:%s  EST2:%s  KEY:%c",
           dnUp(inEstop1.state()), dnUp(inEstop2.state()), debugSnap.lastKey);
  u8g2.drawStr(4, 49, lineBuf);

  char faultCodes[48] = "";
  appendFaultCode(faultCodes, sizeof(faultCodes), faults.estop1,     "EST1 ");
  appendFaultCode(faultCodes, sizeof(faultCodes), faults.estop2,     "EST2 ");
  appendFaultCode(faultCodes, sizeof(faultCodes), faults.sel1,       "SEL1 ");
  appendFaultCode(faultCodes, sizeof(faultCodes), faults.sel2,       "SEL2 ");
  appendFaultCode(faultCodes, sizeof(faultCodes), faults.dcToggle,   "DC ");
  appendFaultCode(faultCodes, sizeof(faultCodes), faults.enc1Sw,     "E1SW ");
  appendFaultCode(faultCodes, sizeof(faultCodes), faults.enc2Sw,     "E2SW ");
  appendFaultCode(faultCodes, sizeof(faultCodes), faults.enc1Sensor, "E1SN ");
  appendFaultCode(faultCodes, sizeof(faultCodes), faults.enc2Sensor, "E2SN ");

  snprintf(lineBuf, sizeof(lineBuf), "FLT: %s", faultCodes[0] ? faultCodes : "NONE");
  u8g2.drawStr(4, 59, lineBuf);
}

void renderGUI() {
  u8g2.clearBuffer();

  if (debugMode) {
    renderDebugGUI();
    u8g2.sendBuffer();
    return;
  }

  if (dcModeActive) {
    renderDcGUI();
    u8g2.sendBuffer();
    return;
  }

  drawStatusStrip();

  for (int i = 0; i < 2; i++) {
    int y = STATUS_STRIP_H + (i * ROW_H);
    bool is_focused = (active_focus_idx == i);

    drawRowFrame(y, is_focused);

    drawDccRow(i, y, is_focused);
  }

  u8g2.sendBuffer();
}