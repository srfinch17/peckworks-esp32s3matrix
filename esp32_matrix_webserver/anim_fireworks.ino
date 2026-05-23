// ============================================================
// SECTION: FIREWORKS ANIMATION
// Single firework loop: white mortar → colorful radial burst → fade.
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
