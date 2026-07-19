#include "app.h"

#include <math.h>

// ---------------------------------------------------------------------------
// Deck
// ---------------------------------------------------------------------------
Deck::Deck() {
  reset();
  bpm = 0.0f;
  locked = false;
}

void Deck::reset() {
  for (uint8_t i = 0; i < MAX_INTERVALS; i++) intervals[i] = 0;
  count = 0;
  head = 0;
  lastTapMs = 0;
  tapCount = 0;
}

void Deck::tap(uint32_t now) {
  // A long silence means we've moved to a new record: start a fresh run.
  if (lastTapMs != 0 && (now - lastTapMs) > IDLE_RESET_MS) {
    count = 0;
    head = 0;
    tapCount = 0;
  }

  if (lastTapMs != 0) {
    uint32_t interval = now - lastTapMs;
    bool usable = interval > 100 && interval < 3000; // 20..600 BPM sanity band

    // Outlier rejection: a wildly off tap (a missed or double beat) restarts
    // the averaging window AND is itself discarded. Storing it would be worse
    // than useless — one fumbled tap at 120 BPM would otherwise become the
    // only sample in a fresh window and slam the readout to ~43 BPM.
    //
    // The previous bpm is deliberately left standing while the window refills,
    // so a single mis-tap doesn't make the display jump to nonsense. Two clean
    // taps re-establish the tempo, which is also what you want when you have
    // genuinely moved on to a record at a different speed.
    if (count > 0) {
      uint32_t avg = averageInterval();
      if (interval > avg * 2 || interval * 2 < avg) {
        count = 0;
        head = 0;
        usable = false;
      }
    }

    if (usable) {
      intervals[head] = interval;
      head = (head + 1) % MAX_INTERVALS;
      if (count < MAX_INTERVALS) count++;
      recompute();
    }
  }

  lastTapMs = now;
  tapCount++;
  locked = false; // a fresh tap unlocks and refines
}

uint32_t Deck::averageInterval() const {
  if (count == 0) return 0;
  uint64_t sum = 0;
  for (uint8_t i = 0; i < count; i++) sum += intervals[i];
  return (uint32_t)(sum / count);
}

void Deck::recompute() {
  uint32_t avg = averageInterval();
  bpm = (avg > 0) ? (60000.0f / (float)avg) : 0.0f;
}

bool Deck::solid() const {
  return count >= SOLID_TAPS;
}

float Deck::jitterMs() const {
  if (count < 2) return -1.0f;
  float mean = (float)averageInterval();
  float acc = 0.0f;
  for (uint8_t i = 0; i < count; i++) {
    float d = (float)intervals[i] - mean;
    acc += d * d;
  }
  return sqrtf(acc / (float)count);
}

bool Deck::beatPulse(uint32_t now, uint16_t widthMs) const {
  uint32_t iv = averageInterval();
  if (iv == 0 || lastTapMs == 0) return false;
  uint32_t phase = (now - lastTapMs) % iv;
  return phase < widthMs;
}

// ---------------------------------------------------------------------------
// Beatmatch maths
// ---------------------------------------------------------------------------
MatchInfo::MatchInfo()
  : valid(false), target(0.0f), pct(0.0f), octave(0),
    matched(false), outOfRange(false), driftSec(-1.0f) {}

float pitchPercent(float from, float to) {
  if (from <= 0.0f || to <= 0.0f) return 0.0f;
  return ((to / from) - 1.0f) * 100.0f;
}

MatchInfo computeMatch(float activeBpm, float otherBpm) {
  MatchInfo m;
  if (activeBpm <= 0.0f || otherBpm <= 0.0f) return m;

  const float  targets[3] = {otherBpm, otherBpm * 2.0f, otherBpm * 0.5f};
  const int8_t octs[3]    = {0, +1, -1};
  float bestAbs = 1e9f;
  for (uint8_t i = 0; i < 3; i++) {
    float p = pitchPercent(activeBpm, targets[i]);
    if (fabsf(p) < bestAbs) {
      bestAbs  = fabsf(p);
      m.target = targets[i];
      m.pct    = p;
      m.octave = octs[i];
    }
  }

  float diff   = fabsf(activeBpm - m.target);
  m.valid      = true;
  m.matched    = diff <= MATCH_TOL_BPM;
  m.outOfRange = fabsf(m.pct) > PITCH_RANGE_PCT;

  // How long until the two decks drift a full beat apart. If they differ by
  // d BPM, they gain one beat on each other every 60/d seconds — i.e. how
  // long you can leave it before nudging.
  m.driftSec = (diff > 0.001f) ? (60.0f / diff) : -1.0f;
  return m;
}
