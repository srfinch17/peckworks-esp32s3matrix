// ============================================================
// SECTION: FROSTBITE ANIMATION
// Shimmering mist backdrop with bright diamond sparkles.
// All 64 pixels always lit. Mist = fbColor scaled dim (range
// fbMistMax/2..fbMistMax). Sparkles = fbColor at full brightness.
// ============================================================

struct FbSpark {
  uint8_t pixIdx;  // logical row-major index 0-63
  uint8_t phase;   // 0-39: sine bell fade-in/out over 40 frames
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

  // Shimmer: every pixel drifts every frame — no skipping, so motion is continuous.
  // Rare random direction flip (~3%) keeps pixels from moving in lockstep.
  for (int i = 0; i < 64; i++) {
    if (random(30) == 0) fbDir[i] = -fbDir[i];
    int next = (int)fbBri[i] + fbDir[i];
    if (next >= (int)fbMistMax) { fbBri[i] = fbMistMax; fbDir[i] = -1; }
    else if (next <= (int)lo)   { fbBri[i] = lo;        fbDir[i] =  1; }
    else                        { fbBri[i] = (uint8_t)next; }
  }

  // Draw mist: fbColor scaled by per-pixel shimmer brightness
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

  // Draw sparkles: smooth sine bell fade-in/out over 40 phases (was 30 linear steps).
  // Sine curve eliminates the visible brightness-step "chunking" of the old linear ramp.
  for (int s = 0; s < 8; s++) {
    FbSpark& sp = fbSparks[s];
    if (!sp.active) continue;

    uint8_t bri = (uint8_t)(sinf(sp.phase * 3.14159265f / 39.0f) * 255.0f);
    if (bri > 0) {
      CRGB c = fbColor;
      c.nscale8(bri);
      setPixel(sp.pixIdx % 8, sp.pixIdx / 8, c);
    }

    sp.phase++;
    if (sp.phase >= 40) sp.active = false;
  }
}
