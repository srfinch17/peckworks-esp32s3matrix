// ============================================================
// SECTION 6.9: TIMER ANIMATIONS + CLOCK
//
// THREE TIMER MODES — all share the same start/end time:
//   timer_fill — LEDs fill up from the bottom like a progress bar
//   timer_snow — LEDs "settle" like falling snow from the top
//   timer_text — MM:SS countdown displayed as digits
//
// CLOCK MODE (NTP):
//   Syncs to pool.ntp.org via WiFi and displays a live 12-hour
//   clock using the H:MM layout (see drawTimeDisplay below).
//   The clock only redraws when the minute actually changes,
//   keeping CPU usage near zero between updates.
//
// COLOR PARAMETERS:
//   timerColor1 = start/bottom/minutes color
//   timerColor2 = end/top/seconds color
//   timerColorColon = colon color (timer_text only)
// ============================================================

// ── MINI_FONT ─────────────────────────────────────────────────
// 3×5 pixel font for digits 0-9.
// Each digit is stored as 3 column bytes (col0, col1, col2).
// Bit encoding: bit0 = top row, bit4 = bottom row.
// PROGMEM: stored in flash to save RAM.
const uint8_t MINI_FONT[10][3] PROGMEM = {
  {31, 17, 31},  // 0 — full rectangle with hollow center
  { 2, 31, 16},  // 1 — right column only (vertically centered)
  {29, 21, 23},  // 2
  {17, 21, 31},  // 3
  { 7,  4, 31},  // 4
  {23, 21, 29},  // 5
  {31, 21, 29},  // 6
  {25,  5,  3},  // 7
  {31, 21, 31},  // 8 — all pixels
  {23, 21, 31},  // 9
};
#define MINI_FONT_W 3

// ── FONT_2X4 ──────────────────────────────────────────────────
// Compact 2×4 font for digits 2-9.
// Used for the units digit in the 13-19 range (timer_text).
// 0 and 1 are handled as special cases inline.
const uint8_t FONT_2X4[10][2] PROGMEM = {
  {0, 0},            // 0 — special case
  {0, 0},            // 1 — special case
  {0b1101, 0b1011},  // 2
  {0b0101, 0b1111},  // 3
  {0b0011, 0b1110},  // 4
  {0b0111, 0b1101},  // 5
  {0b1111, 0b1101},  // 6
  {0b0001, 0b1111},  // 7
  {0b1111, 0b0101},  // 8
  {0b0011, 0b1111},  // 9
};

// ── COMPACT_TENS ──────────────────────────────────────────────
// 2×3 font for tens digits 2-5 (used in the 20-59 range of timer_text).
// Index 0 = digit "2", index 3 = digit "5".
const uint8_t COMPACT_TENS[4][2] PROGMEM = {
  {0b111, 0b101},  // 2
  {0b101, 0b111},  // 3
  {0b011, 0b110},  // 4
  {0b111, 0b011},  // 5
};

// ── drawTimeDisplay ───────────────────────────────────────────
// Renders a time value in H:MM format on the 8×8 matrix.
// Used by both the clock mode (hours 1-12) and timer_text mode
// (minutes 0-59).
//
// LAYOUT:
//   Rows 0-2: hours digit (top half, various font sizes by range)
//   Rows 3-7: minutes tens and units (3×5 MINI_FONT, two digits)
//   Col 0, rows 5 and 7: colon dots
//
// WHY SO MANY CASES:
//   Hours 1-9 use a 3×5 font (clipped to rows 0-3).
//   Hours 10-12 are drawn pixel-by-pixel because they need to fit
//   in 3-4 columns at the top of the matrix — no pre-made font fits.
//   Hours 13-19 use a "1" bar + FONT_2X4 for the units.
//   Hours 20-59 use COMPACT_TENS for the tens digit + MINI_FONT top-3 rows.
//   (The 20-59 range is used in timer_text mode where hVal is minutes.)
void drawTimeDisplay(int hVal, int mVal, CRGB color) {
  // Colon dots — two pixels in col 0 at rows 5 and 7
  setPixel(0, 5, color);
  setPixel(0, 7, color);

  // Minutes digits — always the same: MINI_FONT, cols 2-4 (tens) and 5-7 (units)
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

  // Hours digit — rendering depends on value range
  if (hVal >= 0 && hVal <= 9) {
    // Single digit 1-9: use MINI_FONT clipped to 4 rows (skip bottom-right corner
    // to avoid overlapping the colon dot at col2,row7)
    for (int col = 0; col < 3; col++) {
      uint8_t bits = pgm_read_byte(&MINI_FONT[hVal][col]);
      for (int row = 0; row < 5; row++) {
        if ((bits >> row) & 1) {
          if (col == 2 && row == 4) continue;   // skip this pixel — it's in the colon zone
          setPixel(col, row, color);
        }
      }
    }
  } else if (hVal == 10) {
    // "10": vertical bar at col 0 + small "0" glyph at cols 2-4
    for (int r = 0; r <= 3; r++) setPixel(0, r, color);
    setPixel(2,0,color); setPixel(3,0,color); setPixel(4,0,color);
    setPixel(2,1,color);                       setPixel(4,1,color);
    setPixel(2,2,color); setPixel(3,2,color); setPixel(4,2,color);
  } else if (hVal == 11) {
    // "11": two vertical bars side by side
    for (int r = 0; r <= 3; r++) { setPixel(0, r, color); setPixel(2, r, color); }
  } else if (hVal == 12) {
    // "12": vertical bar + hand-drawn "2"
    for (int r = 0; r <= 3; r++) setPixel(0, r, color);
    setPixel(1,0,color); setPixel(2,0,color);
                          setPixel(2,1,color);
    setPixel(1,2,color);
    setPixel(1,3,color); setPixel(2,3,color);
  } else if (hVal >= 13 && hVal <= 19) {
    // "13"-"19": vertical bar for "1" + FONT_2X4 for units
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
    // "20"-"59": COMPACT_TENS for tens + top-3 rows of MINI_FONT for units
    // This range handles minute values in timer_text mode.
    int tens  = hVal / 10;
    int units = hVal % 10;
    uint8_t ct0 = pgm_read_byte(&COMPACT_TENS[tens - 2][0]);
    uint8_t ct1 = pgm_read_byte(&COMPACT_TENS[tens - 2][1]);
    for (int row = 0; row < 3; row++) {
      if ((ct0 >> row) & 1) setPixel(0, row, color);
      if ((ct1 >> row) & 1) setPixel(1, row, color);
    }
    // Clip MINI_FONT to just the top 3 rows (bits 0-2) to stay out of the colon zone
    for (int col = 0; col < 3; col++) {
      uint8_t bits = pgm_read_byte(&MINI_FONT[units][col]) & 0x07;
      for (int row = 0; row < 3; row++)
        if ((bits >> row) & 1) setPixel(col + 3, row, color);
    }
  }
}

