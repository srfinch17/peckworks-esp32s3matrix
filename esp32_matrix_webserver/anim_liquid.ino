// ============================================================
// SECTION 6.5: IMU (QMI8658C) + LIQUID + IMU BAR ANIMATIONS
//
// The QMI8658C is a 6-axis IMU (accelerometer + gyroscope) wired
// to the ESP32 via I2C. We only use the accelerometer here.
//
// I2C PROTOCOL RECAP:
//   Every I2C transaction starts by addressing the device (0x6B).
//   To write: send address → write register number → write value.
//   To read:  send address → write register number → restart →
//             request N bytes → read them.
//   Wire.endTransmission(false) sends a "repeated start" so the
//   bus isn't released between the address phase and the read.
// ============================================================

// Low-level I2C write to a single QMI8658C register
static void qmiWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(QMI8658_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

// Low-level I2C read from a single QMI8658C register
static uint8_t qmiRead(uint8_t reg) {
  Wire.beginTransmission(QMI8658_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);   // false = repeated start, hold the bus
  Wire.requestFrom((uint8_t)QMI8658_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;   // 0xFF = error sentinel
}

// ── initIMU ───────────────────────────────────────────────────
// Starts I2C on the board's IMU pins, checks the WHO_AM_I register
// (should NOT be 0xFF), then writes two config registers:
//   0x03 = 0x12 → enable accelerometer, ±4g range, 58.3Hz output rate
//   0x08 = 0x01 → enable sensors (power-on)
void initIMU() {
  Wire.begin(IMU_SDA, IMU_SCL);
  delay(20);   // give the IMU time to wake up after power-on
  uint8_t id = qmiRead(0x00);
  Serial.printf("QMI8658 WHO_AM_I = 0x%02X\n", id);
  if (id == 0xFF) {
    Serial.println("IMU not detected — check IMU_SDA/IMU_SCL pin definitions.");
    return;
  }
  qmiWrite(0x03, 0x12);   // accelerometer: ±4g, 58.3Hz
  qmiWrite(0x08, 0x01);   // enable sensors
  delay(50);
  imuReady = true;
  Serial.println("IMU ready.");
}

// ── readAccel ─────────────────────────────────────────────────
// Reads raw 16-bit accelerometer values for all three axes and
// converts them to g-force (gravitational units, ±4g range here).
//
// Each axis is two bytes: low byte then high byte, addresses
// 0x35-0x3A. They're combined into a signed 16-bit int with
// (xL | (xH << 8)), then divided by 8192.0 to convert to g.
// (8192 = 2^13 = half the 16-bit range / 4g full scale)
//
// NOTE: Burst reads (requesting multiple bytes in one transaction)
// fail on this particular chip revision — the register pointer
// doesn't auto-increment correctly. So each byte is fetched
// with an explicit register address. Not pretty, but it works.
void readAccel(float &ax, float &ay, float &az) {
  uint8_t xL = qmiRead(0x35), xH = qmiRead(0x36);
  uint8_t yL = qmiRead(0x37), yH = qmiRead(0x38);
  uint8_t zL = qmiRead(0x39), zH = qmiRead(0x3A);
  ax = (int16_t)(xL | (xH << 8)) / 8192.0f;
  ay = (int16_t)(yL | (yH << 8)) / 8192.0f;
  az = (int16_t)(zL | (zH << 8)) / 8192.0f;
}

// ── stepLiquidFrame ───────────────────────────────────────────
// 2D CLOSED-CONTAINER FLUID.
//   Gravity is treated as a vector in the matrix plane. Each cell gets a
//   "potential" = how far downhill it is along that vector; the fluid occupies
//   the most-downhill cells up to a threshold (liquidLevel) that springs toward
//   equilibrium with momentum. Tilt any direction → the fluid pools against the
//   low edge; rotate past a corner → it spills onto the next edge. Full 360°.
//   (Replaces the old 1D per-column heightfield that could only slosh L/R.)
//
//   1. GRAVITY DIR: derive an in-plane (gx,gy) from the accelerometer and
//      low-pass it into liquidGX/GY so it doesn't jitter.
//   2. POTENTIAL: p(x,y) = x*gx + y*gy. The 32 cells with the highest p are
//      "below the surface" (half full). Teq = the 32nd-largest p.
//   3. SLOSH: liquidLevel springs toward Teq with velocity + damping
//      (viscosity). The overshoot when gravity rotates IS the slosh.
//   4. RENDER: cell is fluid iff p >= liquidLevel. Color by depth via the
//      shared palette (or a custom top/bottom gradient); froth brightens the
//      moving surface.
void stepLiquidFrame() {
  float ax, ay, az;
  if (imuReady) readAccel(ax, ay, az);
  else          { ax = 0.0f; ay = 0.0f; az = 1.0f; }   // flat fallback if IMU failed

  // ── In-plane gravity direction ──────────────────────────────
  // IMU axis mapping — CALIBRATED ON HARDWARE 2026-06-08.
  // gxRaw is negated: tip the board right (clockwise) → fluid pools right.
  // If up/down ever reads reversed, negate gyRaw the same way.
  float gxRaw = -ay;  // → matrix +x (right)
  float gyRaw =  ax;  // → matrix +y (down)

  float mag = sqrtf(gxRaw * gxRaw + gyRaw * gyRaw);
  if (mag > 0.08f) {                         // board is tilted enough to have an in-plane direction
    float nx = gxRaw / mag, ny = gyRaw / mag;
    liquidGX += (nx - liquidGX) * 0.30f;     // low-pass toward the new direction
    liquidGY += (ny - liquidGY) * 0.30f;
  }
  // else: board ~flat (gravity into the screen) — keep the last direction.

  // ── Potential field ─────────────────────────────────────────
  float pot[NUM_LEDS];
  for (int y = 0; y < MATRIX_H; y++)
    for (int x = 0; x < MATRIX_W; x++)
      pot[y * MATRIX_W + x] = x * liquidGX + y * liquidGY;

  // Equilibrium threshold = the LIQUID_CELLS-th largest potential (descending
  // insertion sort — NUM_LEDS is only 64, so this is cheap).
  const int LIQUID_CELLS = 32;   // half full
  float sorted[NUM_LEDS];
  memcpy(sorted, pot, sizeof(pot));
  for (int i = 1; i < NUM_LEDS; i++) {
    float v = sorted[i]; int j = i - 1;
    while (j >= 0 && sorted[j] < v) { sorted[j + 1] = sorted[j]; j--; }
    sorted[j + 1] = v;
  }
  float Teq     = sorted[LIQUID_CELLS - 1];   // 32nd-largest
  float deepest = sorted[0];                  // most-downhill cell present

  // ── Slosh: spring liquidLevel toward equilibrium with momentum ──
  float stiffness = 0.18f * constrain(mag, 0.0f, 1.0f);   // stronger tilt = snappier
  if (stiffness < 0.02f) stiffness = 0.02f;               // always settle eventually
  liquidLevelVel += (Teq - liquidLevel) * stiffness;
  liquidLevelVel *= liquidDamping;                        // viscosity
  liquidLevel    += liquidLevelVel;

  float turb  = constrain(fabsf(liquidLevelVel) * 1.6f, 0.0f, 1.0f);  // froth amount
  float range = deepest - liquidLevel;                                 // surface→bottom span
  if (range < 1.0f) range = 1.0f;
  float surfaceBand = range / MATRIX_H;

  // ── Render ──────────────────────────────────────────────────
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (int y = 0; y < MATRIX_H; y++) {
    for (int x = 0; x < MATRIX_W; x++) {
      float p = pot[y * MATRIX_W + x];
      if (p < liquidLevel) continue;          // above the surface → empty

      float depth = constrain((p - liquidLevel) / range, 0.0f, 1.0f);  // 0 surface, 1 deep
      bool  isSurface = (p - liquidLevel) < surfaceBand;

      CRGB col;
      if (liquidGradient) {
        // Custom: lerp deep→top by surface proximity (s=1 at surface, 0 deep).
        float s = 1.0f - depth;
        col = CRGB(
          liquidBottomColor.r + (int)((liquidTopColor.r - liquidBottomColor.r) * s),
          liquidBottomColor.g + (int)((liquidTopColor.g - liquidBottomColor.g) * s),
          liquidBottomColor.b + (int)((liquidTopColor.b - liquidBottomColor.b) * s));
      } else {
        // Palette (reuses fire's activePalette): surface = bright, deep = darker.
        uint8_t h = (uint8_t)(210.0f - depth * 100.0f);
        col = heatToColor(h);
      }

      // Froth: whiten/boost the moving surface.
      if (isSurface) {
        uint8_t f = (uint8_t)(turb * 150.0f);
        col.r = qadd8(col.r, f);
        col.g = qadd8(col.g, f);
        col.b = qadd8(col.b, f);
      }
      setPixel(x, y, col);
    }
  }
}

// ── stepImuFrame ──────────────────────────────────────────────
// Renders live accelerometer data as three vertical bar graphs,
// one per axis. Useful for debugging tilt or just looking cool.
//
// Layout: columns 0-1 = X axis (red), 3-4 = Y axis (green), 6-7 = Z axis (blue)
//
// Each bar is centered on the middle two rows (3-4), which serve
// as a visual zero reference line (drawn at 25% brightness).
// Positive g tilts the bar upward; negative tilts it downward.
// g is clamped to ±1g (beyond that the display is maxed out).
void stepImuFrame() {
  float ax, ay, az;
  if (imuReady) readAccel(ax, ay, az);
  else          { ax = 0; ay = 0; az = 0; }

  fill_solid(leds, NUM_LEDS, CRGB::Black);

  // drawBar: draws a 2-column-wide bar graph for one axis.
  // g: the g-force value; gRow: which row the tip of the bar reaches.
  // The neutral zone (rows 3-4) is drawn dimly for reference.
  auto drawBar = [&](int colA, int colB, float g, CRGB color) {
    g = constrain(g, -1.0f, 1.0f);
    // Map g range [-1, +1] to row range [7, 0]:
    // g=+1 → row 0 (top), g=0 → row 3.5 (middle), g=-1 → row 7 (bottom)
    int gRow = (int)(3.5f - g * 3.5f + 0.5f);
    gRow = constrain(gRow, 0, 7);

    // Draw neutral line at rows 3-4 (always visible)
    setPixel(colA, 3, color.scale8(40)); setPixel(colB, 3, color.scale8(40));
    setPixel(colA, 4, color.scale8(40)); setPixel(colB, 4, color.scale8(40));

    // Draw filled bar from neutral toward the g-force direction
    if (g >= 0) {
      for (int y = gRow; y <= 3; y++) { setPixel(colA, y, color); setPixel(colB, y, color); }
    } else {
      for (int y = 4; y <= gRow; y++) { setPixel(colA, y, color); setPixel(colB, y, color); }
    }
  };

  drawBar(0, 1, ax, CRGB(180, 0,   0));    // X axis — red
  drawBar(3, 4, ay, CRGB(0,  160,  0));    // Y axis — green
  drawBar(6, 7, az, CRGB(0,   80, 200));   // Z axis — blue
}
