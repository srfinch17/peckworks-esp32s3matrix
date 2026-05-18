// ============================================================
// SECTION 7: SIMPLE ANIMATIONS
//
// Four lean animations that prove you don't need a lot of code
// to make something look great on an LED matrix.
//
// All use millis()-based timing — no delay() calls anywhere.
// The main loop() calls one of these every animationSpeed ms.
// FastLED.show() is called by loop() AFTER the frame function
// returns, so these functions just update leds[] and exit.
// ============================================================

// These are static locals instead of globals — they only need
// to exist between frames, and making them static here keeps
// them out of the global namespace.
static uint8_t rainbowHue   = 0;   // advances each frame → full spectrum over time
static uint8_t breathePhase = 0;   // drives the sine wave phase for the pulse
static uint8_t waveOffset   = 0;   // shifts the wave left/right each frame

// ── Rainbow ───────────────────────────────────────────────────
// fill_rainbow() is a FastLED built-in. It sets each LED to a
// different hue, distributed evenly across the hue wheel (0-255).
// The third arg (7) is the hue step between adjacent LEDs.
// Incrementing rainbowHue each frame rotates the entire spectrum.
void runRainbowFrame() {
  fill_rainbow(leds, NUM_LEDS, rainbowHue, 7);
  rainbowHue += 3;   // higher = faster spin
}

// ── Breathe ───────────────────────────────────────────────────
// Fills with solidColor (set by handleAnimation), then dims all
// LEDs by a sine-wave brightness value so they pulse in and out.
//
// beatsin8(bpm, lo, hi, timebase, phase_offset) is a FastLED
// helper that returns an 8-bit sine wave timed to a BPM value.
// At 20 BPM it cycles once every 3 seconds — gentle breathing pace.
//
// nscale8(val) scales each LED's brightness by val/255.
// Calling it on the already-filled solid color dims the whole
// matrix without changing the hue.
void runBreatheFrame() {
  uint8_t level = beatsin8(20, 10, 255, 0, breathePhase);
  fill_solid(leds, NUM_LEDS, solidColor);
  for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(level);
  breathePhase += 2;
}

// ── Wave ──────────────────────────────────────────────────────
// Simulates a blue wave rolling across the matrix.
//
// For each column x, beatsin8() returns a wave height waveH
// (0-7) that is phase-shifted by the column's position so
// adjacent columns peak at different times — this is what
// creates the "wave moving across" look rather than all columns
// bobbing in sync.
//
// The waveOffset term (incremented each frame) shifts the
// phase of the entire pattern, making the wave travel.
//
// Pixels at or below the wave surface get a blue shade that
// gets darker as depth increases (map() from 255 at the surface
// down to 60 at the bottom). Pixels above the surface are black.
void runWaveFrame() {
  for (int x = 0; x < MATRIX_W; x++) {
    // waveH: how many rows from the bottom are "underwater"
    uint8_t waveH = beatsin8(20, 0, MATRIX_H - 1, 0, x * 32 + waveOffset);
    for (int y = 0; y < MATRIX_H; y++) {
      if (y >= MATRIX_H - 1 - waveH) {
        // brighter at the surface (low y index = near bottom of wave), darker toward top
        uint8_t b = map(y, 0, MATRIX_H - 1, 255, 60);
        setPixel(x, y, CRGB(0, 0, b));
      } else {
        setPixel(x, y, CRGB::Black);
      }
    }
  }
  waveOffset += 2;
}

// ── Solid ─────────────────────────────────────────────────────
// Fills the entire matrix with a single static color.
// solidColor is set when handleAnimation() receives the request.
// This frame function is called repeatedly (redundantly) but
// the animation loop needs it so the web server stays responsive
// while the display stays on — we never block in loop().
void runSolidFrame() {
  fill_solid(leds, NUM_LEDS, solidColor);
}
