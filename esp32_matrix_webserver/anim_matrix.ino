// ============================================================
// Matrix Digital Rain animation
// Each column has one independent drop: a bright head pixel
// followed by a linearly fading color trail above it.
// Theme selects trail and head colors. Speed and per-drop
// timing (1–3 ticks/step) are randomized for a natural look.
// ============================================================

#define MATRIX_TRAIL_LEN 4

struct MatrixDrop {
  int8_t  headY;   // Y of the head pixel; negative = waiting above screen
  uint8_t tick;    // ticks elapsed since last step
  uint8_t speed;   // ticks per step (1–3, randomized per drop)
};

MatrixDrop matrixDrops[MATRIX_W];
CRGB       matrixHeadColor;
CRGB       matrixTrailColor;

void initMatrixDrops() {
  for (int col = 0; col < MATRIX_W; col++) {
    matrixDrops[col].headY = -(int8_t)random(1, MATRIX_H + 1);
    matrixDrops[col].tick  = 0;
    matrixDrops[col].speed = (uint8_t)random(1, 4);
  }
}

void stepMatrixFrame() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  for (int col = 0; col < MATRIX_W; col++) {
    MatrixDrop& d = matrixDrops[col];

    d.tick++;
    if (d.tick >= d.speed) {
      d.tick = 0;
      d.headY++;
    }

    // Recycle once the entire stream (head + trail) has cleared the bottom
    if (d.headY > (int8_t)(MATRIX_H + MATRIX_TRAIL_LEN)) {
      d.headY = -(int8_t)random(1, MATRIX_H + 1);
      d.speed = (uint8_t)random(1, 4);
    }

    // Bright head pixel
    if (d.headY >= 0 && d.headY < MATRIX_H)
      setPixel(col, d.headY, matrixHeadColor);

    // Fading trail — each pixel dimmer the further above the head it is
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
