// openBPMcount — tap-tempo BPM counter & beatmatch assistant for vinyl DJing
// Target: ideaspark ESP32-WROOM-32 + integrated 0.96" SSD1306 OLED (128x64, I2C)
//
// One onboard button (BOOT, GPIO0), three gestures:
//   SHORT press  (<0.6s)
//   LONG  press  (0.6s .. 1.6s)
//   HOLD         (>=1.6s)
//
// MATCH mode (live beatmatching, two decks A & B):
//   SHORT -> tap a beat for the ACTIVE deck
//   LONG  -> lock the active deck and switch to the other deck (A <-> B)
//   HOLD  -> go to LIBRARY mode
//
// LIBRARY mode (stored BPM slots A/B/C, kept in flash for crate digging):
//   SHORT -> move the cursor to the next slot
//   LONG  -> store the active deck's current BPM into the cursor slot
//            (or clear the slot if there's no live reading)
//   HOLD  -> return to MATCH mode
//
// The match readout is octave-aware: a record tapped at half/double time still
// matches, tagged x2 or 1/2, and pitch beyond a turntable's range is flagged.

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static const uint8_t  PIN_BUTTON      = 0;      // onboard BOOT button (active LOW)
static const uint8_t  OLED_SDA        = 21;     // ideaspark integrated OLED
static const uint8_t  OLED_SCL        = 22;

static const uint16_t DEBOUNCE_MS     = 25;     // contact debounce
static const uint16_t LONGPRESS_MS    = 600;    // short -> long threshold
static const uint16_t HOLD_MS         = 1600;   // long  -> hold threshold
static const uint32_t IDLE_RESET_MS   = 3000;   // gap that starts a fresh tap run
static const uint32_t SLEEP_MS        = 120000; // blank the OLED after this idle
static const uint8_t  MAX_INTERVALS   = 8;      // rolling window for averaging
static const uint8_t  LOCK_TAPS       = 6;      // taps before the BPM reads "solid"
static const float    MATCH_TOL_BPM   = 0.10f;  // within this = "MATCHED"
static const float    PITCH_RANGE_PCT = 8.0f;   // typical turntable pitch range +/-

static const uint8_t  NUM_SLOTS       = 3;      // memory slots A / B / C

// Full-frame-buffer SSD1306 on hardware I2C. Reset pin not wired on this board.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

Preferences prefs;

// ---------------------------------------------------------------------------
// BPM engine — one instance per deck
// ---------------------------------------------------------------------------
struct Deck {
  uint32_t intervals[MAX_INTERVALS] = {0};
  uint8_t  count      = 0;      // number of valid intervals stored
  uint8_t  head       = 0;      // ring-buffer write position
  uint32_t lastTapMs  = 0;      // millis() of the most recent tap
  uint32_t tapCount   = 0;      // total taps in the current run
  float    bpm        = 0.0f;   // current averaged BPM (0 = not measured)
  bool     locked     = false;  // frozen by a long-press

