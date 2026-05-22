// ============================================================
// SECTION: GRADIENT ANIMATIONS
// Gradient Spiral, Gradient Starfield, Sun
// ============================================================

// ── Gradient Spiral ───────────────────────────────────────────
// Pre-computes the 64-position clockwise inward spiral path at boot.
// Each frame slides a color gradient along that path — the whole board
// stays lit at all times, color1 chases color2 endlessly inward.

static int8_t  spiralPath[64][2];   // {x, y} for each of the 64 positions
static bool    spiralReady = false;
static uint8_t spiralPhase = 0;

void buildSpiralPath() {
  int idx = 0;
  int top = 0, bottom = 7, left_col = 0, right_col = 7;
  while (idx < 64) {
    for (int x = left_col; x <= right_col && idx < 64; x++) { spiralPath[idx][0]=x; spiralPath[idx][1]=top;    idx++; }
    top++;
    for (int y = top;  y <= bottom   && idx < 64; y++) { spiralPath[idx][0]=right_col; spiralPath[idx][1]=y;   idx++; }
    right_col--;
    for (int x = right_col; x >= left_col && idx < 64; x--) { spiralPath[idx][0]=x; spiralPath[idx][1]=bottom; idx++; }
    bottom--;
    for (int y = bottom; y >= top    && idx < 64; y--) { spiralPath[idx][0]=left_col; spiralPath[idx][1]=y;   idx++; }
    left_col++;
  }
}

// gradient slides: color1 head advances along spiral path each frame
void runSpiralFrame() {
  if (!spiralReady) { buildSpiralPath(); spiralReady = true; }
  for (int i = 0; i < 64; i++) {
    uint8_t t = (uint8_t)(((uint16_t)((i + 64 - spiralPhase) % 64) * 255) / 63);
    setPixel(spiralPath[i][0], spiralPath[i][1], blend(spiralColor1, spiralColor2, t));
  }
  spiralPhase = (spiralPhase + 1) % 64;
}

// ── Gradient Starfield ────────────────────────────────────────
// Pool of star particles. Outward: born at center, die at edges.
// Inward: born at random edge pixel, die at center.
// Color lerps from starColor1 (birth) to starColor2 (death).

struct StarParticle {
  float   x, y;
  float   dx, dy;
  uint8_t age;
  uint8_t maxAge;
  uint8_t brightness;
  bool    active;
};

static StarParticle stars[16];
static bool starsInitialized = false;

static void spawnStar(StarParticle& s) {
  if (starInward) {
    uint8_t edge = random(4);
    if      (edge == 0) { s.x = (float)random(8); s.y = 0.0f; }
    else if (edge == 1) { s.x = 7.0f;              s.y = (float)random(8); }
    else if (edge == 2) { s.x = (float)random(8); s.y = 7.0f; }
    else                { s.x = 0.0f;              s.y = (float)random(8); }
    float cx = 3.5f - s.x, cy = 3.5f - s.y;
    float len = sqrt(cx * cx + cy * cy);
    if (len < 0.01f) len = 0.01f;
    float speed = 0.2f + random(3) * 0.08f;
    s.dx = cx / len * speed;
    s.dy = cy / len * speed;
  } else {
    s.x = 3.5f; s.y = 3.5f;
    float angle = random(360) * (PI / 180.0f);
    float speed = 0.15f + random(3) * 0.08f;
    s.dx = cos(angle) * speed;
    s.dy = sin(angle) * speed;
  }
  s.age        = starInward ? 0 : random(10);   // stagger ages on first spawn
  s.maxAge     = 25 + random(20);
  s.brightness = 80 + random(175);
  s.active     = true;
}

void runStarfieldFrame() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  uint8_t count = min((uint8_t)starDensity, (uint8_t)16);
  for (uint8_t i = 0; i < count; i++) {
    StarParticle& s = stars[i];
    if (!starsInitialized || !s.active || s.age >= s.maxAge) { spawnStar(s); }

    s.x   += s.dx;
    s.y   += s.dy;
    s.age++;

    bool offScreen = (s.x < 0 || s.x > 7 || s.y < 0 || s.y > 7);
    bool atCenter  = starInward && (fabsf(s.x - 3.5f) < 0.7f && fabsf(s.y - 3.5f) < 0.7f);
    if (offScreen || atCenter) { spawnStar(s); continue; }

    uint8_t t = (uint8_t)(((uint16_t)s.age * 255) / s.maxAge);
    CRGB c    = blend(starColor1, starColor2, t);
    c.nscale8(s.brightness);
    setPixel((int)s.x, (int)s.y, c);
  }
  starsInitialized = true;
}
