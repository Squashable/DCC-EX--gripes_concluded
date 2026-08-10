#include "inputs.h"

SystemFaultState faults;

bool noteFaultActivity(FaultTracker &t, unsigned long now, uint8_t threshold, unsigned long windowMs) {
  t.lastActivityMs = now;

  if (now - t.windowStartMs > windowMs) {
    // Window expired -- start counting fresh
    t.windowStartMs = now;
    t.changeCount = 1;
  } else {
    t.changeCount++;
  }

  if (t.changeCount >= threshold) {
    t.faulty = true;
  }
  return t.faulty;
}

void updateFaultRecovery(FaultTracker &t, unsigned long now, unsigned long recoveryQuietMs) {
  if (t.faulty && (now - t.lastActivityMs) >= recoveryQuietMs) {
    t.faulty = false;
    t.changeCount = 0;
  }
}

void DebouncedInput::begin(uint8_t pin, unsigned long debounceMs, uint8_t chatterThreshold,
                            unsigned long chatterWindowMs, unsigned long recoveryQuietMs) {
  _pin = pin;
  _debounceMs = debounceMs;
  _chatterThreshold = chatterThreshold;
  _chatterWindowMs = chatterWindowMs;
  _recoveryQuietMs = recoveryQuietMs;
  _stable = digitalRead(_pin);
  _lastRaw = _stable;
  _lastEdgeMs = millis();
}

void DebouncedInput::update() {
  _pressedEdge = false;
  _releasedEdge = false;

  unsigned long now = millis();
  bool raw = digitalRead(_pin);

  if (raw != _lastRaw) {
    // Raw pin just moved -- this counts as "activity" for fault detection
    // regardless of whether it survives debouncing, since a healthy button
    // shouldn't flicker rapidly even briefly.
    noteFaultActivity(_tracker, now, _chatterThreshold, _chatterWindowMs);
    _lastRaw = raw;
    _lastEdgeMs = now;
  }

  updateFaultRecovery(_tracker, now, _recoveryQuietMs);

  if (_tracker.faulty) {
    // Don't act on a control we believe is malfunctioning. Freeze at last
    // known-good stable state until the line settles down again.
    return;
  }

  if (raw != _stable && (now - _lastEdgeMs) >= _debounceMs) {
    _stable = raw;
    if (_stable == LOW) _pressedEdge = true;
    else _releasedEdge = true;
  }
}