// Linearly interpolates between two CRGB colors.
// t=0 returns a, t=1 returns b.
CRGB blendColors(CRGB a, CRGB b, float t) {
  t = constrain(t, 0.0f, 1.0f);
  return CRGB(
    (uint8_t)(a.r + ((int)b.r - (int)a.r) * t),
    (uint8_t)(a.g + ((int)b.g - (int)a.g) * t),
    (uint8_t)(a.b + ((int)b.b - (int)a.b) * t)
  );
}

// ── stepTimerExpiredFrame ─────────────────────────────────────
// Plays a 10-second blink-to-solid expiry animation when any
// timer reaches zero. timerExpiredState cycles through:
//   0 → 1: first call — record the start time
//   1:     blink at 2Hz (500ms on/off) using timerColor2
//   2:     timer is expired, hold solid timerColor2 indefinitely
void stepTimerExpiredFrame() {
  uint32_t now = millis();
  if (timerExpiredState == 0) { timerExpiredState = 1; timerExpiredMs = now; }
  if (timerExpiredState == 1) {
    if ((now - timerExpiredMs) >= 10000) {
      timerExpiredState = 2;
    } else {
      // Blink: on for 500ms, off for 500ms
      fill_solid(leds, NUM_LEDS, ((now / 500) % 2 == 0) ? timerColor2 : CRGB::Black);
    }
  } else {
    fill_solid(leds, NUM_LEDS, timerColor2);
  }
}

// ── Timer 1: Gradient Fill ────────────────────────────────────
// LEDs light up from the bottom-left corner moving right then up,
// like a progress bar sweeping the matrix.
// The gradient blends from timerColor1 (bottom) to timerColor2 (top).
//
// progress: 0.0 when just started, 1.0 when done.
// targetLit: how many LEDs should be lit at this moment (0-64).
// LEDs are ordered row-major from the bottom: row 7 first, then row 6, etc.
void stepTimerFillFrame() {
  uint32_t now = millis();
  long remaining = (long)(timerEndMs - now);
  if (remaining <= 0) { stepTimerExpiredFrame(); return; }

  float progress = 1.0f - (float)remaining / (float)timerTotalMs;
  int targetLit  = constrain((int)(progress * 64.0f), 0, 64);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (int i = 0; i < targetLit; i++) {
    int row  = 7 - (i / 8);   // start at row 7 (bottom), move up
    int col  = i % 8;
    float rowT = (7.0f - row) / 7.0f;   // 0 at bottom, 1 at top
    setPixel(col, row, blendColors(timerColor1, timerColor2, rowT));
  }
}

