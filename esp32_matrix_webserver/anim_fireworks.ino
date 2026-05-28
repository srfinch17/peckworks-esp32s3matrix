// ============================================================
// SECTION: FIREWORKS ANIMATIONS (1 + 2)
// FW1: white mortar → colorful radial burst → fade.
// FW2: same launch, brighter bloom burst, colored tip + white comet tail per tendril.
// ============================================================

enum FwPhase : uint8_t { FW_IDLE, FW_LAUNCH, FW_EXPLODE, FW_FADE };

struct FwTendril {
  float   x, y;
  float   dx, dy;
  uint8_t brightness;
  bool    active;
};

static FwPhase   fwPhase         = FW_IDLE;
static uint32_t  fwIdleStartMs   = 0;
static float     fwMortarX, fwMortarY;
static float     fwMortarDx, fwMortarDy;
static uint8_t   fwExplodeY;
static uint8_t   fwFlashFrames   = 0;
static FwTendril fwTendrils[12];

// Map brightness (255→0) to a color cycling color1→color2→color3→black
static CRGB fwTendrilColor(uint8_t bri) {
  if (bri > 170) return blend(fwColor2, fwColor1, (uint8_t)map(bri, 170, 255, 0, 255));
  if (bri >  85) return blend(fwColor3, fwColor2, (uint8_t)map(bri,  85, 170, 0, 255));
  return blend(CRGB::Black, fwColor3,  (uint8_t)map(bri,   0,  85, 0, 255));
}

void stepFireworksFrame() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  if (fwPhase == FW_IDLE) {
    if (millis() - fwIdleStartMs >= 700) {
      fwMortarX  = (float)(2 + random(5));    // cols 2-6
      fwMortarY  = 7.0f;
      fwMortarDx = (float)(random(3) - 1) * 0.25f;
      fwMortarDy = -(0.8f + random(5) * 0.08f);
      fwExplodeY = 2 + random(4);             // explodes rows 2-5
      fwPhase    = FW_LAUNCH;
    }
    return;
  }

  if (fwPhase == FW_LAUNCH) {
    fwMortarX += fwMortarDx;
    fwMortarY += fwMortarDy;
    if ((int)fwMortarY <= (int)fwExplodeY) {
      // Spawn tendrils
      for (int i = 0; i < 12; i++) {
        float angle = i * (2.0f * PI / 12.0f) + random(30) * (PI / 180.0f);
        float speed = 0.35f + random(4) * 0.08f;
        fwTendrils[i] = { fwMortarX, fwMortarY, cosf(angle)*speed, sinf(angle)*speed, 255, true };
      }
      fwFlashFrames = 2;
      fwPhase = FW_EXPLODE;
    } else {
      setPixel((int)fwMortarX, (int)fwMortarY, CRGB::White);
    }
    return;
  }

  if (fwPhase == FW_EXPLODE) {
    setPixel((int)fwMortarX, (int)fwMortarY, fwColor1);
    if (--fwFlashFrames == 0) fwPhase = FW_FADE;
    return;
  }

  // FW_FADE
  bool anyActive = false;
  for (int i = 0; i < 12; i++) {
    FwTendril& t = fwTendrils[i];
    if (!t.active) continue;
    anyActive = true;
    t.x += t.dx;
    t.y += t.dy;
    if (t.brightness > 12) t.brightness -= 12; else { t.active = false; continue; }
    if (t.x < 0 || t.x > 7 || t.y < 0 || t.y > 7) { t.active = false; continue; }
    setPixel((int)t.x, (int)t.y, fwTendrilColor(t.brightness));
  }
  if (!anyActive) {
    fwPhase       = FW_IDLE;
    fwIdleStartMs = millis();
  }
}

// ── Fireworks 2 ───────────────────────────────────────────────────────────────
// Colored tip + dimming white tail per tendril (inverse of matrix rain).
// Brighter cross-bloom at burst. Mortar always travels ≥2 visible rows.

