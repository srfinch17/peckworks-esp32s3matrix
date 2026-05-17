// ============================================================
// SECTION 6.9: TIMER ANIMATIONS + CLOCK
// Three countdown styles: gradient fill, snowfall, numeric text.
// timerColor1 = start (bottom/early), timerColor2 = end (top/late).
// ============================================================

// 3×5 pixel mini font for digits 0-9.
// Each digit = 3 column bytes, bit0 = top row, bit4 = bottom row.
const uint8_t MINI_FONT[10][3] PROGMEM = {
  {31, 17, 31},  // 0
  { 2, 31, 16},  // 1
  {29, 21, 23},  // 2
  {17, 21, 31},  // 3
  { 7,  4, 31},  // 4
  {23, 21, 29},  // 5
  {31, 21, 29},  // 6
  {25,  5,  3},  // 7
  {31, 21, 31},  // 8
  {23, 21, 31},  // 9
};
#define MINI_FONT_W 3

// 2×4 compact font for units digits in the 13-19 timer range.
const uint8_t FONT_2X4[10][2] PROGMEM = {
  {0, 0},            // 0 — special
  {0, 0},            // 1 — special
  {0b1101, 0b1011},  // 2
  {0b0101, 0b1111},  // 3
  {0b0011, 0b1110},  // 4
  {0b0111, 0b1101},  // 5
  {0b1111, 0b1101},  // 6
  {0b0001, 0b1111},  // 7
  {0b1111, 0b0101},  // 8
  {0b0011, 0b1111},  // 9
};

// 2×3 compact font for tens digits 2-5 (used in 20-59 timer range).
const uint8_t COMPACT_TENS[4][2] PROGMEM = {
  {0b111, 0b101},  // 2
  {0b101, 0b111},  // 3
  {0b011, 0b110},  // 4
  {0b111, 0b011},  // 5
};

// ── Time display templates (H:MM pixel layout) ─────────────────────────────────
// Used by clock mode (NTP 12-hour, H:MM) and timer_text (M:SS countdown).
// See original file header for full pixel map documentation.
// ─────────────────────────────────────────────────────────────────────────────

void drawTimeDisplay(int hVal, int mVal, CRGB color) {
  setPixel(0, 5, color);
  setPixel(0, 7, color);

  int mTens  = (mVal / 10) % 10;
  int mUnits = mVal % 10;
  for (int col = 0; col < 3; col++) {
    uint8_t bT = pgm_read_byte(&MINI_FONT[mTens][col]);
    uint8_t bU = pgm_read_byte(&MINI_FONT[mUnits][col]);
    for (int row = 0; row < 5; row++) {
      if ((bT >> row) & 1) setPixel(col + 2, row + 3, color);
      if ((bU >> row) & 1) setPixel(col + 5, row + 3, color);
    }
  }

  if (hVal >= 0 && hVal <= 9) {
    for (int col = 0; col < 3; col++) {
      uint8_t bits = pgm_read_byte(&MINI_FONT[hVal][col]);
      for (int row = 0; row < 5; row++) {
        if ((bits >> row) & 1) {
          if (col == 2 && row == 4) continue;
          setPixel(col, row, color);
        }
      }
    }
  } else if (hVal == 10) {
    for (int r = 0; r <= 3; r++) setPixel(0, r, color);
    setPixel(2,0,color); setPixel(3,0,color); setPixel(4,0,color);
    setPixel(2,1,color);                       setPixel(4,1,color);
    setPixel(2,2,color); setPixel(3,2,color); setPixel(4,2,color);
  } else if (hVal == 11) {
    for (int r = 0; r <= 3; r++) { setPixel(0, r, color); setPixel(2, r, color); }
  } else if (hVal == 12) {
    for (int r = 0; r <= 3; r++) setPixel(0, r, color);
    setPixel(1,0,color); setPixel(2,0,color);
                          setPixel(2,1,color);
    setPixel(1,2,color);
    setPixel(1,3,color); setPixel(2,3,color);
  } else if (hVal >= 13 && hVal <= 19) {
    for (int r = 0; r <= 3; r++) setPixel(0, r, color);
    int units = hVal % 10;
    if (units == 0) {
      setPixel(2,0,color); setPixel(3,0,color); setPixel(4,0,color);
      setPixel(2,1,color);                       setPixel(4,1,color);
      setPixel(2,2,color); setPixel(3,2,color); setPixel(4,2,color);
    } else if (units == 1) {
      for (int r = 0; r <= 3; r++) setPixel(2, r, color);
    } else {
      uint8_t c0 = pgm_read_byte(&FONT_2X4[units][0]);
      uint8_t c1 = pgm_read_byte(&FONT_2X4[units][1]);
      for (int row = 0; row < 4; row++) {
        if ((c0 >> row) & 1) setPixel(1, row, color);
        if ((c1 >> row) & 1) setPixel(2, row, color);
      }
    }
  } else if (hVal >= 20 && hVal <= 59) {
    int tens  = hVal / 10;
    int units = hVal % 10;
    uint8_t ct0 = pgm_read_byte(&COMPACT_TENS[tens - 2][0]);
    uint8_t ct1 = pgm_read_byte(&COMPACT_TENS[tens - 2][1]);
    for (int row = 0; row < 3; row++) {
      if ((ct0 >> row) & 1) setPixel(0, row, color);
      if ((ct1 >> row) & 1) setPixel(1, row, color);
    }
    for (int col = 0; col < 3; col++) {
      uint8_t bits = pgm_read_byte(&MINI_FONT[units][col]) & 0x07;
      for (int row = 0; row < 3; row++)
        if ((bits >> row) & 1) setPixel(col + 3, row, color);
    }
  }
}

