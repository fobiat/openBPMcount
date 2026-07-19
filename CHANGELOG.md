# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.2.0] — 2026-07-19

### Fixed

- **A single fumbled tap no longer wrecks the reading.** An off-beat tap cleared the
  averaging window and was then stored as its only sample, so one missed beat at a
  steady 120 BPM slammed the readout to ~43. The outlier is now discarded as well,
  and the previous reading stands while the window refills — two clean taps
  re-establish the tempo. Found by the new test suite on its first run.
- Clearing a slot from the device (storing with no live reading) zeroed the BPM but
  left the record's name behind, so an empty slot kept a stale name.
- The OLED's tap-jitter readout built its ± sign from a raw `0xB1` byte; a font
  without `U+00B1` would silently drop it. Uses ASCII `+/-` now, matching the TFT.
- A failed TFT sprite allocation left a silently black screen, indistinguishable from
  a wiring fault. It now reports the failure on serial and on the panel.
- The CSV download was still named `openbpmcount.csv` after the rename.

### Added

- **Configurable turntable pitch range.** Was hardcoded to ±8 %, which is right for a
  Technics 1210 but wrong for the many decks that do ±16 or wider — those were being
  told perfectly reachable matches were `OUT OF RANGE`. Set it from the WiFi page;
  persisted to flash. The pitch bar scales with it.
- **A desktop unit-test suite** — 34 Unity tests over tempo averaging, outlier
  rejection, idle reset, jitter, beat prediction, octave matching, pitch range and the
  drift timer. Run with `pio test -e native`; no board needed.
- CI runs the tests as a gate before building any firmware.

### Changed

- `app.h`/`app.cpp` are now free of any Arduino dependency, so the BPM engine builds
  and runs on a desktop. Platform-specific persistence moved to `library.cpp`.
- ESP32 settings moved from `[env]` to an `[esp32]` section that the device
  environments extend, so the native test env doesn't inherit a board.

## [1.1.0] — 2026-07-19

### Added

- **Gesture control on the onboard BOOT button**, so the board is fully usable with
  nothing wired to it: short press = TAP, long press = SWAP, hold = MODE. Set
  `GESTURE_PIN = 255` to disable once real buttons are wired.

### Changed

- `pinscan` no longer reports per-edge activity on input-only pins (GPIO34–39), which
  float and cross-talk. It prints a twice-a-second level snapshot instead, so a *held*
  button is distinguishable from a floating pin.

### Notes

A pin scan of the ideaspark ESP32-WROOM-32 established that **GPIO0 is the only
onboard button software can read**. The board's other buttons are EN/RST, which reset
the chip in hardware and never reach a GPIO.

## [1.0.0] — 2026-07-19

First tagged release.

### Added

- **Tap tempo** with a rolling 8-interval average, outlier rejection for missed or
  double taps, and a 3-second idle auto-reset
- **Two decks (A/B)** with a pitch-percentage readout — how far to move the fader
- **Octave-aware matching** so half/double-time records still line up, tagged
  `x2` / `1/2`
- **Drift timer** — seconds until the decks slip a full beat apart (`60 / ΔBPM`)
- **Pitch bar** with a centre detent, colour-coded on the TFT
- **Pitch-range warning** when a match needs more than ±8 %
- **Beat pulse** indicator acting as a visual metronome
- **Tap-jitter readout** so you know whether to trust the reading
- **8 named BPM slots** persisted to flash, surviving power-off
- **WiFi library** — the ESP32 hosts its own access point so records can be named and
  exported as CSV from a phone
- **Screen sleep** after 2 minutes idle
- Support for **two displays from one codebase** via a display abstraction and
  separate PlatformIO environments (`oled`, `tft`)
- `pinscan` diagnostic environment for identifying button GPIOs by measurement
- MIT licence, and a GitHub Actions matrix build across all three environments

[Unreleased]: https://github.com/fobiat/openBPM/compare/v1.2.0...HEAD
[1.2.0]: https://github.com/fobiat/openBPM/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/fobiat/openBPM/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/fobiat/openBPM/releases/tag/v1.0.0
