// openBPMcount — tap-tempo BPM counter & beatmatch assistant for vinyl DJing
//
// Runs on either ideaspark ESP32-WROOM-32 board; pick the PlatformIO env:
//   env:oled -> 0.96" SSD1306 128x64 mono
//   env:tft  -> 1.14" ST7789  240x135 colour
//
// Three momentary buttons, each wired GPIO -> button -> GND (internal pull-ups,
// active LOW). No external resistors needed.
//   TAP = GPIO27   SWAP = GPIO26   MODE = GPIO25
//
// MODE cycles: MATCH -> LIBRARY -> WIFI -> MATCH
//
//   MATCH    TAP: tap a beat      SWAP: lock + switch deck A<->B
//   LIBRARY  TAP: next slot       SWAP: store live BPM (0 = clear)
//   WIFI     TAP: toggle the AP   SWAP: -
//
// See README.md for wiring and usage.

#include <Arduino.h>

#include "app.h"
#include "display.h"
#include "webui.h"

// ---------------------------------------------------------------------------
static const uint8_t  PIN_TAP     = 27;
static const uint8_t  PIN_SWAP    = 26;
static const uint8_t  PIN_MODE    = 25;

static const uint16_t DEBOUNCE_MS = 25;
static const uint32_t SLEEP_MS    = 120000;  // blank the screen after this idle
static const uint16_t TAP_FLASH_MS = 90;

// ---------------------------------------------------------------------------
// Buttons — one debounced falling-edge detector each
// ---------------------------------------------------------------------------
struct Button {
  uint8_t  pin;
  bool     stable;
  bool     raw;
  uint32_t changedMs;
  explicit Button(uint8_t p) : pin(p), stable(false), raw(false), changedMs(0) {}
};

static Button btnTap (PIN_TAP);
static Button btnSwap(PIN_SWAP);
static Button btnMode(PIN_MODE);

// True exactly once, on the debounced press edge.
static bool pressed(Button& b) {
  uint32_t now = millis();
  bool reading = (digitalRead(b.pin) == LOW); // active LOW
  if (reading != b.raw) {
    b.raw = reading;
    b.changedMs = now;
  }
  if ((now - b.changedMs) >= DEBOUNCE_MS && reading != b.stable) {
    b.stable = reading;
    if (b.stable) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
static Deck  deckA;
static Deck  deckB;
static Deck* active = &deckA;

static Mode     mode           = MODE_MATCH;
static uint8_t  cursorSlot     = 0;
static uint32_t lastTapFlashMs = 0;
static uint32_t lastActivityMs = 0;
static bool     asleep         = false;

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_TAP,  INPUT_PULLUP);
  pinMode(PIN_SWAP, INPUT_PULLUP);
  pinMode(PIN_MODE, INPUT_PULLUP);

  Lib::begin();
  Display::begin();
  Display::splash();
  delay(1200);

  lastActivityMs = millis();
}

void loop() {
  uint32_t now = millis();

  bool tapHit  = pressed(btnTap);
  bool swapHit = pressed(btnSwap);
  bool modeHit = pressed(btnMode);

  if (tapHit || swapHit || modeHit) {
    lastActivityMs = now;
    // If the screen was asleep, this press only wakes it — don't act on it.
    if (asleep) {
      asleep = false;
      Display::setPowerSave(false);
      tapHit = swapHit = modeHit = false;
    }
  }

  // MODE always advances to the next screen.
  if (modeHit) {
    mode = (mode == MODE_MATCH)   ? MODE_LIBRARY
         : (mode == MODE_LIBRARY) ? MODE_WIFI
                                  : MODE_MATCH;
  }

  switch (mode) {
    case MODE_MATCH:
      if (tapHit) {
        active->tap(now);
        lastTapFlashMs = now;
      }
      if (swapHit) {
        if (active->bpm > 0.0f) active->locked = true;
        active = (active == &deckA) ? &deckB : &deckA;
      }
      break;

    case MODE_LIBRARY:
      if (tapHit)  cursorSlot = (cursorSlot + 1) % NUM_SLOTS;
      if (swapHit) Lib::store(cursorSlot, active->bpm); // 0 clears
      break;

    case MODE_WIFI:
      if (tapHit) {
        if (WebUI::isOn()) WebUI::stop();
        else               WebUI::start();
      }
      break;
  }

  WebUI::loop();
  WebUI::setLiveBpm(active->bpm);

  // Sleep the screen after inactivity (battery + burn-in). Never sleep while
  // the AP is up — you'd be looking at the screen for the address.
  if (!asleep && !WebUI::isOn() && (now - lastActivityMs) > SLEEP_MS) {
    asleep = true;
    Display::setPowerSave(true);
  }

  if (!asleep) {
    Deck& other = (active == &deckA) ? deckB : deckA;

    UiState s;
    s.mode      = mode;
    s.bpmA      = deckA.bpm;
    s.bpmB      = deckB.bpm;
    s.activeIsA = (active == &deckA);
    s.activeBpm = active->bpm;
    s.tapCount  = active->tapCount;
    s.locked    = active->locked;
    s.solid     = active->solid();
    s.jitterMs  = active->jitterMs();
    s.beatPulse = active->beatPulse(now);
    s.tapFlash  = (now - lastTapFlashMs) < TAP_FLASH_MS;
    s.match     = computeMatch(active->bpm, other.bpm);
    s.cursor    = cursorSlot;
    s.wifiOn    = WebUI::isOn();
    s.wifiSsid  = WebUI::ssid();
    s.wifiPass  = WebUI::password();
    s.wifiIp    = WebUI::ip();

    Display::render(s);
  }

  delay(5); // ~200 Hz button/UI loop, plenty responsive for tapping
}
