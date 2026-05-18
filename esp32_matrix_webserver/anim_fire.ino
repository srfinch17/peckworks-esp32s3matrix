// ============================================================
// SECTION 6: FIRE ANIMATION
//
// HOW IT WORKS — the heat simulation:
//   Each of the 64 LEDs has a "heat" value from 0 (cold/black)
//   to 255 (white-hot). Every frame, three things happen:
//
//     1. SPARK — random hot sparks ignite at the bottom row,
//                simulating the base of the flame.
//     2. RISE  — heat diffuses upward: each cell averages its
//                own value with the cells below it, so hot air
//                naturally rises. The "tendrils" param bends
//                this diffusion sideways, making wispy columns.
//     3. COOL  — a random decay value is subtracted from each
//                cell. Hot regions cool toward the top, which is
//                why the flame fades out before reaching row 0.
//
//   After simulation, each heat value is mapped through a color
//   palette: black → dark red → orange → yellow → white.
//
// PARAMETERS:
//   palette   — color theme (classic/blue/green/purple)
//   intensity — how hot and tall the flame is (1-10)
//   tendrils  — how wispy/narrow the flame columns are (0-10)
//   sparks    — flying ember particles above the main flame (0-10)
// ============================================================

// ── Color Palettes ────────────────────────────────────────────
// Each palette has 8 color stops, from coldest (index 0) to
// hottest (index 7). heatToColor() lerps between these stops.
//
// PROGMEM stores the data in flash memory instead of RAM.
// The ESP32 only has ~300KB of RAM but 8MB of flash, so large
// constant arrays belong in flash. pgm_read_byte() reads them back.

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

// Pointer to the active palette — swapped out when a new theme is selected.
const uint8_t (*activePalette)[3] = PALETTE_CLASSIC;

// One heat value per LED, laid out row-major (y * MATRIX_W + x).
uint8_t fireHeat[NUM_LEDS];

// Per-column drift direction (-1, 0, +1) for tendril mode.
// Non-zero drift causes heat to rise slightly sideways.
int8_t  columnDrift[MATRIX_W];

// Per-column "active" flag for tendril mode.
// Inactive columns get much less spark energy → gaps between tendrils.
uint8_t columnActive[MATRIX_W];

uint8_t fireIntensity = 6;   // 1-10
uint8_t fireTendrils  = 0;   // 0-10

// ── Spark (Ember) Particle System ────────────────────────────
// Sparks are free-flying particles that rise above the main flame.
// Each spark has a position (x, y), velocity (vx, vy), and lifetime.
// They're spawned from hot pixels near the top of the flame body
// and rise upward until they cool out or leave the matrix.

#define MAX_SPARKS 8

struct Spark {
  float   x, y;      // floating-point position for smooth motion
  float   vy;        // vertical velocity (negative = moving up)
  float   vx;        // horizontal drift
  uint8_t life;      // frames remaining
  uint8_t maxLife;   // total lifespan (for brightness fade calculation)
};

Spark   sparks[MAX_SPARKS];
uint8_t sparkRate = 0;   // 0 = no sparks; higher = more frequent

// ── heatToColor ───────────────────────────────────────────────
// Converts a heat value (0-255) to a CRGB color by linearly
// interpolating between the two nearest palette stops.
//
// Example with 8 stops and h=128 (half-hot):
//   t = 128/255 ≈ 0.50
//   idx = 0.50 * 7 = 3.5  → lo=3, hi=4, frac=0.5
//   Result = midpoint between stop 3 and stop 4 colors.
CRGB heatToColor(uint8_t h) {
  float t   = h / 255.0f;
  float idx = t * (PALETTE_SIZE - 1);
  uint8_t lo  = (uint8_t)idx;
  uint8_t hi  = min((uint8_t)(lo + 1), (uint8_t)(PALETTE_SIZE - 1));
  float   frac = idx - lo;

  // pgm_read_byte() fetches a single byte from PROGMEM (flash).
  // Without it, reading from a PROGMEM array returns garbage on AVR/Xtensa.
  uint8_t r = pgm_read_byte(&activePalette[lo][0]) + (pgm_read_byte(&activePalette[hi][0]) - pgm_read_byte(&activePalette[lo][0])) * frac;
  uint8_t g = pgm_read_byte(&activePalette[lo][1]) + (pgm_read_byte(&activePalette[hi][1]) - pgm_read_byte(&activePalette[lo][1])) * frac;
  uint8_t b = pgm_read_byte(&activePalette[lo][2]) + (pgm_read_byte(&activePalette[hi][2]) - pgm_read_byte(&activePalette[lo][2])) * frac;
  return CRGB(r, g, b);
}

