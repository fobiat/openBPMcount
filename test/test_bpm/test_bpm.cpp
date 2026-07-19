// Unit tests for the BPM engine and beatmatch maths.
//
//   pio test -e native
//
// Runs on the desktop — no board required. Deck takes the current time as a
// parameter, so a whole tapping session can be simulated deterministically.
#include <unity.h>

#include "app.h"

void setUp(void) {}
void tearDown(void) {}

// Tap a deck `n` times at a fixed interval, starting at t=1000ms.
static void tapAt(Deck& d, uint32_t intervalMs, uint8_t n, uint32_t start = 1000) {
  for (uint8_t i = 0; i < n; i++) d.tap(start + (uint32_t)i * intervalMs);
}

// ---------------------------------------------------------------------------
// Deck: tempo measurement
// ---------------------------------------------------------------------------

// 468.75ms between beats is exactly 128 BPM.
static void test_deck_measures_bpm(void) {
  Deck d;
  tapAt(d, 469, 8);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 128.0f, d.bpm);
}

static void test_deck_needs_two_taps(void) {
  Deck d;
  d.tap(1000);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, d.bpm);   // one tap gives no interval
  d.tap(1500);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 120.0f, d.bpm);
}

static void test_deck_counts_taps(void) {
  Deck d;
  tapAt(d, 500, 5);
  TEST_ASSERT_EQUAL_UINT32(5, d.tapCount);
}

static void test_deck_solid_after_enough_taps(void) {
  Deck d;
  tapAt(d, 500, 3);
  TEST_ASSERT_FALSE(d.solid());
  tapAt(d, 500, 8);
  TEST_ASSERT_TRUE(d.solid());
}

// The rolling window must not grow past MAX_INTERVALS.
static void test_deck_window_is_bounded(void) {
  Deck d;
  tapAt(d, 500, 40);
  TEST_ASSERT_EQUAL_UINT8(MAX_INTERVALS, d.count);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 120.0f, d.bpm);
}

// A single wild tap (missed or double beat) must not drag the readout with it.
// Regression test: the outlier used to clear the window and then get stored as
// its only sample, slamming a steady 120 BPM down to ~43.
static void test_deck_rejects_outlier(void) {
  Deck d;
  tapAt(d, 500, 6);                          // steady 120 BPM
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 120.0f, d.bpm);

  d.tap(1000 + 5 * 500 + 1400);              // fumbled tap, way late
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 120.0f, d.bpm);  // readout holds steady
  TEST_ASSERT_EQUAL_UINT8(0, d.count);            // window was reset
}

// ...and two clean taps afterwards re-establish the tempo, so the reset also
// serves the case where you really have moved to a faster record.
static void test_deck_recovers_after_outlier(void) {
  Deck d;
  tapAt(d, 500, 6);
  uint32_t t = 1000 + 5 * 500;
  d.tap(t + 1400);                           // outlier
  tapAt(d, 400, 4, t + 1400 + 400);          // clean 150 BPM
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 150.0f, d.bpm);
}

// A long pause means a new record: the next taps start a fresh measurement.
static void test_deck_idle_reset(void) {
  Deck d;
  tapAt(d, 500, 6);                            // 120 BPM
  uint32_t t = 1000 + 5 * 500;
  d.tap(t + IDLE_RESET_MS + 1000);             // long gap -> reset
  TEST_ASSERT_EQUAL_UINT32(1, d.tapCount);     // counter restarted
  tapAt(d, 400, 6, t + IDLE_RESET_MS + 1400);  // now 150 BPM
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 150.0f, d.bpm);
}

// Implausible intervals are ignored entirely.
static void test_deck_ignores_absurd_intervals(void) {
  Deck d;
  d.tap(1000);
  d.tap(1050);                                 // 50ms = 1200 BPM, rejected
  TEST_ASSERT_EQUAL_FLOAT(0.0f, d.bpm);
}

// ---------------------------------------------------------------------------
// Deck: confidence and beat prediction
// ---------------------------------------------------------------------------

static void test_jitter_unknown_before_two_intervals(void) {
  Deck d;
  d.tap(1000);
  TEST_ASSERT_TRUE(d.jitterMs() < 0.0f);
}