static FwPhase   fw2Phase         = FW_IDLE;
static uint32_t  fw2IdleStartMs   = 0;
static float     fw2MortarX, fw2MortarY;
static float     fw2MortarDx, fw2MortarDy;
static uint8_t   fw2ExplodeY;
static uint8_t   fw2FlashFrames   = 0;
static FwTendril fw2Tendrils[12];

#define FW2_TAIL 4

static CRGB fw2TendrilColor(uint8_t bri) {
  if (bri > 170) return blend(fw2Color2, fw2Color1, (uint8_t)map(bri, 170, 255, 0, 255));
  if (bri >  85) return blend(fw2Color3, fw2Color2, (uint8_t)map(bri,  85, 170, 0, 255));
  return blend(CRGB::Black, fw2Color3,  (uint8_t)map(bri,   0,  85, 0, 255));
}

void stepFireworks2Frame() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  if (fw2Phase == FW_IDLE) {
    if (millis() - fw2IdleStartMs >= 700) {
      fw2MortarX  = (float)(2 + random(5));
      fw2MortarY  = 7.0f;
      fw2MortarDx = (float)(random(3) - 1) * 0.25f;
      fw2MortarDy = -(0.8f + random(5) * 0.08f);
      fw2ExplodeY = 1 + random(3);   // rows 1-3: mortar always travels ≥2 visible frames
      fw2Phase    = FW_LAUNCH;
    }
    return;
  }

  if (fw2Phase == FW_LAUNCH) {
    fw2MortarX += fw2MortarDx;
    fw2MortarY += fw2MortarDy;
    if ((int)fw2MortarY <= (int)fw2ExplodeY) {
      for (int i = 0; i < 12; i++) {
        float angle = i * (2.0f * PI / 12.0f) + random(30) * (PI / 180.0f);
        float speed = 0.35f + random(4) * 0.08f;
        fw2Tendrils[i] = { fw2MortarX, fw2MortarY, cosf(angle)*speed, sinf(angle)*speed, 255, true };
      }
      fw2FlashFrames = 1;
      fw2Phase = FW_EXPLODE;
    } else {
      setPixel((int)fw2MortarX, (int)fw2MortarY, CRGB::White);
    }
    return;
  }

  if (fw2Phase == FW_EXPLODE) {
    // Cross-bloom: bright white center + dimmer neighbors
    int cx = (int)fw2MortarX, cy = (int)fw2MortarY;
    setPixel(cx,   cy,   CRGB::White);
    setPixel(cx+1, cy,   CRGB(200, 200, 200));
    setPixel(cx-1, cy,   CRGB(200, 200, 200));
    setPixel(cx,   cy+1, CRGB(200, 200, 200));
    setPixel(cx,   cy-1, CRGB(200, 200, 200));
    if (--fw2FlashFrames == 0) fw2Phase = FW_FADE;
    return;
  }

  // FW_FADE: white comet tail (dimmest → brightest toward tip), colored tip on top
  bool anyActive = false;
  for (int i = 0; i < 12; i++) {
    FwTendril& t = fw2Tendrils[i];
    if (!t.active) continue;
    anyActive = true;
    t.x += t.dx;
    t.y += t.dy;
    if (t.brightness > 12) t.brightness -= 12; else { t.active = false; continue; }
    if (t.x < 0 || t.x > 7 || t.y < 0 || t.y > 7) { t.active = false; continue; }

    // Draw tail oldest-first so the closest segment wins on pixel collisions
    for (int j = FW2_TAIL; j >= 1; j--) {
      float tx = t.x - j * t.dx;
      float ty = t.y - j * t.dy;
      if (tx < 0 || tx > 7 || ty < 0 || ty > 7) continue;
      uint8_t dimness = (uint8_t)((uint16_t)t.brightness * (FW2_TAIL + 1 - j) / (FW2_TAIL + 2));
      CRGB tailC = CRGB::White;
      tailC.nscale8(dimness);
      setPixel((int)tx, (int)ty, tailC);
    }
    // Colored tip overwrites any tail pixel at same coordinate
    setPixel((int)t.x, (int)t.y, fw2TendrilColor(t.brightness));
  }
  if (!anyActive) {
    fw2Phase       = FW_IDLE;
    fw2IdleStartMs = millis();
  }
}

