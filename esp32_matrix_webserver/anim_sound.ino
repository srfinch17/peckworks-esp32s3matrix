// ============================================================
// SECTION: SOUND / VIBRATION VISUALIZER
//
// There is NO microphone on this board. The QMI8658C accelerometer,
// however, picks up low-frequency vibration — so if the board sits on
// or near a speaker, the bass physically shakes it. This mode turns
// that shaking into a VU-style bar that dances to the beat.
//
// HOW IT WORKS:
//   1. magnitude = |accel vector|. At rest this is ~1g.
//   2. A slow-tracked baseline follows the steady magnitude (gravity +
//      resting orientation), so we measure DEVIATION from steady state.
//   3. Deviation × sensitivity → an energy level (fast attack, slow
//      release) drawn as a bar rising from the bottom, gradient
//      soundColor1 (bottom) → soundColor2 (top), with a decaying peak line.
//
// Honest scope: this is a beat/energy visualizer, not a frequency
// equalizer — the IMU can't resolve audio frequencies (see docs/ROADMAP.md).
// ============================================================

void stepSoundFrame() {
  float ax, ay, az;
  if (imuReady) readAccel(ax, ay, az);
  else          { ax = 0.0f; ay = 0.0f; az = 1.0f; }   // flat fallback

  float mag = sqrtf(ax * ax + ay * ay + az * az);

  // dt since last frame (seconds). Attack/release/decay are wall-clock based so
  // they don't change when the animation tick rate (animationSpeed) changes.
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  float dt = lastMs ? (now - lastMs) / 1000.0f : 0.016f;
  lastMs = now;

  // Slowly track the steady-state magnitude so gravity/orientation is removed
  // (~1.5s settle, so a sustained bass note lingers before the AC-coupling fades it).
  soundBaseline += (mag - soundBaseline) * (1.0f - expf(-dt / 1.5f));
  float dev = fabsf(mag - soundBaseline);

  // sensitivity 0-10 → gain. Higher sensitivity reacts to gentler vibration.
  float gain   = 6.0f + soundSensitivity * 8.0f;
  float target = constrain(dev * gain, 0.0f, 1.0f);

  // Fast attack (instant), time-based release (~120ms) — real VU-meter feel.
  if (target > soundEnergy) soundEnergy = target;
  else                      soundEnergy += (target - soundEnergy) * (1.0f - expf(-dt / 0.12f));

  // Peak hold with time-based decay (~0.6 per second).
  if (soundEnergy > soundPeak) soundPeak = soundEnergy;
  else { soundPeak -= 0.6f * dt; if (soundPeak < 0.0f) soundPeak = 0.0f; }

  fill_solid(leds, NUM_LEDS, CRGB::Black);

  // Bar rising from the bottom.
  int h = (int)(soundEnergy * MATRIX_H + 0.5f);
  for (int y = 0; y < h; y++) {
    int   row = MATRIX_H - 1 - y;                       // bottom → up
    float tcol = (MATRIX_H > 1) ? (float)y / (float)(MATRIX_H - 1) : 0.0f;
    CRGB  col  = blendColors(soundColor1, soundColor2, tcol);
    for (int x = 0; x < MATRIX_W; x++) setPixel(x, row, col);
  }

  // Decaying peak marker (white line).
  int ph = (int)(soundPeak * MATRIX_H + 0.5f);
  if (ph > 0 && ph <= MATRIX_H) {
    int prow = MATRIX_H - ph;
    for (int x = 0; x < MATRIX_W; x++) setPixel(x, prow, CRGB::White);
  }
}
