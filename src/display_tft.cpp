// 1.14" ST7789 240x135 colour front-end (ideaspark TFT board).
// Built only in the `tft` PlatformIO environment.
//
// Drawn into an off-screen sprite and blitted in one go, so the readout never
// flickers while you're tapping. Colour carries the match state: you can read
// green/amber/red across a dark booth far faster than you can read digits.
#include "display.h"

#include <TFT_eSPI.h>
#include <math.h>

static const uint8_t PIN_BL = 32;   // backlight (ideaspark 1.14" board)

static const int16_t W = 240;
static const int16_t H = 135;

static TFT_eSPI    tft = TFT_eSPI();
static TFT_eSprite fb  = TFT_eSprite(&tft);
static bool        fbReady = false;

UiState::UiState()
  : mode(MODE_MATCH), bpmA(0), bpmB(0), activeIsA(true), activeBpm(0),
    tapCount(0), locked(false), solid(false), jitterMs(-1), beatPulse(false),
    tapFlash(false), cursor(0), wifiOn(false), wifiSsid(""), wifiPass(""),
    wifiIp("") {}

// ---------------------------------------------------------------------------
static void fmtBpm(char* buf, size_t n, float bpm) {
  if (bpm <= 0.0f) snprintf(buf, n, "--.-");
  else             snprintf(buf, n, "%.1f", bpm);
}

static void fmtDrift(char* buf, size_t n, float sec) {
  if (sec < 0)        snprintf(buf, n, "--");
  else if (sec > 599) snprintf(buf, n, "10m+");
  else if (sec >= 60) snprintf(buf, n, "%dm%02ds", (int)(sec / 60), ((int)sec) % 60);
  else                snprintf(buf, n, "%ds", (int)sec);
}

// Green = matched, amber = adjustable, red = beyond the pitch fader.
static uint16_t matchColour(const MatchInfo& m) {
  if (!m.valid)      return TFT_WHITE;
  if (m.outOfRange)  return TFT_RED;
  if (m.matched)     return TFT_GREEN;
  return TFT_ORANGE;
}

static void drawPitchBar(const UiState& s) {
  const int16_t x = 10, y = 86, w = W - 20, h = 16;
  fb.drawRect(x, y, w, h, TFT_DARKGREY);
  fb.drawFastVLine(x + w / 2, y, h, TFT_DARKGREY);   // centre detent
  if (!s.match.valid) return;

  float clamped = s.match.pct;
  if (clamped >  PITCH_RANGE_PCT) clamped =  PITCH_RANGE_PCT;
  if (clamped < -PITCH_RANGE_PCT) clamped = -PITCH_RANGE_PCT;
  int16_t span   = (w / 2) - 5;
  int16_t offset = (int16_t)((clamped / PITCH_RANGE_PCT) * span);
  fb.fillRect(x + w / 2 + offset - 3, y + 2, 6, h - 4, matchColour(s.match));
}

