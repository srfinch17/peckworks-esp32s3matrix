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
// Full-spectrum mode: 8 vertical hue stripes cycling through the FastLED hue wheel.
// Palette mode: same structure but blends through 4 user-chosen colors instead of hues.

// Blends through rainbowPalColors[4] across 256 steps (64 steps per colour segment).
static CRGB blendRainbowPal(uint8_t t) {
  uint8_t seg  = t >> 6;           // 0-3: which of the 4 colour segments
  uint8_t frac = (t & 63) << 2;    // 0-252: blend fraction within segment
  return blend(rainbowPalColors[seg], rainbowPalColors[(seg + 1) & 3], frac);
}

void runRainbowFrame() {
  uint8_t advance = (uint8_t)max(1, min(10, (int)(400 / max(animationSpeed, (uint32_t)66))));
  rainbowHue += advance;
  for (int x = 0; x < MATRIX_W; x++) {
    uint8_t hue = rainbowHue + (uint8_t)(x * 32);
    CRGB c = rainbowUsePalette ? blendRainbowPal(hue) : CHSV(hue, 255, 200);
    for (int y = 0; y < MATRIX_H; y++) setPixel(x, y, c);
  }
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
// Rolling wave using waveColor1 (surface) and waveColor2 (depth).
// Each column gets a phase-shifted beatsin8() wave height so adjacent
// columns peak at different times. waveOffset shifts the whole pattern.
void runWaveFrame() {
  for (int x = 0; x < MATRIX_W; x++) {
    uint8_t waveH   = beatsin8(20, 0, MATRIX_H - 1, 0, x * 32 + waveOffset);
    int     surface = MATRIX_H - 1 - (int)waveH;
    for (int y = 0; y < MATRIX_H; y++) {
      if (y >= surface) {
        // t=0 at wave surface → waveColor1 (bright); t=255 at deepest → waveColor2 (dim)
        uint8_t t = (waveH == 0) ? 0 : (uint8_t)(((uint16_t)(y - surface) * 255) / waveH);
        setPixel(x, y, blend(waveColor1, waveColor2, t));
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
