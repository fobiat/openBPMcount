// Phone-accessible BPM library over WiFi.
//
// The ESP32 hosts its own access point, so it works in a booth or a record shop
// with no router. Connect a phone, open the page, and you can name records,
// clear slots, and export the lot as CSV — which is how the library gets useful
// names when the device itself only has three buttons.
#pragma once

#include <Arduino.h>

namespace WebUI {
  void        start();          // bring up the AP + HTTP server
  void        stop();           // shut it down (saves power)
  void        loop();           // pump the server; call every iteration
  bool        isOn();
  const char* ssid();
  const char* password();
  const char* ip();

  // Live BPM shown on the web page, pushed from the main loop.
  void setLiveBpm(float bpm);
}