static void test_jitter_zero_when_perfectly_steady(void) {
  Deck d;
  tapAt(d, 500, 8);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, d.jitterMs());
}

static void test_jitter_rises_with_sloppy_tapping(void) {
  Deck steady, sloppy;
  tapAt(steady, 500, 6);

  // Same average, wobbly spacing.
  const uint32_t times[] = {1000, 1480, 2020, 2470, 3030, 3500};
  for (uint8_t i = 0; i < 6; i++) sloppy.tap(times[i]);

  TEST_ASSERT_TRUE(sloppy.jitterMs() > steady.jitterMs());
}

// The beat pulse should fire right on a predicted beat and be off between them.
static void test_beat_pulse_tracks_the_beat(void) {
  Deck d;
  tapAt(d, 500, 6);
  uint32_t last = 1000 + 5 * 500;
  TEST_ASSERT_TRUE(d.beatPulse(last + 1000));        // two beats later
  TEST_ASSERT_FALSE(d.beatPulse(last + 1250));       // half a beat off
}

static void test_beat_pulse_silent_without_tempo(void) {
  Deck d;
  TEST_ASSERT_FALSE(d.beatPulse(5000));
}

// ---------------------------------------------------------------------------
// Pitch maths
// ---------------------------------------------------------------------------

static void test_pitch_percent(void) {
  // 126 -> 128 needs +1.587%
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.587f, pitchPercent(126.0f, 128.0f));
  // and the reverse is negative
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.5625f, pitchPercent(128.0f, 126.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pitchPercent(128.0f, 128.0f));
}

static void test_pitch_percent_guards_zero(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pitchPercent(0.0f, 128.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pitchPercent(128.0f, 0.0f));
}

// ---------------------------------------------------------------------------
// Beatmatching
// ---------------------------------------------------------------------------

static void test_match_invalid_until_both_decks_measured(void) {
  TEST_ASSERT_FALSE(computeMatch(0.0f, 128.0f).valid);
  TEST_ASSERT_FALSE(computeMatch(128.0f, 0.0f).valid);
  TEST_ASSERT_TRUE(computeMatch(128.0f, 126.0f).valid);
}

static void test_match_same_octave(void) {
  MatchInfo m = computeMatch(126.0f, 128.0f);
  TEST_ASSERT_EQUAL_INT8(0, m.octave);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.587f, m.pct);
  TEST_ASSERT_FALSE(m.matched);
  TEST_ASSERT_FALSE(m.outOfRange);
}

static void test_match_reports_matched_within_tolerance(void) {
  MatchInfo m = computeMatch(128.0f, 128.02f);
  TEST_ASSERT_TRUE(m.matched);
}

// A 64 BPM record and a 128 BPM record are the same tempo an octave apart:
// the match should halve the target rather than demand +100% pitch.
static void test_match_detects_double_time(void) {
  MatchInfo m = computeMatch(64.0f, 128.0f);
  TEST_ASSERT_EQUAL_INT8(-1, m.octave);          // target halved
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 64.0f, m.target);
  TEST_ASSERT_TRUE(m.matched);
}

static void test_match_detects_half_time(void) {
  MatchInfo m = computeMatch(128.0f, 64.0f);
  TEST_ASSERT_EQUAL_INT8(1, m.octave);           // target doubled
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 128.0f, m.target);
  TEST_ASSERT_TRUE(m.matched);
}

// Octave choice should pick the smallest pitch move, not just any match.
static void test_match_picks_smallest_pitch_move(void) {
  MatchInfo m = computeMatch(130.0f, 66.0f);     // 66*2 = 132, only +1.5%
  TEST_ASSERT_EQUAL_INT8(1, m.octave);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 1.538f, m.pct);
}

// Beyond a turntable's pitch range the UI must warn instead of nudging.
static void test_match_flags_out_of_range(void) {
  MatchInfo m = computeMatch(100.0f, 128.0f);    // +28%, hopeless
  TEST_ASSERT_TRUE(m.outOfRange);
}

static void test_match_in_range_at_boundary(void) {
  TEST_ASSERT_FALSE(computeMatch(128.0f, 133.0f).outOfRange);  // ~+3.9%
}

// ---------------------------------------------------------------------------
// Drift timer — the headline feature, so pin the arithmetic down
// ---------------------------------------------------------------------------