void initSparks() {
  memset(sparks, 0, sizeof(sparks));   // zero out all sparks → all inactive (life == 0)
}

// ── stepSparks ────────────────────────────────────────────────
// Each call: tries to spawn one new spark, then advances all live ones.
//
// SPAWN: picks a random column, finds the first hot row from the
// top, and launches a spark from there with upward velocity scaled
// so it's guaranteed to exit the matrix before it dies.
//
// ADVANCE: moves each spark by its velocity, bounces off left/right
// walls, and dims it as it ages (life/maxLife gives a 0-1 fade factor).
void stepSparks() {
  // Try to spawn one new spark into the first empty slot
  for (int i = 0; i < MAX_SPARKS; i++) {
    if (sparks[i].life > 0) continue;              // slot in use
    if (random8(100) >= sparkRate * 10) break;     // random chance based on sparkRate

    // Find a hot pixel in this column to spawn from
    int spawnX = random8(MATRIX_W);
    int spawnY = -1;
    for (int ty = 0; ty < MATRIX_H; ty++) {
      if (fireHeat[ty * MATRIX_W + spawnX] >= 25) { spawnY = ty; break; }
    }
    if (spawnY < 0) break;   // column too cold — skip

    uint8_t life  = 6 + random8(7);
    // minVy ensures the spark travels far enough to exit the matrix in its lifetime
    float   minVy = ((float)spawnY + 2.0f) / (float)life;
    sparks[i].x       = (float)spawnX;
    sparks[i].y       = (float)spawnY;
    sparks[i].vy      = -(minVy + random8(3) * 0.05f);   // negative = upward
    sparks[i].vx      = (random8(5) - 2) * 0.10f;        // small random sideways drift
    sparks[i].maxLife = life;
    sparks[i].life    = life;
    break;   // only one spawn per frame
  }

  // Advance all live sparks
  for (int i = 0; i < MAX_SPARKS; i++) {
    if (sparks[i].life == 0) continue;

    sparks[i].y += sparks[i].vy;
    sparks[i].x += sparks[i].vx;
    sparks[i].life--;

    if (sparks[i].y < -1.0f) { sparks[i].life = 0; continue; }   // off top

    // Bounce off left/right walls instead of disappearing
    if (sparks[i].x < 0.0f || sparks[i].x >= (float)MATRIX_W) {
      sparks[i].vx = -sparks[i].vx;
      sparks[i].x  = constrain(sparks[i].x, 0.0f, (float)(MATRIX_W - 1));
    }

    int px = (int)roundf(sparks[i].x);
    int py = (int)roundf(sparks[i].y);
    if (py < 0 || py >= MATRIX_H) continue;

    // Brightness fades from full (just spawned) to dark (about to die)
    float   t    = (float)sparks[i].life / (float)sparks[i].maxLife;
    uint8_t heat = (uint8_t)(220.0f * t);
    setPixel(px, py, heatToColor(heat));
  }
}

