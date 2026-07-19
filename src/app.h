// Core state and beatmatch maths for openBPMcount.
// Display-agnostic: both the OLED and TFT front-ends render from this.
#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
static const uint8_t  MAX_INTERVALS   = 8;      // rolling window for averaging
static const uint32_t IDLE_RESET_MS   = 3000;   // gap that starts a fresh tap run
static const uint8_t  SOLID_TAPS      = 5;      // intervals before a BPM is "solid"
static const float    MATCH_TOL_BPM   = 0.10f;  // within this = MATCHED
static const float    PITCH_RANGE_PCT = 8.0f;   // typical turntable pitch range +/-
static const uint8_t  NUM_SLOTS       = 8;      // stored BPM slots
static const uint8_t  NAME_LEN        = 17;     // slot name incl. NUL

// ---------------------------------------------------------------------------
// One turntable's tap-tempo state
// ---------------------------------------------------------------------------
struct Deck {
  uint32_t intervals[MAX_INTERVALS];
  uint8_t  count;
  uint8_t  head;
  uint32_t lastTapMs;
  uint32_t tapCount;
  float    bpm;
  bool     locked;

  Deck();
  void     tap(uint32_t now);
  void     reset();
  uint32_t averageInterval() const;
  void     recompute();
  bool     solid() const;
  // Standard deviation of the tap intervals in ms; -1 when not enough data.
  // Low = you're tapping consistently, so trust the number.
  float    jitterMs() const;
  // True for a short window each time a predicted beat falls due, so the UI
  // can flash in time with the music (visual metronome).
  bool     beatPulse(uint32_t now, uint16_t widthMs = 90) const;
};

// ---------------------------------------------------------------------------
// Beatmatch result
// ---------------------------------------------------------------------------
struct MatchInfo {
  bool   valid;       // both decks measured
  float  target;      // effective target BPM after the octave choice
  float  pct;         // pitch % to dial into the active deck
  int8_t octave;      // 0 = same, +1 = target doubled, -1 = target halved
  bool   matched;     // within MATCH_TOL_BPM
  bool   outOfRange;  // needs more pitch than a turntable has
  float  driftSec;    // seconds until the decks drift one beat apart (<0 = n/a)

  MatchInfo();
};

// Pitch % to apply to `from` so it matches `to`: ((to/from) - 1) * 100
float     pitchPercent(float from, float to);
// Octave-aware: picks whichever of {other, other*2, other/2} needs least pitch.
MatchInfo computeMatch(float activeBpm, float otherBpm);

// ---------------------------------------------------------------------------
// Stored BPM library (persisted to NVS/flash)
// ---------------------------------------------------------------------------
struct Slot {
  float bpm;
  char  name[NAME_LEN];
};

namespace Lib {
  extern Slot slots[NUM_SLOTS];
  void begin();
  void store(uint8_t i, float bpm);         // 0 clears the slot
  void setName(uint8_t i, const char* name);
  void clear(uint8_t i);
}
