# openBPMcount

A tap-tempo **BPM counter and beatmatch assistant** for vinyl DJing, running on an
**ideaspark ESP32-WROOM-32** with its integrated 0.96" OLED. Tap the onboard button
in time with a record and it reads out the BPM in big, booth-legible digits — then
tells you exactly how much pitch to dial in to match your other deck.

## Hardware

| Part | Detail |
|------|--------|
| Board | ideaspark ESP32-WROOM-32 (integrated OLED variant) |
| Display | 0.96" SSD1306 OLED, 128×64, I2C @ `0x3C` (SDA=GPIO21, SCL=GPIO22) |
| Button | onboard **BOOT** button, GPIO0 (active LOW) |

No wiring required — the OLED and button are on the board. Just plug in over USB.

## Controls (single button, three gestures)

The one onboard button reads three press lengths:

| Gesture | Length |
|---------|--------|
| **Short** | < 0.6 s |
| **Long** | 0.6 – 1.6 s |
| **Hold** | ≥ 1.6 s |

What they do depends on the mode:

| | **MATCH mode** (live mixing) | **LIBRARY mode** (stored BPMs) |
|--|--|--|
| **Short** | Tap a beat for the active deck | Move cursor to next slot |
| **Long** | Lock the deck & switch A ⇄ B | Store the live BPM into the slot* |
| **Hold** | Go to **LIBRARY** | Return to **MATCH** |

\* Storing with no live reading clears the slot.

## How to beatmatch with it

1. Power on — **Deck A** is active. Tap along with the record that's playing.
   The big number settles on the BPM after a few taps.
2. **Long-press** to lock Deck A and switch to **Deck B**.
3. Tap along with the record you're cueing on Deck B.
4. The bottom line shows the **pitch %** to dial into the active deck plus a
   **SPEED UP / SLOW DN / MATCH** nudge, relative to the other deck.
5. Long-press any time to hop back to a deck and re-tap it.

```
┌──────────────────────────┐
│ DECK B          TAPS 7   │
│                          │
│        126.8             │
│ ──────────────────────── │
│ A:128.3 B:126.8          │
│ SPEED UP +1.2%           │
└──────────────────────────┘
```

The pitch figure is `((target / active) - 1) × 100` — i.e. how much to move the
active deck's pitch fader so it matches the other deck.

## Memory slots (crate digging)

Three slots — **A / B / C** — live in **LIBRARY mode** (Hold to enter). Measure a
record in MATCH mode, Hold into LIBRARY, cursor to a slot, and Long-press to store
it. Slots are written to the ESP32's flash (NVS), so **they survive power-off** —
measure a stack of records at the shop and they're still there next time you boot.

## Smart features

- **Octave-aware matching (half / double time).** A record tapped at 64 BPM and one
  at 128 BPM are the same tempo an octave apart. The match readout automatically
  picks whichever of the other deck's tempo, ×2, or ½ needs the least pitch change,
  and tags it `x2` or `1/2` so you know what it did.
- **Turntable pitch-range warning.** If matching the two decks would need more than
  ±8 % pitch (beyond a standard turntable's range), the line shows `!RANGE` instead
  of a nudge — a cue that they won't mix straight, or that it's really a half/double
  relationship.
- **Single-record identify.** With only one deck measured, the footer shows that
  record's `x2` and `half` tempos to help you place ambiguous BPMs.
- **OLED sleep.** After 2 minutes idle the display blanks (saves the battery on the
  board's LiPo connector and prevents burn-in); any button press wakes it.

## How the BPM is measured

Each tap records the interval since the previous one. It keeps a rolling window of
the last 8 intervals and averages them (`BPM = 60000 / avg_interval_ms`), which is far
more stable than timing a single pair of taps. Wildly off-beat taps (a missed or
double beat) are rejected instead of corrupting the average, and a pause of more than
3 seconds starts a fresh measurement so you can move straight to the next record.

## Build & flash (PlatformIO)

With [PlatformIO](https://platformio.org/) (VS Code extension or CLI):

```bash
pio run                 # compile
pio run --target upload # flash over USB
pio device monitor      # optional serial monitor @ 115200
```

Everything is configured in [`platformio.ini`](platformio.ini); the only dependency
is [olikraus/U8g2](https://github.com/olikraus/u8g2).

## Ideas for future versions

- Load a stored slot back onto a deck to match against it directly
- Automatic BPM detection via a microphone module (MAX9814 / INMP441)
- Configurable pitch range (±8 % / ±16 %) for different turntables
