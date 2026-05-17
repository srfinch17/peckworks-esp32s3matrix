// ============================================================
// SECTION 6: FIRE ANIMATION
//
// HOW IT WORKS:
//   Each LED has a "heat" value from 0 (cold) to 255 (hot).
//   Each frame:
//     1. All cells cool down slightly (random cooling)
//     2. Heat rises upward (each cell absorbs heat from below)
//     3. Random sparks ignite at the bottom row
//     4. Heat values are mapped to colors via a color palette
//
// The "tendrils" parameter makes fire go wispy instead of broad.
// The "intensity" parameter controls how hot the bottom sparks get.
// ============================================================

// Four color palettes — each has 8 color stops from cold (index 0) to hot (index 7).
const uint8_t PALETTE_CLASSIC[][3] PROGMEM = {
  {  0,   0,   0}, {  0,   0,   0}, {160,   0,   0}, {255,  50,   0},
  {255, 170,   0}, {255, 230,   0}, {255, 255, 120}, {255, 255, 230}
};
const uint8_t PALETTE_BLUE[][3] PROGMEM = {
  {  0,   0,   0}, {  0,   0,   0}, {  0,   0, 160}, {  0,  40, 255},
  {  0, 160, 255}, {  0, 230, 255}, {120, 245, 255}, {230, 250, 255}
};
const uint8_t PALETTE_GREEN[][3] PROGMEM = {
  {  0,   0,   0}, {  0,   0,   0}, {  0, 130,   0}, { 40, 240,   0},
  {130, 255,   0}, {210, 255,  50}, {240, 255, 160}, {250, 255, 230}
};
const uint8_t PALETTE_PURPLE[][3] PROGMEM = {
  {  0,   0,   0}, {  0,   0,   0}, { 90,   0, 150}, {180,   0, 255},
  {230,  50, 255}, {245, 140, 255}, {255, 210, 255}, {255, 240, 255}
};
#define PALETTE_SIZE 8

const uint8_t (*activePalette)[3] = PALETTE_CLASSIC;

uint8_t fireHeat[NUM_LEDS];
int8_t  columnDrift[MATRIX_W];
uint8_t columnActive[MATRIX_W];

uint8_t fireIntensity = 6;
uint8_t fireTendrils  = 0;

#define MAX_SPARKS 8

struct Spark {
  float   x, y;
  float   vy;
  float   vx;
  uint8_t life;
  uint8_t maxLife;
};

Spark   sparks[MAX_SPARKS];
uint8_t sparkRate = 0;

CRGB heatToColor(uint8_t h) {
  float t   = h / 255.0f;
  float idx = t * (PALETTE_SIZE - 1);
  uint8_t lo  = (uint8_t)idx;
  uint8_t hi  = min((uint8_t)(lo + 1), (uint8_t)(PALETTE_SIZE - 1));
  float   frac = idx - lo;

  uint8_t r = pgm_read_byte(&activePalette[lo][0]) + (pgm_read_byte(&activePalette[hi][0]) - pgm_read_byte(&activePalette[lo][0])) * frac;
  uint8_t g = pgm_read_byte(&activePalette[lo][1]) + (pgm_read_byte(&activePalette[hi][1]) - pgm_read_byte(&activePalette[lo][1])) * frac;
  uint8_t b = pgm_read_byte(&activePalette[lo][2]) + (pgm_read_byte(&activePalette[hi][2]) - pgm_read_byte(&activePalette[lo][2])) * frac;
  return CRGB(r, g, b);
}

void initSparks() {
  memset(sparks, 0, sizeof(sparks));
}

