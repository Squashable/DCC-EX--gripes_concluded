#ifndef DCC_H
#define DCC_H

#include <Arduino.h>

struct EngineState {
  uint16_t address;
  uint8_t  speed;         // 0 to 126 steps
  bool     direction_fwd; // true = FWD, false = REV
  uint32_t functions;     // Bitfield representing F0 through F28
};

extern EngineState engines[2];
extern uint8_t active_focus_idx;

// True once the initial Wi-Fi + CSB1 TCP handshake has succeeded at least once.
// Use this (rather than assuming success) if you want the UI to show a status icon.
extern bool dccLinkUp;

void initDCC();
void maintainDCCConnection(); // Call every loop() iteration; handles reconnects, is non-blocking
void updateLocoThrottle(uint8_t idx);
void toggleDCCFunction(uint8_t idx, uint8_t func_num);
void sendEStop(uint8_t idx);
void sendDCCCommand(const char* cmd);

#endif