// ---------------------------------------------------------------------------
static void renderMatch(const UiState& s) {
  const uint16_t col = matchColour(s.match);

  // Header strip
  fb.fillRect(0, 0, W, 20, TFT_NAVY);
  fb.setTextDatum(TL_DATUM);
  fb.setTextColor(TFT_WHITE, TFT_NAVY);
  char hdr[16];
  snprintf(hdr, sizeof(hdr), "DECK %c", s.activeIsA ? 'A' : 'B');
  fb.drawString(hdr, 6, 3, 2);

  char sub[24];
  if (s.locked) snprintf(sub, sizeof(sub), "LOCKED");
  else          snprintf(sub, sizeof(sub), "TAPS %lu", (unsigned long)s.tapCount);
  fb.setTextDatum(TR_DATUM);
  fb.drawString(sub, W - 26, 3, 2);

  // Beat indicator / visual metronome
  if (s.beatPulse || s.tapFlash) fb.fillCircle(W - 12, 10, 6, TFT_CYAN);
  else                           fb.drawCircle(W - 12, 10, 6, TFT_DARKGREY);

  // Big BPM readout, coloured by match state
  char big[10];
  fmtBpm(big, sizeof(big), s.activeBpm);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(col, TFT_BLACK);
  if (s.activeBpm > 0.0f) fb.drawString(big, W / 2, 52, 7);   // 48px 7-seg
  else                    fb.drawString(big, W / 2, 52, 4);

  drawPitchBar(s);

  // Both decks + tap confidence
  fb.setTextDatum(TL_DATUM);
  fb.setTextColor(TFT_WHITE, TFT_BLACK);
  char a[10], b[10], line[40];
  fmtBpm(a, sizeof(a), s.bpmA);
  fmtBpm(b, sizeof(b), s.bpmB);
  snprintf(line, sizeof(line), "A %s   B %s", a, b);
  fb.drawString(line, 8, 106, 2);

  if (s.jitterMs >= 0.0f) {
    char j[12];
    snprintf(j, sizeof(j), "+/-%dms", (int)(s.jitterMs + 0.5f));
    fb.setTextDatum(TR_DATUM);
    fb.setTextColor(s.solid ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
    fb.drawString(j, W - 8, 106, 2);
  }

  // Guidance line
  fb.setTextDatum(TL_DATUM);
  fb.setTextColor(col, TFT_BLACK);
  if (s.match.valid) {
    const char* oct = (s.match.octave > 0) ? " x2" : (s.match.octave < 0) ? " 1/2" : "";
    char drift[12];
    fmtDrift(drift, sizeof(drift), s.match.driftSec);
    if (s.match.outOfRange) {
      snprintf(line, sizeof(line), "OUT OF RANGE %+.2f%%%s", s.match.pct, oct);
    } else if (s.match.matched) {
      snprintf(line, sizeof(line), "MATCHED  drift %s", drift);
    } else {
      snprintf(line, sizeof(line), "%s %+.2f%%%s  drift %s",
               (s.match.target > s.activeBpm) ? "SPEED UP" : "SLOW DN",
               s.match.pct, oct, drift);
    }
  } else if (s.activeBpm > 0.0f) {
    snprintf(line, sizeof(line), "x2 %.1f    half %.1f",
             s.activeBpm * 2.0f, s.activeBpm * 0.5f);
  } else {
    snprintf(line, sizeof(line), "tap the beat...");
  }
  fb.drawString(line, 8, 120, 2);
}

static void renderLibrary(const UiState& s) {
  fb.fillRect(0, 0, W, 20, TFT_NAVY);
  fb.setTextDatum(TL_DATUM);
  fb.setTextColor(TFT_WHITE, TFT_NAVY);
  fb.drawString("LIBRARY", 6, 3, 2);

  char live[12], liveStr[24];
  fmtBpm(live, sizeof(live), s.activeBpm);
  snprintf(liveStr, sizeof(liveStr), "live %s", live);
  fb.setTextDatum(TR_DATUM);
  fb.drawString(liveStr, W - 6, 3, 2);

  // Page of 5 slots containing the cursor.
  const uint8_t perPage = 5;
  uint8_t start = (s.cursor / perPage) * perPage;
  fb.setTextDatum(TL_DATUM);
  for (uint8_t r = 0; r < perPage; r++) {
    uint8_t i = start + r;
    if (i >= NUM_SLOTS) break;
    int16_t y = 24 + r * 18;

    bool sel = (i == s.cursor);
    if (sel) fb.fillRect(0, y, W, 17, TFT_DARKGREEN);

    char bpmStr[10];
    fmtBpm(bpmStr, sizeof(bpmStr), Lib::slots[i].bpm);
    char row[48];
    if (Lib::slots[i].name[0] != '\0') {
      snprintf(row, sizeof(row), "%c  %s  %s", 'A' + i, bpmStr, Lib::slots[i].name);
    } else {
      snprintf(row, sizeof(row), "%c  %s", 'A' + i, bpmStr);
    }
    fb.setTextColor(sel ? TFT_WHITE : TFT_LIGHTGREY, sel ? TFT_DARKGREEN : TFT_BLACK);
    fb.drawString(row, 6, y + 1, 2);
  }

  fb.setTextColor(TFT_DARKGREY, TFT_BLACK);
  fb.drawString("TAP:next  SWAP:save  MODE:>", 6, 119, 2);
}

static void renderWifi(const UiState& s) {
  fb.fillRect(0, 0, W, 20, TFT_NAVY);
  fb.setTextDatum(TL_DATUM);
  fb.setTextColor(TFT_WHITE, TFT_NAVY);
  fb.drawString("WIFI LIBRARY", 6, 3, 2);

  fb.setTextColor(TFT_WHITE, TFT_BLACK);
  if (s.wifiOn) {
    char line[48];
    snprintf(line, sizeof(line), "SSID  %s", s.wifiSsid);
    fb.drawString(line, 8, 32, 2);
    snprintf(line, sizeof(line), "PASS  %s", s.wifiPass);
    fb.drawString(line, 8, 54, 2);
    fb.setTextColor(TFT_GREEN, TFT_BLACK);
    snprintf(line, sizeof(line), "http://%s", s.wifiIp);
    fb.drawString(line, 8, 78, 4);
  } else {
    fb.setTextColor(TFT_DARKGREY, TFT_BLACK);
    fb.drawString("OFF", 8, 40, 4);
    fb.setTextColor(TFT_WHITE, TFT_BLACK);
    fb.drawString("TAP to enable", 8, 72, 2);
  }

  fb.setTextColor(TFT_DARKGREY, TFT_BLACK);
  fb.drawString("TAP:toggle  MODE:back", 6, 119, 2);
}

// ---------------------------------------------------------------------------
namespace Display {

void begin() {
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  tft.init();
  tft.setRotation(1);            // 240x135 landscape
  tft.fillScreen(TFT_BLACK);

  // Off-screen buffer: 16-bit if it fits (~65KB), else 8-bit (~32KB).
  fb.setColorDepth(16);
  fbReady = (fb.createSprite(W, H) != NULL);
  if (!fbReady) {
    fb.setColorDepth(8);
    fbReady = (fb.createSprite(W, H) != NULL);
  }
}

void splash() {
  if (!fbReady) return;
  fb.fillSprite(TFT_BLACK);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(TFT_CYAN, TFT_BLACK);
  fb.drawString("openBPMcount", W / 2, 50, 4);
  fb.setTextColor(TFT_WHITE, TFT_BLACK);
  fb.drawString("tap the beat...", W / 2, 84, 2);
  fb.pushSprite(0, 0);
}

void render(const UiState& s) {
  if (!fbReady) return;
  fb.fillSprite(TFT_BLACK);
  switch (s.mode) {
    case MODE_MATCH:   renderMatch(s);   break;
    case MODE_LIBRARY: renderLibrary(s); break;
    case MODE_WIFI:    renderWifi(s);    break;
  }
  fb.pushSprite(0, 0);
}

void setPowerSave(bool on) {
  digitalWrite(PIN_BL, on ? LOW : HIGH);
}

} // namespace Display
