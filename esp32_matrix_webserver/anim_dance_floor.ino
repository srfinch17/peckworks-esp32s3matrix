// ============================================================
// SECTION: DANCE FLOOR ANIMATION
// 4×4 grid of 2×2 tiles using 4 color slots in a 2×2 repeating
// checkerboard. All tiles change simultaneously. Each cycle a
// Fisher-Yates shuffle assigns a new permutation of the 4 palette
// colors to the 4 slots — guaranteeing no two adjacent tiles
// (including diagonals) ever share the same color.
// ============================================================

#define DF_BLEND_F   10   // frames to crossfade between cycles
#define DF_HOLD_RNG  20   // random extra hold frames per cycle

// Slot assignment: slot = (tx%2) + (ty%2)*2
//   Row 0: 0 1 0 1
//   Row 1: 2 3 2 3
//   Row 2: 0 1 0 1
//   Row 3: 2 3 2 3
// All 4-directional and diagonal neighbors have different slot numbers.

static CRGB    dfSlotCur[4];
static CRGB    dfSlotNxt[4];
static uint8_t dfBlendPos   = 0;
static uint8_t dfHoldCount  = 0;
static uint8_t dfBrightness[16];
static bool    dfInit       = false;

static const CRGB DF_PALETTES[64][4] = {
  // ── 0-7: Neon / Club ──────────────────────────────────────
  { CRGB(255,  0,255), CRGB(  0,255,255), CRGB(255,255,  0), CRGB(  0,255,  0) }, // 0  Neon Classic
  { CRGB(255,  0,128), CRGB(  0,255,128), CRGB(128,  0,255), CRGB(255,128,  0) }, // 1  Neon Shifted
  { CRGB(255,  0,200), CRGB(  0,200,255), CRGB(200,255,  0), CRGB(255,200,  0) }, // 2  Neon Soft
  { CRGB(255,  0, 80), CRGB( 80,  0,255), CRGB(  0,255, 80), CRGB(255, 80,  0) }, // 3  Primary Neon
  { CRGB(220,  0,255), CRGB(  0,255,220), CRGB(255,220,  0), CRGB(  0,220,255) }, // 4  Electric
  { CRGB(255,  0,255), CRGB(255,  0,100), CRGB(100,  0,255), CRGB(  0,100,255) }, // 5  Pink Purple
  { CRGB(  0,255,255), CRGB(  0,200,255), CRGB(  0,255,200), CRGB(  0,150,255) }, // 6  Cyan Family
  { CRGB(255,255,  0), CRGB(255,200,  0), CRGB(200,255,  0), CRGB(255,150,  0) }, // 7  Yellow Family
  // ── 8-15: Fire / Warm ─────────────────────────────────────
  { CRGB(255, 50,  0), CRGB(255,150,  0), CRGB(255,200,  0), CRGB(255,255, 50) }, // 8  Fire
  { CRGB(255,  0,  0), CRGB(255, 80,  0), CRGB(200,  0,  0), CRGB(255, 40, 40) }, // 9  Red Hot
  { CRGB(255,100,  0), CRGB(255,200, 50), CRGB(255, 50,  0), CRGB(200,100,  0) }, // 10 Amber
  { CRGB(255, 20,  0), CRGB(255,100,  0), CRGB(255,160,  0), CRGB(255,255,100) }, // 11 Ember
  { CRGB(255,  0, 50), CRGB(255, 50,  0), CRGB(200,  0,100), CRGB(255,100, 50) }, // 12 Lava
  { CRGB(255, 80, 80), CRGB(255, 40,  0), CRGB(200, 20,  0), CRGB(255,160, 80) }, // 13 Sunset Warm
  { CRGB(255,200,  0), CRGB(255,100,  0), CRGB(255, 50, 50), CRGB(200,200,  0) }, // 14 Gold
  { CRGB(255,  0, 80), CRGB(255, 80,  0), CRGB(255,180,  0), CRGB(200,  0, 80) }, // 15 Hot Candy
  // ── 16-23: Ocean / Cool ───────────────────────────────────
  { CRGB(  0,100,255), CRGB(  0,200,255), CRGB(  0,255,200), CRGB( 50, 50,200) }, // 16 Ocean
  { CRGB(  0,150,255), CRGB(  0,255,255), CRGB(  0,200,200), CRGB(100,200,255) }, // 17 Aqua
  { CRGB(  0, 50,200), CRGB( 50,100,255), CRGB(  0,200,255), CRGB(100, 50,200) }, // 18 Deep Blue
  { CRGB(  0,200,200), CRGB(  0,150,200), CRGB( 50,200,255), CRGB(  0,100,150) }, // 19 Teal
  { CRGB(100,  0,255), CRGB(  0,100,255), CRGB(  0,200,255), CRGB( 50,  0,200) }, // 20 Blue Purple
  { CRGB(  0,255,255), CRGB(  0,200,255), CRGB(  0,150,255), CRGB(  0,100,200) }, // 21 Ice
  { CRGB( 50,  0,200), CRGB(100,  0,255), CRGB(150, 50,255), CRGB(200,100,255) }, // 22 Violet
  { CRGB(  0,100,200), CRGB(  0, 50,150), CRGB( 50,150,255), CRGB(100,200,255) }, // 23 Navy
  // ── 24-31: Nature ─────────────────────────────────────────
  { CRGB(  0,200,  0), CRGB( 50,255, 50), CRGB(100,255,  0), CRGB(  0,150, 50) }, // 24 Forest
  { CRGB(  0,255,  0), CRGB(100,255,  0), CRGB(  0,200, 50), CRGB( 50,255,100) }, // 25 Lime
  { CRGB(100,255,  0), CRGB(200,255,  0), CRGB( 50,200,  0), CRGB(150,255, 50) }, // 26 Chartreuse
  { CRGB(  0,150, 50), CRGB(  0,200,100), CRGB( 50,255,150), CRGB(  0,100, 50) }, // 27 Emerald
  { CRGB(200,150, 50), CRGB(150,100,  0), CRGB(100,200, 50), CRGB(200,200,100) }, // 28 Earth
  { CRGB(255,150,  0), CRGB(200,100,  0), CRGB(100,200,  0), CRGB(255,200, 50) }, // 29 Autumn
  { CRGB(255,100,150), CRGB(200,255,100), CRGB(100,200,255), CRGB(255,200,100) }, // 30 Spring
  { CRGB(  0,200,150), CRGB(  0,150,100), CRGB( 50,255,200), CRGB(100,255,200) }, // 31 Jade
  // ── 32-39: Pastel ─────────────────────────────────────────
  { CRGB(255,150,200), CRGB(150,200,255), CRGB(200,255,150), CRGB(255,255,150) }, // 32 Pastel Rainbow
  { CRGB(255,150,200), CRGB(255,100,150), CRGB(200,100,200), CRGB(255,200,220) }, // 33 Pastel Pink
  { CRGB(150,200,255), CRGB(100,150,255), CRGB(150,150,255), CRGB(200,220,255) }, // 34 Pastel Blue
  { CRGB(200,255,200), CRGB(150,255,150), CRGB(100,220,150), CRGB(200,255,180) }, // 35 Pastel Green
  { CRGB(255,200,150), CRGB(255,220,150), CRGB(200,150,100), CRGB(255,230,180) }, // 36 Pastel Warm
  { CRGB(200,150,255), CRGB(220,180,255), CRGB(180,100,255), CRGB(240,200,255) }, // 37 Pastel Purple
  { CRGB(150,255,240), CRGB(150,220,255), CRGB(180,255,220), CRGB(200,255,255) }, // 38 Pastel Mint
  { CRGB(255,255,150), CRGB(255,240,100), CRGB(255,200,100), CRGB(255,255,200) }, // 39 Pastel Yellow
  // ── 40-47: Monochrome ─────────────────────────────────────
  { CRGB(255,  0,  0), CRGB(200,  0,  0), CRGB(150,  0,  0), CRGB(100,  0,  0) }, // 40 Red Mono
  { CRGB(255, 80,  0), CRGB(200, 60,  0), CRGB(150, 40,  0), CRGB(255,120,  0) }, // 41 Orange Mono
  { CRGB(255,255,  0), CRGB(200,200,  0), CRGB(150,150,  0), CRGB(255,220, 50) }, // 42 Yellow Mono
  { CRGB(  0,255,  0), CRGB(  0,200,  0), CRGB(  0,150,  0), CRGB( 50,255, 50) }, // 43 Green Mono
  { CRGB(  0,  0,255), CRGB(  0,  0,200), CRGB(  0, 50,255), CRGB( 50, 50,255) }, // 44 Blue Mono
  { CRGB(150,  0,255), CRGB(100,  0,200), CRGB(200, 50,255), CRGB( 80,  0,180) }, // 45 Purple Mono
  { CRGB(255,  0,150), CRGB(200,  0,100), CRGB(255, 50,180), CRGB(150,  0, 80) }, // 46 Pink Mono
  { CRGB(  0,255,200), CRGB(  0,200,150), CRGB( 50,255,220), CRGB(  0,150,120) }, // 47 Teal Mono
  // ── 48-55: Retro / 80s ────────────────────────────────────
  { CRGB(255,  0,255), CRGB(  0,255,  0), CRGB(255,255,  0), CRGB(  0,  0,255) }, // 48 80s Classic
  { CRGB(255,  0,100), CRGB(  0,200,255), CRGB(200,255,  0), CRGB(255,100,  0) }, // 49 Miami Vice
  { CRGB(100,  0,255), CRGB(255,  0,255), CRGB(  0,200,200), CRGB(255,200,  0) }, // 50 Synthwave
  { CRGB(255, 50,150), CRGB(150,  0,255), CRGB(  0,200,255), CRGB(255,200, 50) }, // 51 VHS
  { CRGB(  0,255,  0), CRGB(  0,200,  0), CRGB(255,  0,  0), CRGB(  0,  0,255) }, // 52 Arcade
  { CRGB(255,200,  0), CRGB(255,100,  0), CRGB(  0,200,  0), CRGB(200,  0,200) }, // 53 Pac-Man
  { CRGB(255,255,  0), CRGB(255,  0,  0), CRGB(  0,  0,255), CRGB(255,255,255) }, // 54 Pinball
  { CRGB(  0,255,150), CRGB(255,  0,100), CRGB(255,150,  0), CRGB(100,  0,255) }, // 55 Funky
  // ── 56-63: Dark / Moody ───────────────────────────────────
  { CRGB(100,  0,150), CRGB(  0, 50,150), CRGB(150,  0,100), CRGB(  0,100,100) }, // 56 Dark Galaxy
  { CRGB( 80,  0,  0), CRGB( 50,  0, 50), CRGB(  0,  0, 80), CRGB( 80, 40,  0) }, // 57 Ember Dark
  { CRGB(150,  0, 50), CRGB(100,  0,100), CRGB( 50,  0,150), CRGB(  0, 50,100) }, // 58 Noir
  { CRGB(  0,100,  0), CRGB(  0, 80, 50), CRGB( 50,100,  0), CRGB(  0, 60, 60) }, // 59 Deep Forest
  { CRGB(100, 50,  0), CRGB( 80,  0,  0), CRGB( 50, 50,  0), CRGB( 60, 30,  0) }, // 60 Rust
  { CRGB(  0, 80,100), CRGB(  0, 50, 80), CRGB( 50,  0,100), CRGB(  0,100, 80) }, // 61 Abyss
  { CRGB( 80,  0, 80), CRGB( 60,  0, 60), CRGB(100,  0, 50), CRGB( 50,  0, 80) }, // 62 Dusk
  { CRGB( 50, 50, 50), CRGB( 80, 80, 80), CRGB(120,120,120), CRGB( 30, 30, 30) }, // 63 Grayscale
};

