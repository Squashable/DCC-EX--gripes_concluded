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

// True while the Wi-Fi link AND the TCP socket to the CSB1 are both up.
extern bool dccLinkUp;

// --- DC mode -----------------------------------------------------------
// When the DC/DCC switch is thrown to DC, the handheld stops using the two
// DCC engines and controls ONE DC loco instead (single throttle, basic UI).
// dcEngine.address is the DCC-EX "cab id" assigned to the DC track; the
// track letter and cab id are set by DC_TRACK_LETTER / DC_CAB_ID in dcc.cpp.
extern bool dcModeActive;
extern EngineState dcEngine;

// --- Decoder address programming (programming track) ---------------------
// Writes the loco address stored inside the decoder using <W addr>, then
// confirms it with <R> (reply <r addr>). The loco must be on the CSB1's PROG
// track. Result is reported through progState/progAddr for the UI to show.
enum ProgState : uint8_t { PROG_IDLE, PROG_WRITING, PROG_VERIFYING, PROG_OK, PROG_FAIL };
extern ProgState progState;
extern uint16_t progAddr;       // address being / last programmed
bool programmingBusy();         // true while a write/verify is in flight
void startProgramAddress(uint16_t addr);
void clearProgResult();         // dismiss an OK/FAIL result

void initDCC();
void maintainDCCConnection(); // Call every loop() iteration; handles reconnects, is non-blocking
void resyncAfterReconnect();  // Call once when the link comes back up

// Returns true if the command was actually written to the socket, false if the link is down.
bool sendDCCCommand(const char* cmd);

void updateLocoThrottle(uint8_t idx);

// Stops the loco at engine idx's current address, then points the engine at
// newAddr (speed 0, functions cleared). If the link is down the stop for the
// old address is remembered and sent when the link returns.
void changeEngineAddress(uint8_t idx, uint16_t newAddr);
void toggleDCCFunction(uint8_t idx, uint8_t func_num);
void sendEStop(uint8_t idx);

void enterDCMode();
void exitDCMode();
void updateDCThrottle();
void sendDCEStop();

// Zero the speed of whatever is currently being driven (DCC engines or the DC loco).
void stopAllThrottles();

#endif
