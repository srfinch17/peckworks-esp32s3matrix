// ============================================================
// SECTION 7: OTHER ANIMATIONS
// All use millis()-based timing — no delay() calls.
// ============================================================

static uint8_t rainbowHue   = 0;
static uint8_t breathePhase = 0;
static uint8_t waveOffset   = 0;

void runRainbowFrame() {
  fill_rainbow(leds, NUM_LEDS, rainbowHue, 7);
  rainbowHue += 3;
}

void runBreatheFrame() {
  uint8_t level = beatsin8(20, 10, 255, 0, breathePhase);
  fill_solid(leds, NUM_LEDS, solidColor);
  for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(level);
  breathePhase += 2;
}

void runWaveFrame() {
  for (int x = 0; x < MATRIX_W; x++) {
    uint8_t waveH = beatsin8(20, 0, MATRIX_H - 1, 0, x * 32 + waveOffset);
    for (int y = 0; y < MATRIX_H; y++) {
      if (y >= MATRIX_H - 1 - waveH) {
        uint8_t b = map(y, 0, MATRIX_H - 1, 255, 60);
        setPixel(x, y, CRGB(0, 0, b));
      } else {
        setPixel(x, y, CRGB::Black);
      }
    }
  }
  waveOffset += 2;
}

void runSolidFrame() {
  fill_solid(leds, NUM_LEDS, solidColor);
}
