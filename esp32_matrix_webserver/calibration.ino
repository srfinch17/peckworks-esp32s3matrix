// calibration.ino — Phase 3 active correction pipeline.
//
// Loads the measured profile (data/calibration.json, produced by the Calibration
// Lab) and applies floor-lift -> white-balance -> gamma at the single show()
// chokepoint matrixShow(). Identity fallback on absence/parse-failure so the
// panel never breaks. The struct/globals and the function prototypes live in the
// MAIN ino (single-TU concatenation order); this file holds only the bodies.
//
// Why save/restore: animations write AND read leds[] (fades, trails). Correcting
// leds[] permanently would compound the correction every frame. So matrixShow()
// stashes the uncorrected leds[] in ledsBackup[], corrects leds[] in place, shows,
// then restores — the panel sees the correction, the next animation frame reads
// the original values. FastLED stays registered to leds[], so any FastLED.show()
// NOT routed through matrixShow() (boot status colors, the grid-test/Calibration
// Lab patterns) simply shows the RAW panel — which is exactly what those want.
// Correction is value-domain and runs BEFORE FastLED's global brightness scaling
// ((c*(bri+1))>>8) at show() time.

void buildGammaLUT() {
  for (int v = 0; v < 256; v++) {
    if (v == 0 || calib.gamma == 1.0f) { gammaLUT[v] = (uint8_t)v; continue; }
    float f = powf((float)v / 255.0f, calib.gamma);
    gammaLUT[v] = (uint8_t)constrain((int)lroundf(f * 255.0f), 0, 255);
  }
}

void loadCalibration() {
  calib = CalibrationProfile();   // identity defaults (member initializers)
  if (LittleFS.exists("/calibration.json")) {
    File f = LittleFS.open("/calibration.json", "r");
    if (f) {
      JsonDocument doc;
      if (deserializeJson(doc, f) == DeserializationError::Ok) {
        calib.floorR = (uint8_t)constrain((int)(doc["floors"]["r"] | 1), 1, 255);
        calib.floorG = (uint8_t)constrain((int)(doc["floors"]["g"] | 1), 1, 255);
        calib.floorB = (uint8_t)constrain((int)(doc["floors"]["b"] | 1), 1, 255);
        calib.gainR  = constrain((float)(doc["white_balance"]["r"] | 1.0), 0.0f, 1.0f);
        calib.gainG  = constrain((float)(doc["white_balance"]["g"] | 1.0), 0.0f, 1.0f);
        calib.gainB  = constrain((float)(doc["white_balance"]["b"] | 1.0), 0.0f, 1.0f);
        calib.gamma  = constrain((float)(doc["gamma"] | 1.0), 0.1f, 4.0f);
      }
      f.close();
    }
  }
  buildGammaLUT();
  Serial.printf("Calibration: floors(%u,%u,%u) gains(%.3f,%.3f,%.3f) gamma=%.2f\n",
                calib.floorR, calib.floorG, calib.floorB,
                calib.gainR, calib.gainG, calib.gainB, calib.gamma);
}

// Lift a nonzero channel up to its visibility floor. Floors are 1 for this panel
// (so this is inert), but kept general for other boards/binning.
static inline uint8_t liftFloor(uint8_t c, uint8_t floor) {
  return (c > 0 && c < floor) ? floor : c;
}

// In-place correction on a buffer: floor-lift -> white-balance -> gamma (LUT).
void applyCalibration(CRGB* buf) {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t r = liftFloor(buf[i].r, calib.floorR);
    uint8_t g = liftFloor(buf[i].g, calib.floorG);
    uint8_t b = liftFloor(buf[i].b, calib.floorB);
    r = (uint8_t)(r * calib.gainR);
    g = (uint8_t)(g * calib.gainG);
    b = (uint8_t)(b * calib.gainB);
    buf[i].r = gammaLUT[r];
    buf[i].g = gammaLUT[g];
    buf[i].b = gammaLUT[b];
  }
}

// The single show() chokepoint for corrected content. Stash leds[], correct it in
// place, show, restore — so read-back animations never compound. With the setting
// OFF it is a plain FastLED.show(), bit-identical to pre-Phase-3 behavior.
void matrixShow() {
  if (settings.calibCorrection) {
    memcpy(ledsBackup, leds, sizeof(leds));
    applyCalibration(leds);
    FastLED.show();
    memcpy(leds, ledsBackup, sizeof(leds));
  } else {
    FastLED.show();
  }
}
