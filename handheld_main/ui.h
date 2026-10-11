#ifndef UI_H
#define UI_H

#include <Arduino.h>

extern char num_buffer[6];
extern bool editing_loco_id;   // typing a new handheld address for the selected engine
extern bool programming_mode;  // typing a new address to write into the loco's decoder

// Debug/test mode: hold BTN_SELECT_E1 + BTN_SELECT_E2 together for 1.5s to
// toggle. While active, encoders/keypad/DC-switch are captured for display
// only and are NOT forwarded to the layout (E-stop still works normally).
extern bool debugMode;

struct DebugSnapshot {
  long enc1Count = 0; // cumulative raw encoder counts since debug mode was entered
  long enc2Count = 0;
  char lastKey = '-'; // last keypad key seen while in debug mode, '-' = none yet
};
extern DebugSnapshot debugSnap;

void initUI();
void showConnectingScreen();
void renderGUI();

#endif