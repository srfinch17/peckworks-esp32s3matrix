// ============================================================
// SECTION: FROSTBITE ANIMATION
// Shimmering mist backdrop with bright diamond sparkles.
// All 64 pixels always lit. Mist = fbColor scaled dim (range
// fbMistMax/2..fbMistMax). Sparkles = fbColor at full brightness.
// ============================================================

struct FbSpark {
  uint8_t pixIdx;  // logical row-major index 0-63
  uint8_t phase;   // 0-29: ramp up 0-7, hold 8-19, ramp down 20-29
  bool    active;
};

static uint8_t fbBri[64];    // per-pixel shimmer brightness
static int8_t  fbDir[64];    // drift direction: +1 or -1
static FbSpark fbSparks[8];
static bool    fbInit = false;

void runFrostbiteFrame() {
  uint8_t lo = max((uint8_t)8, (uint8_t)(fbMistMax >> 1));

  if (!fbInit) {
    for (int i = 0; i < 64; i++) {
      fbBri[i] = lo + (uint8_t)random(fbMistMax - lo + 1);
      fbDir[i] = random(2) ? 1 : -1;
    }
    for (int s = 0; s < 8; s++) fbSparks[s].active = false;
    fbInit = true;
  }

  // Shimmer: drift each pixel's brightness between lo and fbMistMax
  for (int i = 0; i < 64; i++) {
    if (random(4) == 0) {
      fbBri[i] += fbDir[i];
      if (fbBri[i] >= fbMistMax) { fbBri[i] = fbMistMax; fbDir[i] = -1; }
      if (fbBri[i] <= lo)        { fbBri[i] = lo;        fbDir[i] =  1; }
    }
  }

  // Draw mist: fbColor directly, scaled by shimmer brightness
  // No white blend — lets the actual hue show through clearly
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      CRGB c = fbColor;
      c.nscale8(fbBri[y * 8 + x]);
      setPixel(x, y, c);
    }
  }

  // Sparkle spawn
  if (random(100) < fbSparkRate) {
    for (int s = 0; s < 8; s++) {
      if (!fbSparks[s].active) {
        fbSparks[s].pixIdx = (uint8_t)random(64);
        fbSparks[s].phase  = 0;
        fbSparks[s].active = true;
        break;
      }
    }
  }

  // Draw sparkles: fbColor at full LED brightness — clearly above the mist
  for (int s = 0; s < 8; s++) {
    FbSpark& sp = fbSparks[s];
    if (!sp.active) continue;

    uint8_t bri;
    if      (sp.phase < 8)  bri = sp.phase * 32;
    else if (sp.phase < 20) bri = 255;
    else                    bri = (uint8_t)((29 - sp.phase) * 28);

    CRGB c = fbColor;
    c.nscale8(bri);
    setPixel(sp.pixIdx % 8, sp.pixIdx / 8, c);

    sp.phase++;
    if (sp.phase >= 30) sp.active = false;
  }
}
