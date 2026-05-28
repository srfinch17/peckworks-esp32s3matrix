// ============================================================
// SECTION: COMET ANIMATION
// Bobbing comet at the right edge with wave tail and sparks.
// ============================================================

struct CometSpark {
  float   x, y;
  float   dx, dy;
  uint8_t brightness;
  bool    active;
};

static float      cometYHist[8]   = {3,3,3,3,3,3,3,3};
static uint8_t    cometHistIdx    = 0;
static float      cometPhase      = 0.0f;
static CometSpark cometSparks[6];
static bool       cometInit       = false;

// Returns the cometY from `n` frames ago (0 = most recent stored value)
static float cometGetHistY(int n) {
  return cometYHist[(cometHistIdx + 16 - 1 - n) % 8];
}

// Draws one tail column: x col, history depth, row span relative to histY, color, brightness
static void drawCometCol(int x, int histN, int rowOff, int rowCount, CRGB color, uint8_t bri) {
  int baseY = (int)cometGetHistY(histN);
  CRGB c = color; c.nscale8(bri);
  for (int r = baseY + rowOff; r < baseY + rowOff + rowCount; r++)
    setPixel(x, r, c);
}

void runCometFrame() {
  if (!cometInit) {
    for (int i = 0; i < 6; i++) cometSparks[i].active = false;
    cometInit = true;
  }

  fill_solid(leds, NUM_LEDS, CRGB::Black);

  // Advance bob: ±2px around row 3
  cometPhase += 0.10f;
  float cY = 3.0f + sinf(cometPhase) * 2.0f;

  // Store in ring buffer, then increment index
  cometYHist[cometHistIdx] = cY;
  cometHistIdx = (cometHistIdx + 1) % 8;

  int iy = (int)cY;

  // Heart: 2×2 at x=6-7, rows iy and iy+1 — color1, full brightness
  setPixel(6, iy,   cometColor1); setPixel(7, iy,   cometColor1);
  setPixel(6, iy+1, cometColor1); setPixel(7, iy+1, cometColor1);

  // Tail columns: uniform 4 rows each (-1 to +2 relative to histY), color transitions outward
  drawCometCol(5, 1, -1, 4, cometColor2, 192);
  drawCometCol(4, 2, -1, 4, cometColor3, 140);
  drawCometCol(3, 3, -1, 4, cometColor3, 102);
  drawCometCol(2, 4, -1, 4, cometColor4,  64);

  // Sparks: ~5% chance per frame
  if (random(20) == 0) {
    for (int s = 0; s < 6; s++) {
      if (!cometSparks[s].active) {
        cometSparks[s].x          = 5.0f;
        cometSparks[s].y          = cY + (float)random(2);
        cometSparks[s].dx         = -(0.4f + random(4) * 0.15f);
        cometSparks[s].dy         = (float)(random(5) - 2) * 0.15f;
        cometSparks[s].brightness = 220;
        cometSparks[s].active     = true;
        break;
      }
    }
  }
  for (int s = 0; s < 6; s++) {
    CometSpark& sp = cometSparks[s];
    if (!sp.active) continue;
    sp.x += sp.dx;
    sp.y += sp.dy;
    if (sp.brightness > 35) sp.brightness -= 35; else { sp.active = false; continue; }
    if (sp.x < 0 || sp.y < 0 || sp.y > 7) { sp.active = false; continue; }
    CRGB c = cometColor3; c.nscale8(sp.brightness);
    setPixel((int)sp.x, (int)sp.y, c);
  }
}
