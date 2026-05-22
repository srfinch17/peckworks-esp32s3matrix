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
