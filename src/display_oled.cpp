// 0.96" SSD1306 128x64 monochrome front-end (ideaspark integrated OLED).
// Built only in the `oled` PlatformIO environment.
#include "display.h"

#include <U8g2lib.h>
#include <Wire.h>
#include <math.h>

static const uint8_t OLED_SDA = 21;
static const uint8_t OLED_SCL = 22;

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

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

// Compact "time until one beat of drift" for a 128px wide screen.
static void fmtDrift(char* buf, size_t n, float sec) {
  if (sec < 0)        snprintf(buf, n, "--");
  else if (sec > 599) snprintf(buf, n, "10m+");
  else if (sec >= 60) snprintf(buf, n, "%dm%02d", (int)(sec / 60), ((int)sec) % 60);
  else                snprintf(buf, n, "%ds", (int)sec);
}

// Horizontal pitch error bar: centre = matched, deflection = how far off.
static void drawPitchBar(float pct, bool valid) {
  const int16_t x = 0, y = 40, w = 128, h = 7;
  u8g2.drawFrame(x, y, w, h);
  u8g2.drawVLine(x + w / 2, y, h);          // centre detent
  if (!valid) return;

  float clamped = pct;
  if (clamped >  PITCH_RANGE_PCT) clamped =  PITCH_RANGE_PCT;
  if (clamped < -PITCH_RANGE_PCT) clamped = -PITCH_RANGE_PCT;
  int16_t span   = (w / 2) - 3;
  int16_t offset = (int16_t)((clamped / PITCH_RANGE_PCT) * span);
  u8g2.drawBox(x + w / 2 + offset - 1, y + 1, 3, h - 2);
}

// ---------------------------------------------------------------------------
static void renderMatch(const UiState& s) {
  char big[10];
  fmtBpm(big, sizeof(big), s.activeBpm);

  // Header
  u8g2.setFont(u8g2_font_6x10_tf);
  char hdr[16];
  snprintf(hdr, sizeof(hdr), "DECK %c", s.activeIsA ? 'A' : 'B');
  u8g2.drawStr(0, 8, hdr);

  if (s.locked) {
    u8g2.drawStr(50, 8, "LOCK");
  } else {
    char taps[12];
    snprintf(taps, sizeof(taps), "TAPS %lu", (unsigned long)s.tapCount);
    u8g2.drawStr(50, 8, taps);
  }
  // Beat / tap indicator, doubles as a visual metronome.
  if (s.beatPulse || s.tapFlash) u8g2.drawDisc(122, 4, 4);

  // Big BPM
  u8g2.setFont(u8g2_font_fub25_tr);
  uint8_t w = u8g2.getStrWidth(big);
  u8g2.drawStr((128 - w) / 2, 36, big);

  drawPitchBar(s.match.pct, s.match.valid);

  // Line 1: both decks + tap confidence
  u8g2.setFont(u8g2_font_6x10_tf);
  char a[10], b[10], line[26];
  fmtBpm(a, sizeof(a), s.bpmA);
  fmtBpm(b, sizeof(b), s.bpmB);
  snprintf(line, sizeof(line), "A:%s B:%s", a, b);
  u8g2.drawStr(0, 55, line);
  if (s.jitterMs >= 0.0f) {
    char j[10];
    snprintf(j, sizeof(j), "%c%dms", 0xB1, (int)(s.jitterMs + 0.5f));
    u8g2.drawStr(128 - u8g2.getStrWidth(j), 55, j);
  }

  // Line 2: guidance
  if (s.match.valid) {
    const char* oct = (s.match.octave > 0) ? "x2" : (s.match.octave < 0) ? "/2" : "";
    char drift[10];
    fmtDrift(drift, sizeof(drift), s.match.driftSec);
    if (s.match.outOfRange) {
      snprintf(line, sizeof(line), "!RANGE %+.1f%% %s", s.match.pct, oct);
    } else if (s.match.matched) {
      snprintf(line, sizeof(line), "MATCH %+.1f%% %s", s.match.pct, drift);
    } else {
      snprintf(line, sizeof(line), "%s %+.1f%% %s",
               (s.match.target > s.activeBpm) ? "UP  " : "DOWN",
               s.match.pct, drift);
    }
  } else if (s.activeBpm > 0.0f) {
    snprintf(line, sizeof(line), "x2 %.0f  half %.1f",
             s.activeBpm * 2.0f, s.activeBpm * 0.5f);
  } else {
    snprintf(line, sizeof(line), "tap the beat...");
  }
  u8g2.drawStr(0, 64, line);
}

static void renderLibrary(const UiState& s) {
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 8, "LIBRARY");
  char live[16], liveStr[22];
  fmtBpm(live, sizeof(live), s.activeBpm);
  snprintf(liveStr, sizeof(liveStr), "live %s", live);
  u8g2.drawStr(128 - u8g2.getStrWidth(liveStr), 8, liveStr);
  u8g2.drawHLine(0, 11, 128);

  // Page of 4 slots containing the cursor.
  uint8_t page  = s.cursor / 4;
  uint8_t start = page * 4;
  for (uint8_t r = 0; r < 4; r++) {
    uint8_t i = start + r;
    if (i >= NUM_SLOTS) break;
    int16_t y = 22 + r * 10;

    char bpmStr[10];
    fmtBpm(bpmStr, sizeof(bpmStr), Lib::slots[i].bpm);
    char row[26];
    if (Lib::slots[i].name[0] != '\0') {
      snprintf(row, sizeof(row), "%c %s %s", 'A' + i, bpmStr, Lib::slots[i].name);
    } else {
      snprintf(row, sizeof(row), "%c %s", 'A' + i, bpmStr);
    }

    if (i == s.cursor) {
      u8g2.drawBox(0, y - 8, 128, 10);
      u8g2.setDrawColor(0);
      u8g2.drawStr(2, y, row);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(2, y, row);
    }
  }

  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(0, 64, "TAP:next SWAP:save MODE:>");
}

static void renderWifi(const UiState& s) {
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 8, "WIFI LIBRARY");
  u8g2.drawHLine(0, 11, 128);

  if (s.wifiOn) {
    char line[26];
    snprintf(line, sizeof(line), "SSID %s", s.wifiSsid);
    u8g2.drawStr(0, 24, line);
    snprintf(line, sizeof(line), "PASS %s", s.wifiPass);
    u8g2.drawStr(0, 36, line);
    snprintf(line, sizeof(line), "http://%s", s.wifiIp);
    u8g2.drawStr(0, 48, line);
  } else {
    u8g2.drawStr(0, 28, "OFF");
    u8g2.drawStr(0, 42, "TAP to enable");
  }

  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(0, 64, "TAP:toggle MODE:back");
}

// ---------------------------------------------------------------------------
namespace Display {

void begin() {
  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.setI2CAddress(0x3C << 1);
  u8g2.begin();
}

void splash() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(6, 26, "openBPMcount");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(2, 44, "tap the beat...");
  u8g2.sendBuffer();
}

void render(const UiState& s) {
  u8g2.clearBuffer();
  switch (s.mode) {
    case MODE_MATCH:   renderMatch(s);   break;
    case MODE_LIBRARY: renderLibrary(s); break;
    case MODE_WIFI:    renderWifi(s);    break;
  }
  u8g2.sendBuffer();
}

void setPowerSave(bool on) {
  u8g2.setPowerSave(on ? 1 : 0);
}

} // namespace Display
