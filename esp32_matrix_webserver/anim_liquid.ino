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
// Simulates fluid sloshing inside the matrix in response to
// physical board tilt detected by the accelerometer.
//
// PHYSICS MODEL:
//   Each column has a float surface height (liquidHeight[x]) and
//   vertical velocity (liquidVelocity[x]). Each frame:
//
//   1. TILT FORCE: atan2(-ay, az) converts the gravity vector
//      into a tilt angle, which becomes a target height slope.
//      When you tilt the board right, the fluid "wants" to pile
//      up on the right side — a higher target height there pulls
//      the surface up via a spring force.
//
//   2. WAVE PROPAGATION: each column also has a restoring force
//      from its neighbors (shallow water wave equation).
//      (lh + rh - 2*prev[x]) is a discrete second derivative —
//      it creates surface tension that spreads waves outward.
//
//   3. DAMPING: liquidVelocity is multiplied by liquidDamping
//      (< 1.0) each frame so the fluid eventually settles.
//      viscosity param from the API maps to damping coefficient:
//      lower viscosity = less damping = more sloshy.
//
//   4. CONSERVATION: a drift correction keeps the average surface
//      height at MATRIX_H/2 so the fluid doesn't all pile to one side.
//
// RENDERING:
//   Pixels at or below the surface are lit (teal, brighter at
//   the surface, darker at depth). Turbulence brightens the
//   surface pixels — the faster a column is moving, the whiter it is.
void stepLiquidFrame() {
  float ax, ay, az;
  if (imuReady) readAccel(ax, ay, az);
  else          { ax = 0.0f; ay = 0.0f; az = 1.0f; }   // flat fallback if IMU failed

  // atan2(-ay, az) gives the board tilt angle in radians.
  // Dividing by (PI/2) normalizes it to roughly -1..+1:
  //   0 = flat, +1 = vertical right side down, -1 = vertical left side down.
  float tilt    = atan2f(-ay, az) / (M_PI * 0.5f);
  float slopeMax = 3.5f;   // max height difference across the matrix at full tilt
  float xCenter  = (MATRIX_W - 1) * 0.5f;

  float prev[MATRIX_W];
  memcpy(prev, liquidHeight, sizeof(float) * MATRIX_W);   // snapshot before update

  for (int x = 0; x < MATRIX_W; x++) {
    float lh = prev[x > 0          ? x - 1 : x];   // left neighbor height
    float rh = prev[x < MATRIX_W-1 ? x + 1 : x];   // right neighbor height

    // Tilt-driven target height: left side goes up when tilting right
    float xNorm  = (x - xCenter) / xCenter;   // -1 at left edge, +1 at right edge
    float target = MATRIX_H * 0.5f - tilt * slopeMax * xNorm;

    // Spring force toward tilt target + wave propagation from neighbors
    liquidVelocity[x] += (target - prev[x]) * 0.12f;           // tilt spring
    liquidVelocity[x] += (lh + rh - 2.0f * prev[x]) * 0.18f;  // wave spring
    liquidVelocity[x] *= liquidDamping;   // energy dissipation
    liquidHeight[x]    = prev[x] + liquidVelocity[x];
  }

  // Conservation pass: remove accumulated drift so total fluid volume stays constant
  float avg = 0;
  for (int x = 0; x < MATRIX_W; x++) avg += liquidHeight[x];
  avg /= MATRIX_W;
  float drift = avg - MATRIX_H * 0.5f;
  for (int x = 0; x < MATRIX_W; x++)
    liquidHeight[x] = constrain(liquidHeight[x] - drift, 0.0f, (float)(MATRIX_H - 1));

  // Render the fluid
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (int x = 0; x < MATRIX_W; x++) {
    int   surf = (int)liquidHeight[x];
    // turb: how fast this column is moving. 0=still, 1=very turbulent.
    float turb = constrain(fabsf(liquidVelocity[x]) * 6.0f, 0.0f, 1.0f);

    for (int y = surf; y < MATRIX_H; y++) {
      int   span  = MATRIX_H - surf;
      // depth: 0 at the surface, 1 at the bottom
      float depth = (span > 1) ? (float)(y - surf) / (float)(span - 1) : 0.0f;

      // Water color: bright teal at surface → dark blue at bottom
      uint8_t v = (uint8_t)((1.0f - depth * 0.5f) * 200.0f);
      CRGB col = CRGB(0, v >> 1, v);

      // Turbulence: add white-ish glow at the surface when the column is moving fast
      if (y == surf) {
        uint8_t f = (uint8_t)(turb * 160.0f);
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
