// Display abstraction for openBPM.
//
// The two front-ends (display_oled.cpp for the 0.96" SSD1306, display_tft.cpp
// for the 1.14" ST7789) each implement this same interface and lay the screen
// out to suit their size and colour depth. PlatformIO picks one per build env,
// so the app logic never knows which screen it's driving.
#pragma once

#include <Arduino.h>
#include "app.h"

enum Mode { MODE_MATCH, MODE_LIBRARY, MODE_WIFI };

// Everything a screen needs in order to render a frame.
struct UiState {
  Mode      mode;

  // Decks
  float     bpmA;
  float     bpmB;
  bool      activeIsA;
  float     activeBpm;
  uint32_t  tapCount;
  bool      locked;
  bool      solid;
  float     jitterMs;    // <0 when unknown
  bool      beatPulse;   // predicted beat is due -> flash
  bool      tapFlash;    // a tap just landed

  MatchInfo match;

  // Library
  uint8_t   cursor;

  // WiFi
  bool        wifiOn;
  const char* wifiSsid;
  const char* wifiPass;
  const char* wifiIp;

  UiState();
};

namespace Display {
  void begin();
  void splash();
  void render(const UiState& s);
  void setPowerSave(bool on);
}
