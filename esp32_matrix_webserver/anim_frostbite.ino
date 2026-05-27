// ============================================================
// SECTION: FROSTBITE ANIMATION
// Shimmering pale mist backdrop with bright diamond sparkles.
// All 64 pixels are always lit (no black background).
// Base color drives both mist (desaturated) and sparkle (bright).
// ============================================================

struct FbSpark {
  uint8_t pixIdx;  // logical row-major index 0-63
  uint8_t phase;   // 0-29: 0-7 ramp up, 8-19 hold bright, 20-29 ramp down
  bool    active;
};

static uint8_t fbBri[64];    // per-pixel shimmer brightness [30..90]
static int8_t  fbDir[64];    // drift direction: +1 or -1
static FbSpark fbSparks[8];
static bool    fbInit = false;

void runFrostbiteFrame() {
  if (!fbInit) {
    for (int i = 0; i < 64; i++) {
      fbBri[i] = 30 + (uint8_t)random(60);
      fbDir[i] = random(2) ? 1 : -1;
    }
    for (int s = 0; s < 8; s++) fbSparks[s].active = false;
    fbInit = true;
  }

  // Shimmer: slowly drift each pixel's brightness independently
  for (int i = 0; i < 64; i++) {
    if (random(4) == 0) {
      fbBri[i] += fbDir[i];
      if (fbBri[i] >= 90) { fbBri[i] = 90; fbDir[i] = -1; }
      if (fbBri[i] <= 30) { fbBri[i] = 30; fbDir[i] =  1; }
    }
  }

  // Mist base: blend fbColor 30% + white 70% → pale tinted mist
  CRGB mistBase;
  mistBase.r = (uint8_t)(fbColor.r * 30 / 100 + 178);  // 255 * 0.7 ≈ 178
  mistBase.g = (uint8_t)(fbColor.g * 30 / 100 + 178);
  mistBase.b = (uint8_t)(fbColor.b * 30 / 100 + 178);

  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      CRGB c = mistBase;
      c.nscale8(fbBri[y * 8 + x]);
      setPixel(x, y, c);
    }
  }

  // Sparkle spawn: fbSparkRate is 0-100 probability check per frame
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

  // Sparkle base: blend fbColor 40% + white 60% → bright diamond tint
  CRGB sparkBase;
  sparkBase.r = (uint8_t)(fbColor.r * 40 / 100 + 153);  // 255 * 0.6 ≈ 153
  sparkBase.g = (uint8_t)(fbColor.g * 40 / 100 + 153);
  sparkBase.b = (uint8_t)(fbColor.b * 40 / 100 + 153);

  for (int s = 0; s < 8; s++) {
    FbSpark& sp = fbSparks[s];
    if (!sp.active) continue;

    uint8_t bri;
    if      (sp.phase < 8)  bri = sp.phase * 32;
    else if (sp.phase < 20) bri = 255;
    else                    bri = (uint8_t)((29 - sp.phase) * 28);  // 252 → 0

    CRGB c = sparkBase;
    c.nscale8(bri);
    setPixel(sp.pixIdx % 8, sp.pixIdx / 8, c);

    sp.phase++;
    if (sp.phase >= 30) sp.active = false;
  }
}
