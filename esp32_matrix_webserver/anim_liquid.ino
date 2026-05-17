// ============================================================
// SECTION 6.5: IMU (QMI8658C) + LIQUID ANIMATION
// ============================================================

static void qmiWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(QMI8658_ADDR);
  Wire.write(reg); Wire.write(val);
  Wire.endTransmission();
}

static uint8_t qmiRead(uint8_t reg) {
  Wire.beginTransmission(QMI8658_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)QMI8658_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

void initIMU() {
  Wire.begin(IMU_SDA, IMU_SCL);
  delay(20);
  uint8_t id = qmiRead(0x00);
  Serial.printf("QMI8658 WHO_AM_I = 0x%02X\n", id);
  if (id == 0xFF) {
    Serial.println("IMU not detected — check IMU_SDA/IMU_SCL pin definitions.");
    return;
  }
  qmiWrite(0x03, 0x12);
  qmiWrite(0x08, 0x01);
  delay(50);
  imuReady = true;
  Serial.println("IMU ready.");
}

// Burst reads fail on this chip — each byte fetched with explicit register address.
void readAccel(float &ax, float &ay, float &az) {
  uint8_t xL = qmiRead(0x35), xH = qmiRead(0x36);
  uint8_t yL = qmiRead(0x37), yH = qmiRead(0x38);
  uint8_t zL = qmiRead(0x39), zH = qmiRead(0x3A);
  ax = (int16_t)(xL | (xH << 8)) / 8192.0f;
  ay = (int16_t)(yL | (yH << 8)) / 8192.0f;
  az = (int16_t)(zL | (zH << 8)) / 8192.0f;
}

// Simulates fluid sloshing in response to physical board tilt.
// Each column has a float surface height and velocity.
// atan2(-ay, az) gives correct tilt for full 360-degree rotation:
//   flat=0, vertical=±1, upside-down=±2
void stepLiquidFrame() {
  float ax, ay, az;
  if (imuReady) readAccel(ax, ay, az);
  else          { ax = 0.0f; ay = 0.0f; az = 1.0f; }

  float tilt    = atan2f(-ay, az) / (M_PI * 0.5f);
  float slopeMax = 3.5f;
  float xCenter  = (MATRIX_W - 1) * 0.5f;

  float prev[MATRIX_W];
  memcpy(prev, liquidHeight, sizeof(float) * MATRIX_W);

  for (int x = 0; x < MATRIX_W; x++) {
    float lh = prev[x > 0          ? x - 1 : x];
    float rh = prev[x < MATRIX_W-1 ? x + 1 : x];

    float xNorm  = (x - xCenter) / xCenter;
    float target = MATRIX_H * 0.5f - tilt * slopeMax * xNorm;

    liquidVelocity[x] += (target - prev[x]) * 0.12f;
    liquidVelocity[x] += (lh + rh - 2.0f * prev[x]) * 0.18f;
    liquidVelocity[x] *= liquidDamping;
    liquidHeight[x]    = prev[x] + liquidVelocity[x];
  }

  float avg = 0;
  for (int x = 0; x < MATRIX_W; x++) avg += liquidHeight[x];
  avg /= MATRIX_W;
  float drift = avg - MATRIX_H * 0.5f;
  for (int x = 0; x < MATRIX_W; x++)
    liquidHeight[x] = constrain(liquidHeight[x] - drift, 0.0f, (float)(MATRIX_H - 1));

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (int x = 0; x < MATRIX_W; x++) {
    int   surf = (int)liquidHeight[x];
    float turb = constrain(fabsf(liquidVelocity[x]) * 6.0f, 0.0f, 1.0f);

    for (int y = surf; y < MATRIX_H; y++) {
      int   span  = MATRIX_H - surf;
      float depth = (span > 1) ? (float)(y - surf) / (float)(span - 1) : 0.0f;

      // Water color: bright teal at surface → dark blue at bottom (always visible)
      uint8_t v = (uint8_t)((1.0f - depth * 0.5f) * 200.0f);
      CRGB col = CRGB(0, v >> 1, v);

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

// ============================================================
// SECTION 6.6: IMU VISUALIZER — three live bar graphs
// Red cols 0-1 = ax, Green cols 3-4 = ay, Blue cols 6-7 = az
// ============================================================
void stepImuFrame() {
  float ax, ay, az;
  if (imuReady) readAccel(ax, ay, az);
  else          { ax = 0; ay = 0; az = 0; }

  fill_solid(leds, NUM_LEDS, CRGB::Black);

  auto drawBar = [&](int colA, int colB, float g, CRGB color) {
    g = constrain(g, -1.0f, 1.0f);
    int gRow = (int)(3.5f - g * 3.5f + 0.5f);
    gRow = constrain(gRow, 0, 7);
    setPixel(colA, 3, color.scale8(40)); setPixel(colB, 3, color.scale8(40));
    setPixel(colA, 4, color.scale8(40)); setPixel(colB, 4, color.scale8(40));
    if (g >= 0) {
      for (int y = gRow; y <= 3; y++) { setPixel(colA, y, color); setPixel(colB, y, color); }
    } else {
      for (int y = 4; y <= gRow; y++) { setPixel(colA, y, color); setPixel(colB, y, color); }
    }
  };

  drawBar(0, 1, ax, CRGB(180, 0, 0));
  drawBar(3, 4, ay, CRGB(0, 160, 0));
  drawBar(6, 7, az, CRGB(0, 80, 200));
}