// 0.5 BPM apart means one full beat of drift every 120 seconds.
static void test_drift_half_bpm_is_two_minutes(void) {
  MatchInfo m = computeMatch(127.5f, 128.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 120.0f, m.driftSec);
}

// 0.1 BPM apart buys you ten minutes.
static void test_drift_tenth_bpm_is_ten_minutes(void) {
  MatchInfo m = computeMatch(127.9f, 128.0f);
  TEST_ASSERT_FLOAT_WITHIN(5.0f, 600.0f, m.driftSec);
}

// Bigger error, less time — and it must never go negative for a real mismatch.
static void test_drift_shrinks_as_error_grows(void) {
  float near = computeMatch(127.5f, 128.0f).driftSec;
  float far  = computeMatch(126.0f, 128.0f).driftSec;
  TEST_ASSERT_TRUE(far < near);
  TEST_ASSERT_TRUE(far > 0.0f);
}

// A perfect match has no meaningful drift time; -1 signals "not applicable".
static void test_drift_not_applicable_when_identical(void) {
  MatchInfo m = computeMatch(128.0f, 128.0f);
  TEST_ASSERT_TRUE(m.driftSec < 0.0f);
}

// Drift is symmetric — it doesn't matter which deck is the faster one.
static void test_drift_is_symmetric(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.5f,
                           computeMatch(127.5f, 128.0f).driftSec,
                           computeMatch(128.5f, 128.0f).driftSec);
}

// ---------------------------------------------------------------------------
// End-to-end: tap two records, get usable beatmatch guidance
// ---------------------------------------------------------------------------
static void test_full_beatmatch_flow(void) {
  Deck a, b;
  tapAt(a, 469, 8);        // ~128 BPM
  tapAt(b, 476, 8);        // ~126 BPM

  MatchInfo m = computeMatch(b.bpm, a.bpm);   // match deck B up to deck A
  TEST_ASSERT_TRUE(m.valid);
  TEST_ASSERT_EQUAL_INT8(0, m.octave);
  TEST_ASSERT_TRUE(m.pct > 0.0f);             // B is slower, so speed it up
  TEST_ASSERT_FALSE(m.outOfRange);
  TEST_ASSERT_TRUE(m.driftSec > 0.0f);
}

// ---------------------------------------------------------------------------
int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_deck_measures_bpm);
  RUN_TEST(test_deck_needs_two_taps);
  RUN_TEST(test_deck_counts_taps);
  RUN_TEST(test_deck_solid_after_enough_taps);
  RUN_TEST(test_deck_window_is_bounded);
  RUN_TEST(test_deck_rejects_outlier);
  RUN_TEST(test_deck_recovers_after_outlier);
  RUN_TEST(test_deck_idle_reset);
  RUN_TEST(test_deck_ignores_absurd_intervals);

  RUN_TEST(test_jitter_unknown_before_two_intervals);
  RUN_TEST(test_jitter_zero_when_perfectly_steady);
  RUN_TEST(test_jitter_rises_with_sloppy_tapping);
  RUN_TEST(test_beat_pulse_tracks_the_beat);
  RUN_TEST(test_beat_pulse_silent_without_tempo);

  RUN_TEST(test_pitch_percent);
  RUN_TEST(test_pitch_percent_guards_zero);

  RUN_TEST(test_match_invalid_until_both_decks_measured);
  RUN_TEST(test_match_same_octave);
  RUN_TEST(test_match_reports_matched_within_tolerance);
  RUN_TEST(test_match_detects_double_time);
  RUN_TEST(test_match_detects_half_time);
  RUN_TEST(test_match_picks_smallest_pitch_move);
  RUN_TEST(test_match_flags_out_of_range);
  RUN_TEST(test_match_in_range_at_boundary);

  RUN_TEST(test_drift_half_bpm_is_two_minutes);
  RUN_TEST(test_drift_tenth_bpm_is_ten_minutes);
  RUN_TEST(test_drift_shrinks_as_error_grows);
  RUN_TEST(test_drift_not_applicable_when_identical);
  RUN_TEST(test_drift_is_symmetric);

  RUN_TEST(test_full_beatmatch_flow);

  return UNITY_END();
}