// ── Timer 2: Snowfall Fill ────────────────────────────────────
// LEDs "settle" into place from the bottom up, like snow accumulating.
// The 64 cells settle in a randomized-within-row order (shuffled in
// handleAnimation → Fisher-Yates within each row).
//
// TWO PARALLEL TRACKS:
//   Settlement: purely time-based. (elapsed/total * 64) cells settle
//               by now — this is the authoritative count. It never
//               drifts or speeds up based on animation timing.
//   Fall animation: cosmetic. A single flake animates falling down to
//               the next unsettled position. It doesn't affect timing.
//
// This separation means the timer is always accurate even if the
// frame rate varies.
void stepTimerSnowFrame() {
  uint32_t now = millis();
  long remaining = (long)(timerEndMs - now);
  if (remaining <= 0) { stepTimerExpiredFrame(); return; }

  fill_solid(leds, NUM_LEDS, CRGB::Black);

  // How many cells should be settled by this point in time
  float elapsed       = (float)timerTotalMs - (float)remaining;
  int   targetSettled = constrain((int)(elapsed / (float)timerTotalMs * 64.0f), 0, 63);

  if (targetSettled > snowSettledCount) {
    snowSettledCount = targetSettled;
    snowFallActive   = false;   // trigger a fresh fall animation for the next cell
  }

  // Draw all cells that have already settled
  for (int i = 0; i < snowSettledCount; i++) {
    int c    = snowPos[i].col;
    int matY = 7 - snowPos[i].row;   // snowPos.row: 0=bottom, 7=top → matrix y: 7=bottom, 0=top
    float rowT = snowPos[i].row / 7.0f;
    setPixel(c, matY, blendColors(timerColor1, timerColor2, rowT));
  }

  // Animate the next flake falling toward its settled position
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
      // Fall duration: 75% of the per-cell time budget, clamped 80ms-2500ms
      float perFlakeMs   = (float)timerTotalMs / 64.0f;
      snowFallDurationMs = (uint32_t)constrain(perFlakeMs * 0.75f, 80.0f, 2500.0f);
    }

    // t: 0 = just started falling, 1 = reached destination
    float t    = constrain((float)(now - snowFallStartMs) / (float)snowFallDurationMs, 0.0f, 1.0f);
    int   drawY = constrain((int)(t * snowFallTargetY + 0.5f), 0, snowFallTargetY);
    if (drawY < MATRIX_H)
      setPixel(snowFallCol, drawY, (drawY < snowFallTargetY) ? flakeColor : flakeColor.scale8(160));
  }
}

// ── Timer 3: MM:SS Countdown ──────────────────────────────────
// Displays remaining time as M:SS (or MM:SS) in the top half,
// and seconds (SS) in the bottom half. Uses drawTimeDisplay()
// which handles any minute value from 0 to 59.
//
// Layout (rows):
//   0-2: minutes value (top 3 rows via compact fonts)
//   3-7: seconds value (3×5 MINI_FONT)
//   col 0, rows 4 and 6: colon dots
void stepTimerTextFrame() {
  uint32_t now = millis();
  long remaining = (long)(timerEndMs - now);
  if (remaining <= 0) { stepTimerExpiredFrame(); return; }

  fill_solid(leds, NUM_LEDS, CRGB::Black);

  // Round up to the next whole second so the display reads "1" until it hits 0
  int totalSec = (int)((remaining + 999) / 1000);
  int minutes  = min(totalSec / 60, 99);
  int seconds  = totalSec % 60;

  // Minutes in top-left (rows 0-2), only drawn if minutes > 0
  if (minutes > 0) {
    drawChar3x3('0' + minutes / 10, 0, 0, timerColor1);
    drawChar3x3('0' + minutes % 10, 4, 0, timerColor1);
  }

  // Colon dots (always visible)
  setPixel(0, 4, timerColorColon);
  setPixel(0, 6, timerColorColon);

  // Seconds in the lower portion (rows 3-7)
  drawChar3x5('0' + seconds / 10, 1, 3, timerColor2);
  drawChar3x5('0' + seconds % 10, 5, 3, timerColor2);
}

// ── Clock Mode (NTP) ─────────────────────────────────────────
// Displays the current time synced from pool.ntp.org.
// configTime() is called in handleAnimation() to start NTP sync.
// Until sync completes, the display pulses clockBgColor to show
// it's waiting for a time signal.
//
// The display only redraws when the hour or minute changes —
// this is an optimization since the 8×8 matrix only has minute
// resolution anyway. Skipping identical frames saves CPU and
// avoids flicker from constant redraws.
void stepClockFrame() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) {
    // NTP not synced yet — pulse the background color while waiting
    uint8_t pulse = (uint8_t)(128 + 60 * sinf(millis() / 800.0f));
    fill_solid(leds, NUM_LEDS, CRGB(
      (uint8_t)((uint32_t)clockBgColor.r * pulse / 255),
      (uint8_t)((uint32_t)clockBgColor.g * pulse / 255),
      (uint8_t)((uint32_t)clockBgColor.b * pulse / 255)
    ));
    return;
  }
  ntpSynced = true;

  // Convert 24h → 12h, treating 0 as 12
  int h = timeinfo.tm_hour % 12;
  if (h == 0) h = 12;
  int m = timeinfo.tm_min;

  // Skip redraw if nothing changed since last frame
  if (h == clockPrevHour && m == clockPrevMin) return;
  clockPrevHour = h;
  clockPrevMin  = m;

  fill_solid(leds, NUM_LEDS, clockBgColor);

  // Choose digit color: same hue as the background but desaturated and bright,
  // so the digits always contrast against any background color the user sets.
  CHSV bgHSV = rgb2hsv_approximate(clockBgColor);
  CRGB digitColor = CHSV(bgHSV.h, 50, 220);

  drawTimeDisplay(h, m, digitColor);
}