// ── stepFireFrame ─────────────────────────────────────────────
// Called once per frame. Three phases: spark, diffuse/cool, render.
//
// The "w" variable (0.0-1.0) is the tendrils parameter scaled to [0,1].
// At w=0 the fire is broad and even. At w=1 the fire splits into
// narrow wispy columns with dark gaps between them.
void stepFireFrame() {
  float w = fireTendrils / 10.0f;   // tendril weight: 0=none, 1=max

  // ── Phase 1: Update column personality (tendril mode) ──────
  // Each frame, columns randomly toggle their "active" state and
  // drift direction. Inactive columns get much less spark energy,
  // creating the dark gaps between tendrils.
  for (uint8_t x = 0; x < MATRIX_W; x++) {
    if (random8() < (uint8_t)(20 + w * 30)) {
      columnActive[x] = (random8() > (uint8_t)(w * 140)) ? 1 : 0;
    }
    if (random8() < (uint8_t)(w * 76)) {
      columnDrift[x] += (random8() < 128) ? -1 : 1;
      columnDrift[x] = constrain(columnDrift[x], -1, 1);
    }
    if (random8() < 38) columnDrift[x] = 0;   // drift decays back toward center
  }

  // ── Phase 2: Ignite sparks at the bottom row ───────────────
  // The bottom row (y = MATRIX_H-1) is the "fire base".
  // Random cells get set to a high heat value each frame.
  // The sparkThresh controls how often ignition happens and how hot.
  for (uint8_t x = 0; x < MATRIX_W; x++) {
    float   activeBoost  = columnActive[x] ? 1.0f : (1.0f - w * 0.85f);
    uint8_t sparkThresh  = (uint8_t)((100 + fireIntensity * 15) * activeBoost);
    if (random8() < sparkThresh) {
      // qadd8 is a FastLED saturating add — clamps at 255 instead of wrapping
      fireHeat[(MATRIX_H - 1) * MATRIX_W + x] = qadd8(100, random8(fireIntensity * 15 + 40));
    } else if (w > 0 && !columnActive[x]) {
      // Inactive tendril columns cool rapidly at the base → dark gap
      fireHeat[(MATRIX_H - 1) * MATRIX_W + x] = (uint8_t)(fireHeat[(MATRIX_H - 1) * MATRIX_W + x] * (1.0f - w * 0.6f));
    }
  }

  // ── Phase 3: Diffuse heat upward + cool ────────────────────
  // For each non-bottom cell, compute a weighted average of the
  // cells below it. This is the "heat rises" simulation.
  //
  // broadAvg: averages current cell with its lower-left, lower-right,
  //           and directly-below neighbors (standard 2D fire algorithm).
  // wispyAvg: uses the drifted column position, creating a "leaning" flame.
  // avg:      blends between broad and wispy based on tendril weight.
  //
  // A random decay is then subtracted so heat cools as it rises.
  for (uint8_t y = 0; y < MATRIX_H - 1; y++) {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      uint16_t below  = fireHeat[(y + 1) * MATRIX_W + x];
      uint16_t belowL = fireHeat[(y + 1) * MATRIX_W + max(0, (int)x - 1)];
      uint16_t belowR = fireHeat[(y + 1) * MATRIX_W + min(MATRIX_W - 1, (int)x + 1)];
      uint16_t broadAvg = (below * 2 + belowL + belowR) / 4;

      uint8_t  driftX     = constrain((int)x + columnDrift[x], 0, MATRIX_W - 1);
      uint16_t belowDrift = fireHeat[(y + 1) * MATRIX_W + driftX];
      uint16_t wispyAvg   = (below * 3 + belowDrift) / 4;

      // Blend between broad and wispy averaging based on tendril strength
      uint16_t avg = (uint16_t)(broadAvg * (1.0f - w) + wispyAvg * w);

      // Random decay — active columns cool less than inactive ones in tendril mode
      uint8_t baseDecay  = 20 + random8(35 - fireIntensity * 2);
      float   wispyDecay = columnActive[x]
        ? baseDecay * (1.0f - w * 0.2f)
        : baseDecay * (1.0f + w * 1.5f);
      uint8_t decay = (uint8_t)(baseDecay * (1.0f - w) + wispyDecay * w);

      fireHeat[y * MATRIX_W + x] = (avg > decay) ? (uint8_t)(avg - decay) : 0;
    }
  }

  // ── Phase 4: Map heat to colors and write to leds[] ─────────
  for (uint8_t y = 0; y < MATRIX_H; y++) {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
      int idx = XY(x, y);
      if (idx >= 0) leds[idx] = heatToColor(fireHeat[y * MATRIX_W + x]);
    }
  }

  // Draw flying ember particles on top of the flame (if any)
  if (sparkRate > 0) stepSparks();
}
