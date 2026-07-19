# Contributing to openBPM

Thanks for taking an interest. This is a small, practical project — a BPM counter that
DJs actually use — so the bar for contributions is "does it make the thing better in a
booth", not architectural purity.

## Ways to help

- **Report a bug.** Especially anything you hit on real hardware.
- **Add a board.** The display layer is abstracted; new screens are welcome (see below).
- **Improve the beatmatch maths** or the tap-tempo accuracy.
- **Tell us it worked.** Which board, which enclosure, how it felt to use — open a
  discussion. Real-world reports are genuinely useful and cost you five minutes.

## Getting set up

```bash
git clone https://github.com/fobiat/openBPM
cd openBPM
pio run                      # builds the default (tft) env
pio run -e oled -e tft -e pinscan   # build everything
```

You need [PlatformIO](https://platformio.org/) — either the VS Code extension or
`pip install platformio`.

**Please build all three environments before opening a PR.** CI does it too, but it's
faster to catch locally. A change to shared code can easily break the display you
aren't looking at.

## Testing without hardware

Most of the interesting logic — the tap-tempo averaging, octave-aware matching, drift
timer, pitch percentages — lives in [`src/app.cpp`](src/app.cpp) and has no dependency
on a screen, a button, or Arduino at all. `Deck` takes the current time as a parameter
instead of calling `millis()`, so a whole tapping session can be simulated
deterministically:

```bash
pio test -e native
```

**Please add a test for any change to the BPM engine, and run the suite before you
open a PR.** CI runs it ahead of the firmware builds. It is not ceremony — the suite
caught a real bug on its first run, where a single fumbled tap dropped a steady
120 BPM readout to ~43.

Anything needing the platform (NVS persistence) lives in
[`src/library.cpp`](src/library.cpp), which the native build excludes. Keep it that
way: if you find yourself wanting `millis()` or `Preferences` in `app.cpp`, pass the
value in instead.

If you *do* have hardware, say so in the PR and describe what you actually observed.
"Compiles" and "works on a deck" are very different claims, and we'd rather know which
one you're making.

## Adding a display

1. Implement the interface in [`src/display.h`](src/display.h) in a new
   `src/display_yourscreen.cpp`, including the `UiState` constructor.
2. Add an env to [`platformio.ini`](platformio.ini) with a `build_src_filter` that
   excludes the other display front-ends.
3. Add it to the CI matrix in `.github/workflows/build.yml`.

Each front-end lays the screen out to suit its own size and colour depth — don't try
to make one layout serve every panel.

## Style

Match the surrounding code rather than importing your own conventions:

- 2-space indent, no tabs
- `camelCase` for variables and functions, `UPPER_SNAKE` for constants
- Comments explain **why**, not what. If a line needs a comment to say what it does,
  the line usually wants rewriting instead.
- Keep hardware quirks documented at the point of use — the GPIO34–39 pull-up trap
  and the GPIO0 strapping behaviour are both the kind of thing that costs someone an
  evening otherwise.

## Commits and PRs

- Conventional-ish prefixes (`feat:`, `fix:`, `docs:`, `refactor:`) are appreciated
- Explain *why* in the body, not just what changed
- One logical change per PR where you can manage it

## Reporting hardware issues

Include:

- Which board and display variant
- The build env (`tft` / `oled`)
- Whether buttons are onboard, wired, or both
- Serial monitor output at 115200 if there is any

Output from `pio run -e pinscan -t upload` is gold for anything button-related.

## Licence

Contributions are accepted under the [MIT Licence](LICENSE), same as the rest of the
project.