static void dfShuffle(uint8_t perm[4]) {
  for (int i = 3; i > 0; i--) {
    int j = random(i + 1);
    uint8_t tmp = perm[i]; perm[i] = perm[j]; perm[j] = tmp;
  }
}

static void dfNewCycle() {
  uint8_t perm[4] = {0, 1, 2, 3};
  dfShuffle(perm);
  for (int s = 0; s < 4; s++) {
    dfSlotCur[s] = dfSlotNxt[s];
    dfSlotNxt[s] = DF_PALETTES[dfPalette][perm[s]];
  }
  dfBlendPos  = 0;
  dfHoldCount = dfHoldMin + (uint8_t)random(DF_HOLD_RNG);
}

void runDanceFloorFrame() {
  if (!dfInit) {
    uint8_t perm[4] = {0, 1, 2, 3};
    dfShuffle(perm);
    for (int s = 0; s < 4; s++) dfSlotCur[s] = DF_PALETTES[dfPalette][perm[s]];
    dfShuffle(perm);
    for (int s = 0; s < 4; s++) dfSlotNxt[s] = DF_PALETTES[dfPalette][perm[s]];
    for (int i = 0; i < 16; i++) dfBrightness[i] = 160 + random(96);
    dfBlendPos  = DF_BLEND_F;
    dfHoldCount = dfHoldMin;
    dfInit      = true;
  }

  if      (dfBlendPos < DF_BLEND_F) dfBlendPos++;
  else if (dfHoldCount > 0)         dfHoldCount--;
  else                              dfNewCycle();

  uint8_t blend_t = (dfBlendPos >= DF_BLEND_F)
    ? 255
    : (uint8_t)((uint16_t)dfBlendPos * 255 / DF_BLEND_F);

  for (int i = 0; i < 16; i++) {
    int     tx   = i % 4, ty = i / 4;
    uint8_t slot = (uint8_t)((tx % 2) + (ty % 2) * 2);
    CRGB c = blend(dfSlotCur[slot], dfSlotNxt[slot], blend_t);
    c.nscale8(dfBrightness[i]);
    int px = tx * 2, py = ty * 2;
    setPixel(px,   py,   c);
    setPixel(px+1, py,   c);
    setPixel(px,   py+1, c);
    setPixel(px+1, py+1, c);
  }
}
