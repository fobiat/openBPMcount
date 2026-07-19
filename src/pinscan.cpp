// Button pin scanner — a diagnostic build, not part of the firmware.
//
//   pio run -e pinscan -t upload
//   pio device monitor
//
// Press each button on the board in turn; it prints the GPIO that changed.
// Put those numbers into TAP_PINS / SWAP_PINS / MODE_PINS in main.cpp.
//
// Boards differ between revisions, so measuring beats trusting a datasheet.
#include <Arduino.h>

// Plausible button pins. The SPI display pins (2, 4, 15, 18, 23, 32) are left
// out so the screen's own traffic doesn't show up as false presses.
static const uint8_t SCAN_PINS[] = {
  0, 5, 12, 13, 14, 16, 17, 19, 21, 22, 25, 26, 27, 33, 34, 35, 36, 39
};
static const uint8_t N = sizeof(SCAN_PINS) / sizeof(SCAN_PINS[0]);

static bool     lastState[N];
static uint32_t lastChange[N];

static const uint16_t DEBOUNCE_MS = 25;

void setup() {
  Serial.begin(115200);
  delay(600);

  Serial.println();
  Serial.println(F("openBPM pin scanner"));
  Serial.println(F("Press each button. Pins that go LOW are buttons to GND."));
  Serial.println(F("A pin that never settles is floating - ignore it."));
  Serial.println();

  for (uint8_t i = 0; i < N; i++) {
    // GPIO34-39 are input-only and have no internal pull-up.
    pinMode(SCAN_PINS[i], SCAN_PINS[i] >= 34 ? INPUT : INPUT_PULLUP);
    lastState[i]  = digitalRead(SCAN_PINS[i]);
    lastChange[i] = 0;
  }

  Serial.print(F("Idle state: "));
  for (uint8_t i = 0; i < N; i++) {
    Serial.print(F("GPIO"));
    Serial.print(SCAN_PINS[i]);
    Serial.print(lastState[i] ? F("=1 ") : F("=0 "));
  }
  Serial.println();
  Serial.println(F("---- ready ----"));
}

void loop() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < N; i++) {
    bool s = digitalRead(SCAN_PINS[i]);
    if (s != lastState[i] && (now - lastChange[i]) > DEBOUNCE_MS) {
      lastState[i]  = s;
      lastChange[i] = now;
      Serial.print(s ? F("  released  GPIO") : F("* PRESSED   GPIO"));
      Serial.println(SCAN_PINS[i]);
    }
  }
  delay(5);
}
