// ============================================================
// SECTION 6.x: SNOW (ambient, no accumulation)
//
// Continuous snowfall that NEVER fills the screen — unlike the
// timer_snow mode (clock_timer.ino), whose whole point is to
// accumulate until time runs out. Here there is a fixed snow
// "floor" along the bottom and a handful of flakes that drift
// down and VANISH when they reach the floor, then respawn at the
// top. Steady-state: it just keeps snowing.
//
// COLOR (set in handleAnimation):
//   single-hue (default) — one random bri-5-safe color per launch
//     tints every flake AND the floor (a whole snowfall of e.g.
//     ice-blue, then cyan next time, then gold…). "Seeded" look.
//   confetti (confetti:true) — each flake picks its own random
//     color; the floor stays a neutral white.
//
// Channel-distinct hues only (white / ice-blue / cyan / mint /
// magenta / gold): at brightness 5 a continuous hue wheel
// collapses to mud — see docs/LED_BRIGHTNESS.md.
// ============================================================

#define SNOW_FLAKES 6   // simultaneous falling flakes — sparse reads as gentle snow + light heap

// bri-5-safe palette: spaced around the spectrum so colors stay distinct at
// brightness 5 (the 6/256 scaling leaves ~5 visible levels per channel, so
// near-hues collapse). The warm trio (red/orange/gold) shares R-high and is
// separated by distinct green levels (~40/130/200); cool hues carry most of
// the perceived variety. Keep in sync with PALETTE[] in data/snow.html.
const CRGB SNOW_PALETTE[] = {
  CRGB(255, 255, 255),  // white
  CRGB(255,  40,  40),  // red
  CRGB(255, 130,   0),  // orange
  CRGB(255, 200,  40),  // gold
  CRGB( 80, 255,  40),  // green
  CRGB( 60, 255, 140),  // mint
  CRGB(  0, 230, 230),  // cyan
  CRGB( 60, 140, 255),  // ice blue
  CRGB( 40,  70, 255),  // blue
  CRGB(160,  40, 255),  // violet
  CRGB(255,  60, 230),  // magenta
  CRGB(255,  80, 150),  // pink
};
#define SNOW_PALETTE_LEN (int)(sizeof(SNOW_PALETTE) / sizeof(SNOW_PALETTE[0]))

// Topmost floor row per column — an uneven 1px snow bank (mounds at cols 2 & 5
// rise one row). A flake vanishes once it reaches its column's floor surface.
const int8_t SNOW_FLOOR_TOP[MATRIX_W] = { 7, 7, 6, 7, 7, 6, 7, 7 };

// State for one falling flake.
struct SnowFlake {
  int8_t  x;       // column
  int8_t  y;       // current row; negative = still above the top edge
  uint8_t tick;    // ticks since last downward step
  uint8_t speed;   // ticks per step (1 = fast, 3 = slow) → per-flake parallax
  CRGB    color;   // confetti mode only; single-hue uses snowFlakeColor
};

SnowFlake snowFlakes[SNOW_FLAKES];
bool      snowConfetti   = false;            // false = single random hue per launch
CRGB      snowFlakeColor = CRGB(60,140,255); // single-hue flake color (set at launch)
CRGB      snowFloorColor = CRGB(60,140,255); // the snow bank color

// Picks a flake color: random palette entry in confetti mode, else the
// per-launch single hue.
CRGB pickFlakeColor() {
  return snowConfetti ? SNOW_PALETTE[random(0, SNOW_PALETTE_LEN)] : snowFlakeColor;
}

// (Re)spawns flake i above the top edge with a fresh column, speed and color.
// Staggered start heights so they don't all enter on the same frame.
// NOTE: takes an INDEX, not a SnowFlake& — the Arduino IDE auto-prototypes
// every function at the top of the concatenated sketch, ABOVE the struct
// definition, so a struct-typed parameter would fail to compile ("SnowFlake
// was not declared in this scope"). Same reason anim_matrix.ino's helpers
// operate on the global array instead of taking the struct as a param.
void spawnSnowFlake(int i, bool stagger) {
  SnowFlake& f = snowFlakes[i];
  f.x     = (int8_t)random(0, MATRIX_W);
  f.y     = stagger ? -(int8_t)random(1, MATRIX_H + 1) : -1;
  f.tick  = 0;
  f.speed = (uint8_t)random(1, 4);   // 1 = fast, 3 = slow
  f.color = pickFlakeColor();
}

// Resets all flakes — called from handleAnimation when snow starts.
void initSnow() {
  for (int i = 0; i < SNOW_FLAKES; i++) spawnSnowFlake(i, true);
}

// ── stepSnowFrame ─────────────────────────────────────────────
// One frame: draw the fixed floor, then advance and draw each flake.
// A flake that reaches its column's floor surface respawns at the top
// (no accumulation — the floor height never changes).
void stepSnowFrame() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  // Fixed snow bank along the bottom (uneven by 1px at the mounds).
  for (int x = 0; x < MATRIX_W; x++)
    for (int y = SNOW_FLOOR_TOP[x]; y < MATRIX_H; y++)
      setPixel(x, y, snowFloorColor);

  // Falling flakes.
  for (int i = 0; i < SNOW_FLAKES; i++) {
    SnowFlake& f = snowFlakes[i];

    // Advance one row when this flake's tick counter reaches its speed.
    f.tick++;
    if (f.tick >= f.speed) {
      f.tick = 0;
      f.y++;
    }

    // Reached the snow surface in its column → vanish + respawn at the top.
    if (f.y >= SNOW_FLOOR_TOP[f.x]) {
      spawnSnowFlake(i, false);
      continue;
    }

    // Draw it only while on-screen and above the floor.
    if (f.y >= 0) setPixel(f.x, f.y, f.color);
  }
}
