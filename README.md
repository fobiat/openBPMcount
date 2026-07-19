# openBPM

[![build](https://github.com/fobiat/openBPM/actions/workflows/build.yml/badge.svg)](https://github.com/fobiat/openBPM/actions/workflows/build.yml)
[![release](https://img.shields.io/github/v/release/fobiat/openBPM)](https://github.com/fobiat/openBPM/releases/latest)
[![license: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.md)

A tap-tempo **BPM counter and beatmatch assistant** for vinyl DJing, running on an
**ideaspark ESP32-WROOM-32**. Tap a button in time with a record and it reads out
the BPM in big, booth-legible digits — then tells you exactly how much pitch to dial
in to match your other deck, and how long you've got before they drift apart.

Roughly **£20–30 of off-the-shelf parts**, no PCB to make, and it runs with **no
wiring at all** — the board's BOOT button is enough to use it. Supports both ideaspark
display boards from one codebase.

**Jump to:** [Build one](docs/HARDWARE.md) · [Flash it](#build--flash) ·
[Controls](#controls) · [Contributing](CONTRIBUTING.md) ·
[Changelog](CHANGELOG.md) · [Releases](https://github.com/fobiat/openBPM/releases)

## Hardware

Full parts list, wiring, GPIO reference and enclosure notes are in
**[docs/HARDWARE.md](docs/HARDWARE.md)**. The short version:

| Part | Detail |
|------|--------|
| Board | ideaspark ESP32-WROOM-32 |
| Display *(either)* | 0.96" SSD1306 OLED 128×64 I2C `0x3C` (SDA=21, SCL=22) — build env `oled` |
| | 1.14" ST7789 TFT 240×135 SPI colour — build env `tft` |
| Buttons | Onboard buttons **and/or** external momentary buttons — see below |

The colour TFT is the better choice: colour carries the match state, and you can
read green/amber/red across a dark booth far faster than you can read digits.

### Buttons

It works **with no wiring at all**, and gets nicer once you add buttons.

#### No wiring: the onboard BOOT button

The BOOT button drives all three actions by press length, so you can flash the board
and use it straight away:

| Gesture | Action |
|---------|--------|
| **Short** press (< 0.6 s) | TAP |
| **Long** press (0.6 – 1.6 s) | SWAP |
| **Hold** (≥ 1.6 s) | MODE |

It's the only onboard button software can read on this board — the others are
**EN/RST**, which reset the chip in hardware and never reach a GPIO.

#### Better: wire real buttons

Each action also accepts **several pins at once**, so wired buttons and the onboard
one both work — press whichever is to hand. Tapping a beat on a dedicated button
beats gesturing on BOOT. The pin lists are at the top of
[`src/main.cpp`](src/main.cpp):

```cpp
static const uint8_t TAP_PINS[]  = {27};
static const uint8_t SWAP_PINS[] = {26};
static const uint8_t MODE_PINS[] = {25};

static const uint8_t GESTURE_PIN = 0;   // onboard BOOT; set 255 to disable
```

External buttons go between the GPIO and **GND** — no resistors needed, the firmware
enables internal pull-ups so each pin idles HIGH and reads LOW when pressed:

```
GPIO27 ──[ TAP  ]── GND
GPIO26 ──[ SWAP ]── GND
GPIO25 ──[ MODE ]── GND
```

Avoid GPIO 6–11 (flash), 12 (boot strap), 21/22 (OLED I2C) and 34–39 (no internal
pull-up). On the TFT board GPIO 2/4/15/18/23/32 belong to the display.

> **GPIO0 is the onboard BOOT button.** It's also a boot strapping pin, so holding it
> while the board powers up enters the bootloader instead of running the firmware.
> That's normal — just don't hold it through a reset.

#### Finding your board's onboard button pins

Board revisions differ, so measure rather than guess. Flash the scanner, open the
monitor, and press each button:

```bash
pio run -e pinscan -t upload
pio device monitor
```

It prints the GPIO behind each press:

```
* PRESSED   GPIO0
  released  GPIO0
```

Put those numbers into the pin lists above and reflash.

**Reading the results:** a real button gives clean, paired press/release lines. Pins
that fire in bursts, change in step with each other, or never settle are **floating**,
not buttons — GPIO34–39 in particular are input-only with no internal pull-up, so
they drift. For those the scanner prints a twice-a-second snapshot instead: press and
**hold** the button and watch for a level that stays put while you're holding it.

**Tip:** make the TAP button physically distinct — bigger, or a different coloured
cap — so you find it by feel in the dark without looking.

## Controls

**MODE** cycles through three screens: **MATCH → LIBRARY → WIFI → MATCH**.

| Screen | **TAP** | **SWAP** |
|--------|---------|----------|
| **MATCH** | Tap a beat for the active deck | Lock the deck & switch A ⇄ B |
| **LIBRARY** | Move cursor to next slot | Store live BPM into slot* |
| **WIFI** | Toggle the access point | — |

\* Storing with no live reading clears the slot.

## How to beatmatch with it

1. Power on — **Deck A** is active. **TAP** along with the record that's playing.
2. Press **SWAP** to lock Deck A and switch to **Deck B**.
3. **TAP** along with the record you're cueing.
4. Read the pitch % and nudge, dial it into the deck, and watch the bar centre.

```
┌────────────────────────────────┐
│ DECK B              TAPS 7  ●  │
│                                │
│           126.8                │
│                                │
│  ├────────────█───┼─────────┤  │   pitch bar, centre = matched
│                                │
│  A 128.3   B 126.8    +/-3ms   │
│  SPEED UP +1.18%  drift 40s    │
└────────────────────────────────┘
```

## What's on screen

- **Pitch %** — `((target / active) - 1) × 100`, i.e. how far to move the active
  deck's pitch fader. Colour-coded green / amber / red on the TFT.
- **Pitch bar** — glanceable deflection from centre; centre means matched.
- **Drift timer** — *"drift 40s"* is how long until the two decks slip a **full beat**
  apart at the current error (`60 / ΔBPM` seconds). It answers the question you
  actually care about mid-mix: how long before I need to nudge? 0.5 BPM out gives you
  two minutes; 0.1 BPM out gives you ten.
- **±ms** — the spread of your tap intervals. Low means you're tapping consistently,
  so trust the number. Turns green once the reading is solid.
- **Beat dot** — flashes on every predicted beat once a tempo is locked: a visual
  metronome you can check against the record.
- **`x2` / `1/2`** — the match was made an octave apart (see below).

## Smart behaviour

- **Octave-aware matching.** A record at 64 BPM and one at 128 BPM are the same tempo
  an octave apart. The match picks whichever of the other deck's tempo, ×2, or ½ needs
  the least pitch change, and tags it `x2` or `1/2` so you know what it did.
- **Pitch-range warning.** If matching would need more than ±8 % (beyond a standard
  turntable), it shows `OUT OF RANGE` — they won't mix straight.
- **Outlier rejection.** A missed or double tap restarts the averaging window instead
  of poisoning it, and a 3-second pause starts a fresh measurement for the next record.
- **Screen sleep.** Blanks after 2 minutes idle (battery/burn-in); any press wakes it.
  Never sleeps while the WiFi AP is up.

## WiFi library

Three buttons can't type a record name, so the **WIFI** screen brings up the ESP32's
own access point — no router needed, works in a shop or a booth:

| | |
|--|--|
| SSID | `openBPM` |
| Password | `beatmatch` |
| URL | `http://192.168.4.1` |

From a phone you can **name each of the 8 slots**, clear them, and **download the
whole library as CSV**. Names and BPMs are stored in the ESP32's flash (NVS), so they
survive power-off. Turn the AP off when you're done — it's the biggest power draw.

## Build & flash

With [PlatformIO](https://platformio.org/) (VS Code extension or CLI), pick the env
that matches your board:

```bash
pio run -e tft  -t upload    # 1.14" ST7789 colour  (default)
pio run -e oled -t upload    # 0.96" SSD1306 mono
pio run -e pinscan -t upload # button pin scanner (diagnostic)
pio device monitor           # optional, 115200
```

`tft` is the default env, so a bare `pio run -t upload` builds it.

### Tests

The BPM engine has no Arduino dependency — `Deck` takes the current time as a
parameter rather than calling `millis()` — so a whole tapping session can be
simulated and the tempo maths tested on a desktop, no board required:

```bash
pio test -e native      # 30 unit tests
```

They cover tap averaging, outlier rejection, idle reset, jitter, beat prediction,
octave matching, pitch range and the drift timer. CI runs them before it builds any
firmware. They earn their keep — they caught a real bug where a single fumbled tap
dropped a steady 120 BPM readout to ~43.

Prebuilt `firmware.bin` for each board is attached to every
[release](https://github.com/fobiat/openBPM/releases), and CI builds all three
environments on each push.

`TFT_eSPI` is configured entirely from `build_flags` in
[`platformio.ini`](platformio.ini) — the library's `User_Setup.h` is never edited, so
the build stays reproducible.

## Layout

```
src/
  app.h / app.cpp        BPM engine + beatmatch maths (no Arduino dependency)
  library.cpp            persisted BPM slots (NVS/flash)
  display.h              display interface + UiState (what a screen renders)
  display_oled.cpp       0.96" SSD1306 front-end   (env: oled)
  display_tft.cpp        1.14" ST7789 front-end    (env: tft)
  webui.h / webui.cpp    WiFi AP + phone-facing library page
  main.cpp               buttons, modes, main loop
  pinscan.cpp            standalone button pin scanner (env: pinscan)
test/test_bpm/           desktop unit tests          (env: native)
docs/
  HARDWARE.md            parts list, wiring, GPIO reference, enclosure
```

Each display front-end lays the screen out to suit its own size and colour depth;
the app logic never knows which screen it's driving.

`app.cpp` is deliberately platform-free so it can be tested on a desktop; everything
needing the platform is quarantined in `library.cpp`. That makes the beatmatch maths
the easiest place to contribute without owning any hardware.

## Enclosure

Fits a **Hammond 1591XXLBK** (87 × 57 × 39 mm) nicely — roughly stompbox sized.
The board (~55 × 28 mm) leaves room for three 12 mm panel-mount buttons in a row
(~48 mm total) and a LiPo underneath. Mount the board to the lid, cut a window for
the screen, and leave a side hole for USB so you can charge and reflash without
opening it.

## Ideas for future versions

- Automatic BPM detection from a microphone module (MAX9814 / INMP441) — the real
  endgame: no tapping at all
- Load a stored slot back onto a deck to match against it directly
- Battery gauge from the LiPo connector, and deep sleep on long idle
- Configurable pitch range (±8 % / ±16 %) for different turntables

Fancy building one of these? See [CONTRIBUTING.md](CONTRIBUTING.md).

## Contributing

Contributions are welcome — bug reports from real hardware especially, since most of
this has only been verified on one board. Adding a new display is deliberately
straightforward.

- [CONTRIBUTING.md](CONTRIBUTING.md) — setup, style, how to add a screen
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) — be decent to people
- [Discussions](https://github.com/fobiat/openBPM/discussions) — built one? Show it off

**Built one?** Post it in Discussions — which board, which enclosure, how it felt to
use. Real-world reports are the most useful thing you can send.

## Credits

Built on [U8g2](https://github.com/olikraus/u8g2) by olikraus,
[TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) by Bodmer, and the
[Arduino core for ESP32](https://github.com/espressif/arduino-esp32).

## Licence

[MIT](LICENSE) © Ohmic Labs — do what you like with it, including selling them.
