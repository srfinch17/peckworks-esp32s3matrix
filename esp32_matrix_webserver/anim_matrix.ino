// ============================================================
// SECTION 6.10: MATRIX DIGITAL RAIN
//
// Recreates the iconic "digital rain" effect from The Matrix.
// Each of the 8 columns has one independent falling drop.
// A drop is a bright "head" pixel at the front edge, followed
// by a short trail of pixels that fade to black behind it.
//
// Each drop moves at its own random speed (1-3 ticks per step),
// so columns don't travel in sync — the natural variation is
// what makes it look organic rather than like a grid update.
//
// COLOR THEMES (set in handleAnimation):
//   classic → green trail / white head
//   blue    → blue trail  / light blue head
//   red     → red trail   / pink head
//   purple  → purple trail / lavender head
//
// RECYCLING:
//   When a drop's head clears the bottom AND the entire trail
//   has also left the screen, the drop resets with a new random
//   starting position above the top edge and a new random speed.
//   The staggered initial positions prevent all 8 columns from
//   simultaneously having visible drops on startup.
// ============================================================

#define MATRIX_TRAIL_LEN 4   // number of fading pixels behind the head

// State for one falling drop in one column.
struct MatrixDrop {
  int8_t  headY;   // current Y of the head; negative = still above the matrix
  uint8_t tick;    // ticks elapsed since the last step
  uint8_t speed;   // ticks required per step (1=fast, 3=slow)
};

MatrixDrop matrixDrops[MATRIX_W];
CRGB       matrixHeadColor;    // bright front pixel color
CRGB       matrixTrailColor;   // base color for the fading tail

// Initializes all drops with random starting Y positions above the screen
// and random speeds so they don't all start at the top edge simultaneously.
void initMatrixDrops() {
  for (int col = 0; col < MATRIX_W; col++) {
    matrixDrops[col].headY = -(int8_t)random(1, MATRIX_H + 1);   // start 1-8 rows above top
    matrixDrops[col].tick  = 0;
    matrixDrops[col].speed = (uint8_t)random(1, 4);               // 1 = fast, 3 = slow
  }
}

// ── stepMatrixFrame ───────────────────────────────────────────
// Clears the buffer, then for each column:
//   1. Advances the drop by one row if enough ticks have elapsed.
//   2. Recycles the drop if it has fully cleared the bottom.
//   3. Draws the bright head pixel at headY.
//   4. Draws the trail: t pixels above the head, each at a
//      linearly decreasing brightness (t=1 is 75% bright,
//      t=TRAIL_LEN is nearly black).
void stepMatrixFrame() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  for (int col = 0; col < MATRIX_W; col++) {
    MatrixDrop& d = matrixDrops[col];

    // Advance one step when the tick counter reaches the speed threshold
    d.tick++;
    if (d.tick >= d.speed) {
      d.tick = 0;
      d.headY++;
    }

    // Recycle once the head + entire trail have cleared the bottom edge
    if (d.headY > (int8_t)(MATRIX_H + MATRIX_TRAIL_LEN)) {
      d.headY = -(int8_t)random(1, MATRIX_H + 1);
      d.speed = (uint8_t)random(1, 4);
    }

    // Draw bright head pixel (only when on-screen)
    if (d.headY >= 0 && d.headY < MATRIX_H)
      setPixel(col, d.headY, matrixHeadColor);

    // Draw fading trail above the head.
    // t=1 is one row above the head; t=TRAIL_LEN is farthest back.
    // Fade factor: (TRAIL_LEN - t) / (TRAIL_LEN + 1) → 1.0 near head, 0.0 at end.
    for (int t = 1; t <= MATRIX_TRAIL_LEN; t++) {
      int ty = d.headY - t;
      if (ty < 0 || ty >= MATRIX_H) continue;
      float fade = 1.0f - (float)t / (float)(MATRIX_TRAIL_LEN + 1);
      setPixel(col, ty, CRGB(
        (uint8_t)(matrixTrailColor.r * fade),
        (uint8_t)(matrixTrailColor.g * fade),
        (uint8_t)(matrixTrailColor.b * fade)
      ));
    }
  }
}