CRGB blendColors(CRGB a, CRGB b, float t) {
  t = constrain(t, 0.0f, 1.0f);
  return CRGB(
    (uint8_t)(a.r + ((int)b.r - (int)a.r) * t),
    (uint8_t)(a.g + ((int)b.g - (int)a.g) * t),
    (uint8_t)(a.b + ((int)b.b - (int)a.b) * t)
  );
}

void stepTimerExpiredFrame() {
  uint32_t now = millis();
  if (timerExpiredState == 0) { timerExpiredState = 1; timerExpiredMs = now; }
  if (timerExpiredState == 1) {
    if ((now - timerExpiredMs) >= 10000) {
      timerExpiredState = 2;
    } else {
      fill_solid(leds, NUM_LEDS, ((now / 500) % 2 == 0) ? timerColor2 : CRGB::Black);
    }
  } else {
    fill_solid(leds, NUM_LEDS, timerColor2);
  }
}

// ── Timer 1: Gradient Fill ────────────────────────────────────
void stepTimerFillFrame() {
  uint32_t now = millis();
  long remaining = (long)(timerEndMs - now);
  if (remaining <= 0) { stepTimerExpiredFrame(); return; }

  float progress = 1.0f - (float)remaining / (float)timerTotalMs;
  int targetLit  = constrain((int)(progress * 64.0f), 0, 64);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (int i = 0; i < targetLit; i++) {
    int row  = 7 - (i / 8);
    int col  = i % 8;
    float rowT = (7.0f - row) / 7.0f;
    setPixel(col, row, blendColors(timerColor1, timerColor2, rowT));
  }
}