void stepSparks() {
  for (int i = 0; i < MAX_SPARKS; i++) {
    if (sparks[i].life > 0) continue;
    if (random8(100) >= sparkRate * 10) break;

    int spawnX = random8(MATRIX_W);
    int spawnY = -1;
    for (int ty = 0; ty < MATRIX_H; ty++) {
      if (fireHeat[ty * MATRIX_W + spawnX] >= 25) { spawnY = ty; break; }
    }
    if (spawnY < 0) break;

    uint8_t life  = 6 + random8(7);
    float   minVy = ((float)spawnY + 2.0f) / (float)life;
    sparks[i].x       = (float)spawnX;
    sparks[i].y       = (float)spawnY;
    sparks[i].vy      = -(minVy + random8(3) * 0.05f);
    sparks[i].vx      = (random8(5) - 2) * 0.10f;
    sparks[i].maxLife = life;
    sparks[i].life    = life;
    break;
  }

  for (int i = 0; i < MAX_SPARKS; i++) {
    if (sparks[i].life == 0) continue;

    sparks[i].y += sparks[i].vy;
    sparks[i].x += sparks[i].vx;
    sparks[i].life--;

    if (sparks[i].y < -1.0f) { sparks[i].life = 0; continue; }

    if (sparks[i].x < 0.0f || sparks[i].x >= (float)MATRIX_W) {
      sparks[i].vx = -sparks[i].vx;
      sparks[i].x  = constrain(sparks[i].x, 0.0f, (float)(MATRIX_W - 1));
    }

    int px = (int)roundf(sparks[i].x);
    int py = (int)roundf(sparks[i].y);
    if (py < 0 || py >= MATRIX_H) continue;

    float   t    = (float)sparks[i].life / (float)sparks[i].maxLife;
    uint8_t heat = (uint8_t)(220.0f * t);
    setPixel(px, py, heatToColor(heat));
  }
}

void stepFireFrame() {
  float w = fireTendrils / 10.0f;

  for (uint8_t x = 0; x < MATRIX_W; x++) {
    if (random8() < (uint8_t)(20 + w * 30)) {
      columnActive[x] = (random8() > (uint8_t)(w * 140)) ? 1 : 0;
    }
    if (random8() < (uint8_t)(w * 76)) {
      columnDrift[x] += (random8() < 128) ? -1 : 1;
      columnDrift[x] = constrain(columnDrift[x], -1, 1);
    }
    if (random8() < 38) columnDrift[x] = 0;
  }

  for (uint8_t x = 0; x < MATRIX_W; x++) {
    float   activeBoost  = columnActive[x] ? 1.0f : (1.0f - w * 0.85f);
    uint8_t sparkThresh  = (uint8_t)((100 + fireIntensity * 15) * activeBoost);
    if (random8() < sparkThresh) {
      fireHeat[(MATRIX_H - 1) * MATRIX_W + x] = qadd8(100, random8(fireIntensity * 15 + 40));
    } else if (w > 0 && !columnActive[x]) {
      fireHeat[(MATRIX_H - 1) * MATRIX_W + x] = (uint8_t)(fireHeat[(MATRIX_H - 1) * MATRIX_W + x] * (1.0f - w * 0.6f));
    }
  }

  for (uint8_t y = 0; y < MATRIX_H - 1; y++) {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      uint16_t below  = fireHeat[(y + 1) * MATRIX_W + x];
      uint16_t belowL = fireHeat[(y + 1) * MATRIX_W + max(0, (int)x - 1)];
      uint16_t belowR = fireHeat[(y + 1) * MATRIX_W + min(MATRIX_W - 1, (int)x + 1)];
      uint16_t broadAvg = (below * 2 + belowL + belowR) / 4;

      uint8_t  driftX    = constrain((int)x + columnDrift[x], 0, MATRIX_W - 1);
      uint16_t belowDrift = fireHeat[(y + 1) * MATRIX_W + driftX];
      uint16_t wispyAvg  = (below * 3 + belowDrift) / 4;

      uint16_t avg = (uint16_t)(broadAvg * (1.0f - w) + wispyAvg * w);

      uint8_t baseDecay  = 20 + random8(35 - fireIntensity * 2);
      float   wispyDecay = columnActive[x]
        ? baseDecay * (1.0f - w * 0.2f)
        : baseDecay * (1.0f + w * 1.5f);
      uint8_t decay = (uint8_t)(baseDecay * (1.0f - w) + wispyDecay * w);

      fireHeat[y * MATRIX_W + x] = (avg > decay) ? (uint8_t)(avg - decay) : 0;
    }
  }

  for (uint8_t y = 0; y < MATRIX_H; y++) {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      int idx = XY(x, y);
      if (idx >= 0) leds[idx] = heatToColor(fireHeat[y * MATRIX_W + x]);
    }
  }

  if (sparkRate > 0) stepSparks();
}
