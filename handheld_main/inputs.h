#ifndef INPUTS_H
#define INPUTS_H

#include <Arduino.h>

// ============================================================================
// Fault-tracking primitive
// ============================================================================
// A "fault" here means: something is changing far more often than a human
// pressing a button or a healthy encoder could produce. That pattern is the
// classic signature of a broken/intermittent wire, a dead pull-up resistor,
// a failing switch, or electrical noise -- not real user input. We don't try
// to diagnose *why*, we just refuse to act on it and tell the user which
// control looks unwell.
struct FaultTracker {
  uint8_t changeCount = 0;
  unsigned long windowStartMs = 0;
  unsigned long lastActivityMs = 0;
  bool faulty = false;
};

// Call every time the raw signal does something suspicious (a bounce edge,
// or an encoder jump that's too large to be a real hand turning a knob).
// threshold/windowMs: e.g. "more than 8 changes within 500ms" = chatter.
// Returns the tracker's faulty state after this update.
bool noteFaultActivity(FaultTracker &t, unsigned long now, uint8_t threshold, unsigned long windowMs);

// Call every loop() regardless of activity, so a faulty tracker can clear
// itself again once the signal has been quiet for recoveryQuietMs.
void updateFaultRecovery(FaultTracker &t, unsigned long now, unsigned long recoveryQuietMs);

// ============================================================================
// DebouncedInput -- wraps one active-low digital pin (button or switch)
// ============================================================================
class DebouncedInput {
public:
  void begin(uint8_t pin,
             unsigned long debounceMs = 25,
             uint8_t chatterThreshold = 8,
             unsigned long chatterWindowMs = 500,
             unsigned long recoveryQuietMs = 3000);

  void update(); // call once per loop()

  bool pressedEdge()  const { return _pressedEdge; }  // true for one loop on HIGH->LOW
  bool releasedEdge() const { return _releasedEdge; }  // true for one loop on LOW->HIGH
  // True for one loop at the moment the input is first flagged faulty. Lets
  // safety-critical callers (E-stop) fail safe instead of going deaf.
  bool faultStartedEdge() const { return _faultStartedEdge; }
  bool state()  const { return _stable; }              // debounced level (LOW = active)
  bool faulty() const { return _tracker.faulty; }

private:
  uint8_t _pin = 0;
  bool _stable = HIGH;
  bool _lastRaw = HIGH;
  unsigned long _lastEdgeMs = 0;
  unsigned long _debounceMs = 25;
  uint8_t _chatterThreshold = 8;
  unsigned long _chatterWindowMs = 500;
  unsigned long _recoveryQuietMs = 3000;

  bool _pressedEdge = false;
  bool _releasedEdge = false;
  bool _faultStartedEdge = false;
  FaultTracker _tracker;
};

// ============================================================================
// System-wide fault flags, read by ui.cpp to draw on-screen warnings
// ============================================================================
struct SystemFaultState {
  bool estop1 = false;
  bool estop2 = false;
  bool sel1 = false;
  bool sel2 = false;
  bool dcToggle = false;
  bool enc1Sw = false;
  bool enc2Sw = false;
  bool enc1Sensor = false; // encoder producing implausible jumps
  bool enc2Sensor = false;

  bool any() const {
    return estop1 || estop2 || sel1 || sel2 || dcToggle ||
           enc1Sw || enc2Sw || enc1Sensor || enc2Sensor;
  }
  // True if anything wrong with controls belonging to engine idx (0 or 1)
  bool anyForEngine(uint8_t idx) const {
    if (idx == 0) return estop1 || sel1 || enc1Sw || enc1Sensor;
    return estop2 || sel2 || enc2Sw || enc2Sensor;
  }
};

extern SystemFaultState faults;

// The actual DebouncedInput objects for every physical button/switch are
// defined in the main .ino (that's where they're wired up and .update()d
// each loop). Declared here so other modules -- specifically the debug UI --
// can read live state without duplicating the wiring definitions.
extern DebouncedInput inEnc1Sw, inEnc2Sw, inSel1, inSel2, inDcToggle, inEstop1, inEstop2;

#endif