// ── Timer 2: Snowfall Fill ────────────────────────────────────
// Settlement is purely time-based (elapsed/total * 64 cells).
// Fall animation is cosmetic — runs concurrently, never blocks timing.
void stepTimerSnowFrame() {
  uint32_t now = millis();
  long remaining = (long)(timerEndMs - now);
  if (remaining <= 0) { stepTimerExpiredFrame(); return; }

  fill_solid(leds, NUM_LEDS, CRGB::Black);

  // How many of 64 cells should be settled by now
  float elapsed       = (float)timerTotalMs - (float)remaining;
  int   targetSettled = constrain((int)(elapsed / (float)timerTotalMs * 64.0f), 0, 63);

  if (targetSettled > snowSettledCount) {
    snowSettledCount = targetSettled;
    snowFallActive   = false; // restart fall for the new next-cell
  }

  // Draw all settled cells
  for (int i = 0; i < snowSettledCount; i++) {
    int c    = snowPos[i].col;
    int matY = 7 - snowPos[i].row;
    float rowT = snowPos[i].row / 7.0f;
    setPixel(c, matY, blendColors(timerColor1, timerColor2, rowT));
  }

  // Fall animation for the next cell to settle (purely visual)
  if (snowSettledCount < 64) {
    SnowCell next    = snowPos[snowSettledCount];
    int targetMatY   = 7 - next.row;
    float rowT       = next.row / 7.0f;
    CRGB flakeColor  = blendColors(timerColor1, timerColor2, rowT);

    if (!snowFallActive) {
      snowFallActive     = true;
      snowFallStartMs    = now;
      snowFallCol        = next.col;
      snowFallTargetY    = targetMatY;
      float perFlakeMs   = (float)timerTotalMs / 64.0f;
      snowFallDurationMs = (uint32_t)constrain(perFlakeMs * 0.75f, 80.0f, 2500.0f);
    }

    float t    = constrain((float)(now - snowFallStartMs) / (float)snowFallDurationMs, 0.0f, 1.0f);
    int   drawY = constrain((int)(t * snowFallTargetY + 0.5f), 0, snowFallTargetY);
    if (drawY < MATRIX_H)
      setPixel(snowFallCol, drawY, (drawY < snowFallTargetY) ? flakeColor : flakeColor.scale8(160));
  }
}

// ── Timer 3: MM:SS Countdown ──────────────────────────────────
// Layout: rows 0-2 = minutes (3×3 font), rows 3-7 = seconds (3×5 font)
// Colon dots at col 0, rows 4 and 6. Minutes area blanks when minutes == 0.
void stepTimerTextFrame() {
  uint32_t now = millis();
  long remaining = (long)(timerEndMs - now);
  if (remaining <= 0) { stepTimerExpiredFrame(); return; }

  fill_solid(leds, NUM_LEDS, CRGB::Black);

  int totalSec = (int)((remaining + 999) / 1000);
  int minutes  = min(totalSec / 60, 99);
  int seconds  = totalSec % 60;

  if (minutes > 0) {
    drawChar3x3('0' + minutes / 10, 0, 0, timerColor1);  // rowOffset 0 = ROW_3X3_TOP
    drawChar3x3('0' + minutes % 10, 4, 0, timerColor1);
  }

  setPixel(0, 4, timerColorColon);
  setPixel(0, 6, timerColorColon);

  drawChar3x5('0' + seconds / 10, 1, 3, timerColor2);   // rowOffset 3 = ROW_3X5_BOTTOM
  drawChar3x5('0' + seconds % 10, 5, 3, timerColor2);
}

// ── Clock Mode (NTP) ─────────────────────────────────────────
void stepClockFrame() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) {
    uint8_t pulse = (uint8_t)(128 + 60 * sinf(millis() / 800.0f));
    fill_solid(leds, NUM_LEDS, CRGB(
      (uint8_t)((uint32_t)clockBgColor.r * pulse / 255),
      (uint8_t)((uint32_t)clockBgColor.g * pulse / 255),
      (uint8_t)((uint32_t)clockBgColor.b * pulse / 255)
    ));
    return;
  }
  ntpSynced = true;

  int h = timeinfo.tm_hour % 12;
  if (h == 0) h = 12;
  int m = timeinfo.tm_min;

  if (h == clockPrevHour && m == clockPrevMin) return;
  clockPrevHour = h;
  clockPrevMin  = m;

  fill_solid(leds, NUM_LEDS, clockBgColor);

  CHSV bgHSV = rgb2hsv_approximate(clockBgColor);
  CRGB digitColor = CHSV(bgHSV.h, 50, 220);

  drawTimeDisplay(h, m, digitColor);
}