  // Register a beat.
  void tap(uint32_t now) {
    // A long silence means we've moved to a new record: start a fresh run.
    if (lastTapMs != 0 && (now - lastTapMs) > IDLE_RESET_MS) {
      count = 0;
      head = 0;
      tapCount = 0;
    }

    if (lastTapMs != 0) {
      uint32_t interval = now - lastTapMs;

      // Outlier rejection: a wildly off tap (missed/double beat) restarts the
      // window instead of poisoning the average.
      if (count > 0) {
        uint32_t avg = averageInterval();
        if (interval > avg * 2 || interval * 2 < avg) {
          count = 0;
          head = 0;
        }
      }

      if (interval > 100 && interval < 3000) { // 20..600 BPM sanity band
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

  uint32_t averageInterval() const {
    if (count == 0) return 0;
    uint64_t sum = 0;
    for (uint8_t i = 0; i < count; i++) sum += intervals[i];
    return (uint32_t)(sum / count);
  }

  void recompute() {
    uint32_t avg = averageInterval();
    bpm = (avg > 0) ? (60000.0f / (float)avg) : 0.0f;
  }

  bool solid() const { return count >= (LOCK_TAPS - 1); }
};

Deck deckA;
Deck deckB;
Deck* active = &deckA;   // deck the taps currently drive

// ---------------------------------------------------------------------------
// Memory slots (persisted to NVS/flash)
// ---------------------------------------------------------------------------
float   slotBpm[NUM_SLOTS] = {0};
uint8_t cursorSlot         = 0;

void loadSlots() {
  prefs.begin("openbpm", false);
  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    char key[4];
    snprintf(key, sizeof(key), "s%u", i);
    slotBpm[i] = prefs.getFloat(key, 0.0f);
  }
}

void saveSlot(uint8_t i) {
  char key[4];
  snprintf(key, sizeof(key), "s%u", i);
  prefs.putFloat(key, slotBpm[i]);
}

// ---------------------------------------------------------------------------
// Button handling — debounce + short / long / hold detection
// ---------------------------------------------------------------------------
enum ButtonEvent { BTN_NONE, BTN_SHORT, BTN_LONG, BTN_HOLD };

bool     btnStable    = false;   // debounced logical state (true = pressed)
bool     btnRaw       = false;
uint32_t btnChangedMs = 0;
uint32_t btnDownMs    = 0;
bool     holdFired    = false;   // hold already emitted for this press

ButtonEvent pollButton() {
  ButtonEvent ev = BTN_NONE;
  uint32_t now = millis();

  bool reading = (digitalRead(PIN_BUTTON) == LOW); // active LOW
  if (reading != btnRaw) {
    btnRaw = reading;
    btnChangedMs = now;
  }

  if ((now - btnChangedMs) >= DEBOUNCE_MS && reading != btnStable) {
    btnStable = reading;
    if (btnStable) {                 // press begins
      btnDownMs = now;
      holdFired = false;
    } else if (!holdFired) {         // release -> short or long
      uint32_t held = now - btnDownMs;
      ev = (held >= LONGPRESS_MS) ? BTN_LONG : BTN_SHORT;
    }
  }

  // Fire HOLD while still held so it feels responsive; suppress the release.
  if (btnStable && !holdFired && (now - btnDownMs) >= HOLD_MS) {
    holdFired = true;
    ev = BTN_HOLD;
  }
  return ev;
}

// ---------------------------------------------------------------------------
// Beatmatch math
// ---------------------------------------------------------------------------
// Pitch % to apply to `from` so it matches `to`:  ((to/from) - 1) * 100
float pitchPercent(float from, float to) {
  if (from <= 0.0f || to <= 0.0f) return 0.0f;
  return ((to / from) - 1.0f) * 100.0f;
}

// Octave-aware match: pick whichever of {other, other*2, other/2} needs the
// least pitch change, so half/double-time records still line up.
struct Match {
  float target;   // effective target BPM after octave choice
  float pct;      // pitch % to apply to the active deck
  int8_t octave;  // 0 = same, +1 = target doubled, -1 = target halved
  bool  valid;
};

Match bestMatch(float activeBpm, float otherBpm) {
  Match m{otherBpm, 0.0f, 0, false};
  if (activeBpm <= 0.0f || otherBpm <= 0.0f) return m;

  const float targets[3] = {otherBpm, otherBpm * 2.0f, otherBpm * 0.5f};
  const int8_t octs[3]   = {0, +1, -1};
  float bestAbs = 1e9f;
  for (uint8_t i = 0; i < 3; i++) {
    float p = pitchPercent(activeBpm, targets[i]);
    if (fabsf(p) < bestAbs) {
      bestAbs = fabsf(p);
      m.target = targets[i];
      m.pct    = p;
      m.octave = octs[i];
    }
  }
  m.valid = true;
  return m;
}

// ---------------------------------------------------------------------------
// UI state
// ---------------------------------------------------------------------------
enum Mode { MODE_MATCH, MODE_LIBRARY };
Mode     mode           = MODE_MATCH;
uint32_t lastTapFlashMs = 0;   // brief visual pulse on each tap
uint32_t lastActivityMs = 0;   // any button event; drives OLED sleep
bool     asleep         = false;

void formatBpm(char* buf, size_t n, float bpm) {
  if (bpm <= 0.0f) snprintf(buf, n, "---.-");
  else             snprintf(buf, n, "%.1f", bpm);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void drawMatch() {
  const bool activeIsA = (active == &deckA);
  Deck& other = activeIsA ? deckB : deckA;

  char big[8];
  formatBpm(big, sizeof(big), active->bpm);

  // Header: active deck + tap count / lock state.
  u8g2.setFont(u8g2_font_6x12_tf);
  char hdr[16];
  snprintf(hdr, sizeof(hdr), "DECK %c", activeIsA ? 'A' : 'B');
  u8g2.drawStr(0, 10, hdr);
  if (active->locked) {
    u8g2.drawStr(80, 10, "LOCK");
  } else {
    char taps[12];
    snprintf(taps, sizeof(taps), "TAPS %lu", (unsigned long)active->tapCount);
    u8g2.drawStr(74, 10, taps);
  }
  if (millis() - lastTapFlashMs < 90) u8g2.drawBox(124, 0, 4, 4);

  // Big BPM number.
  u8g2.setFont(u8g2_font_fub25_tn);
  uint8_t w = u8g2.getStrWidth(big);
  u8g2.drawStr((128 - w) / 2, 42, big);

  // Footer line 1: both decks.
  u8g2.drawHLine(0, 47, 128);
  u8g2.setFont(u8g2_font_6x12_tf);
  char a[8], b[8];
  formatBpm(a, sizeof(a), deckA.bpm);
  formatBpm(b, sizeof(b), deckB.bpm);
  char foot[26];
  snprintf(foot, sizeof(foot), "A:%s B:%s", a, b);
  u8g2.drawStr(0, 58, foot);

  // Footer line 2: guidance.
  u8g2.setFont(u8g2_font_6x10_tf);
  char line[26];
  if (active->bpm > 0.0f && other.bpm > 0.0f) {
    Match m = bestMatch(active->bpm, other.bpm);
    const char* oct = (m.octave > 0) ? " x2" : (m.octave < 0) ? " 1/2" : "";
    const char* nudge;
    if (fabsf(active->bpm - m.target) <= MATCH_TOL_BPM) nudge = "MATCH   ";
    else if (m.target > active->bpm)                    nudge = "SPEED UP";
    else                                                nudge = "SLOW DN ";
    if (fabsf(m.pct) > PITCH_RANGE_PCT) {
      snprintf(line, sizeof(line), "!RANGE %+.1f%%%s", m.pct, oct);
    } else {
      snprintf(line, sizeof(line), "%s%+.1f%%%s", nudge, m.pct, oct);
    }
  } else if (active->bpm > 0.0f) {
    // Only one deck measured: help identify half/double-time records.
    snprintf(line, sizeof(line), "x2 %.0f  half %.1f",
             active->bpm * 2.0f, active->bpm * 0.5f);
  } else {
    snprintf(line, sizeof(line), "tap the beat...");
  }
  u8g2.drawStr(0, 64, line);
}

void drawLibrary() {
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 10, "LIBRARY");
  char live[16];
  formatBpm(live, sizeof(live), active->bpm);
  char liveStr[20];
  snprintf(liveStr, sizeof(liveStr), "live %s", live);
  u8g2.drawStr(128 - u8g2.getStrWidth(liveStr), 10, liveStr);
  u8g2.drawHLine(0, 13, 128);

  // Three slot rows.
  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    uint8_t y = 27 + i * 12;
    char sv[8];
    formatBpm(sv, sizeof(sv), slotBpm[i]);
    char row[16];
    snprintf(row, sizeof(row), "%c: %s", 'A' + i, sv);
    if (i == cursorSlot) {
      u8g2.drawBox(0, y - 10, 128, 12);
      u8g2.setDrawColor(0);
      u8g2.drawStr(4, y, row);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(4, y, row);
    }
  }

  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(0, 64, "SHORT next  LONG store");
}

void draw() {
  u8g2.clearBuffer();
  if (mode == MODE_MATCH) drawMatch();
  else                    drawLibrary();
  u8g2.sendBuffer();
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.setI2CAddress(0x3C << 1);
  u8g2.begin();

  loadSlots();

  // Splash
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(6, 26, "openBPMcount");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(2, 44, "tap the beat...");
  u8g2.sendBuffer();
  delay(1200);

  lastActivityMs = millis();
}

void loop() {
  ButtonEvent ev = pollButton();
  uint32_t now = millis();

  if (ev != BTN_NONE) {
    lastActivityMs = now;
    // If the screen was asleep, the wake press only wakes it — don't act on it.
    if (asleep) {
      asleep = false;
      u8g2.setPowerSave(0);
      ev = BTN_NONE;
    }
  }

  if (mode == MODE_MATCH) {
    if (ev == BTN_SHORT) {
      active->tap(now);
      lastTapFlashMs = now;
    } else if (ev == BTN_LONG) {
      if (active->bpm > 0.0f) active->locked = true;
      active = (active == &deckA) ? &deckB : &deckA;
    } else if (ev == BTN_HOLD) {
      mode = MODE_LIBRARY;
    }
  } else { // MODE_LIBRARY
    if (ev == BTN_SHORT) {
      cursorSlot = (cursorSlot + 1) % NUM_SLOTS;
    } else if (ev == BTN_LONG) {
      slotBpm[cursorSlot] = active->bpm; // 0 if no live reading -> clears slot
      saveSlot(cursorSlot);
    } else if (ev == BTN_HOLD) {
      mode = MODE_MATCH;
    }
  }

  // OLED sleep after inactivity (battery + burn-in protection).
  if (!asleep && (now - lastActivityMs) > SLEEP_MS) {
    asleep = true;
    u8g2.setPowerSave(1);
  }

  if (!asleep) draw();
  delay(5); // ~200 Hz UI/button loop, plenty responsive for tapping
}
