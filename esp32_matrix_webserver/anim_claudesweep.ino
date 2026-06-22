// ============================================================
// SECTION: CLAUDESWEEP ANIMATION
// A single-color CRT/radar sweep around the 8x8 perimeter (dim baseline ->
// bright head -> fading comet tail, never off), with the orange Claude mascot
// centered inside the 1px border doing a 1px bob + eye-blink.
// Sweep uses the same per-pixel decay/floor trick as anim_comet.ino.
// ============================================================

// The 28 perimeter pixels as an ordered CLOCKWISE loop, starting top-left.
// Top row L->R (8), right col T->B (7), bottom row R->L (7), left col B->T (6).
static const uint8_t SWEEP_PERIM[28][2] = {
  {0,0},{1,0},{2,0},{3,0},{4,0},{5,0},{6,0},{7,0},          // top
  {7,1},{7,2},{7,3},{7,4},{7,5},{7,6},{7,7},                // right
  {6,7},{5,7},{4,7},{3,7},{2,7},{1,7},{0,7},                // bottom (R->L)
  {0,6},{0,5},{0,4},{0,3},{0,2},{0,1}                        // left (B->T)
};

// Per-pixel sweep brightness (0..255), decayed each frame toward the floor.
static uint8_t  sweepBri[28];
static uint8_t  sweepHead   = 0;       // current head index into SWEEP_PERIM
static bool     sweepInit   = false;

// Baseline floor: the ring never dims below this. The HARD minimum for amber's
// green channel to survive FastLED global brightness 5 USED to be 63, but the
// always-on white-balance correction now ATTENUATES green (×0.863) BEFORE the bri-5
// scaling — so amber's green at the floor must be ~73 (was 63) to still light. We
// bump 76→88 to restore real margin under correction (any weaker-green hue / bri-4
// corner still reads). Costs nothing visually at bri 5. Verify live in Phase 4D.
static const uint8_t SWEEP_FLOOR = 88;
// Per-frame decay multiplier for the tail (scale8: 200/256 ~= 0.78 -> a ~4-5px tail).
static const uint8_t SWEEP_DECAY = 200;

// ---- Mini Claude (6 wide x 5 tall) drawn in the 6x6 interior (board cols 1-6) ----
// '#' = lit (orange), '.' = off. Eyes are the gaps in row 2.
static const char* CLAUDE6_OPEN[5] = {
  ".####.",
  "######",
  "#.##.#",
  "######",
  ".#..#."
};
// Blink frame: eyes closed (row 2 filled).
static const char* CLAUDE6_BLINK[5] = {
  ".####.",
  "######",
  "######",
  "######",
  ".#..#."
};
static const CRGB CLAUDE_ORANGE = CRGB(0xFF, 0x50, 0x08);   // locked orange #ff5008 (was #ff6a14 — pre-correction; the global white-balance now does the green-pull, so author the true deep orange)

static uint32_t sweepFrameCount = 0;   // drives bob + blink cadence

static void drawMiniClaude() {
  // Bob: vertical offset toggles 0/1 every ~14 frames. Interior rows are 1..6,
  // so offset 0 -> sprite rows 1..5, offset 1 -> rows 2..6 (both inside the border).
  int bob = ((sweepFrameCount / 14) % 2);
  // Blink: closed for ~3 frames every ~40 frames.
  bool blink = (sweepFrameCount % 40) < 3;
  const char** spr = blink ? CLAUDE6_BLINK : CLAUDE6_OPEN;
  for (int sy = 0; sy < 5; sy++) {
    for (int sx = 0; sx < 6; sx++) {
      if (spr[sy][sx] == '#') setPixel(sx + 1, sy + 1 + bob, CLAUDE_ORANGE);
    }
  }
}

void resetClaudeSweep() { sweepInit = false; }

void stepClaudeSweepFrame() {
  if (!sweepInit) {
    for (int i = 0; i < 28; i++) sweepBri[i] = SWEEP_FLOOR;
    sweepHead = 0; sweepFrameCount = 0; sweepInit = true;
  }
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  // Decay the whole ring toward 0, then advance + light the head.
  for (int i = 0; i < 28; i++) sweepBri[i] = scale8(sweepBri[i], SWEEP_DECAY);
  sweepHead = (sweepHead + 1) % 28;
  sweepBri[sweepHead] = 255;

  // Render the ring: floor each pixel so it never drops below the dim baseline.
  for (int i = 0; i < 28; i++) {
    uint8_t b = sweepBri[i] > SWEEP_FLOOR ? sweepBri[i] : SWEEP_FLOOR;
    CRGB c = sweepColor; c.nscale8(b);
    setPixel(SWEEP_PERIM[i][0], SWEEP_PERIM[i][1], c);
  }

  drawMiniClaude();
  sweepFrameCount++;
}
