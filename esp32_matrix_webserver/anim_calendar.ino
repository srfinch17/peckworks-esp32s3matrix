// ============================================================
// SECTION: CALENDAR ANIMATIONS
//
// Five ways to show today's date (synced from NTP, same as clock mode):
//   "scroll" — "TUE JUN 9" scrolls across (3×5 font)
//   "bignum" — the day-of-month as a big centered number
//   "grid"   — a mini month grid: 7 cols (Sun–Sat) × weeks, today highlighted
//   "clock"  — month over day, in the clock's tiny-top / big-bottom layout
//   "square" — desk-calendar square: 2-letter weekday on top, day number below
//
// Reuses font helpers (fonts.ino), drawTimeDisplay/blendColors (clock_timer.ino),
// and the same getLocalTime() NTP source as the clock. configTime() is started
// in handleAnimation() when calendar mode begins.
//
// Colors (all three distinct so the parts read apart):
//   calendarColor1 — primary   (day number / today / scroll text)
//   calendarColor2 — secondary (month number / weekday cells in the grid)
//   calendarColor3 — accent    (weekday letter in clock style / weekend cells in grid)
// ============================================================

static const char* const CAL_WDAYS[7]   = { "SUN","MON","TUE","WED","THU","FRI","SAT" };
static const char* const CAL_MONTHS[12]  = { "JAN","FEB","MAR","APR","MAY","JUN",
                                             "JUL","AUG","SEP","OCT","NOV","DEC" };

// Days in a given month (mon0 = 0-11), accounting for leap years.
static int calDaysInMonth(int mon0, int year) {
  static const int dim[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  if (mon0 == 1) {  // February
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return leap ? 29 : 28;
  }
  return dim[mon0];
}

void stepCalendarFrame() {
  struct tm t;
  if (!getLocalTime(&t, 100)) {
    drawNtpWaitFrame();   // animated hourglass until the first NTP sync (shared with clock)
    return;
  }
  ntpSynced = true;

  int mday = t.tm_mday;          // 1-31
  int mon0 = t.tm_mon;           // 0-11
  int wday = t.tm_wday;          // 0 = Sunday
  int year = t.tm_year + 1900;

  fill_solid(leds, NUM_LEDS, CRGB::Black);

  if (calendarStyle == "scroll") {
    // Draw the date as three tokens so weekday / month / day can each take their own
    // color (color1/2/3). Single-color mode (calendarScrollMono) paints all in color1.
    // Each glyph is a 4px stride (3px + 1px gap); a word gap is one extra stride.
    char wd[4], mo[4], dy[4];
    snprintf(wd, sizeof(wd), "%s", CAL_WDAYS[wday]);   // 3 chars
    snprintf(mo, sizeof(mo), "%s", CAL_MONTHS[mon0]);  // 3 chars
    snprintf(dy, sizeof(dy), "%d", mday);              // 1-2 chars
    CRGB cWd = calendarColor1;
    CRGB cMo = calendarScrollMono ? calendarColor1 : calendarColor2;
    CRGB cDy = calendarScrollMono ? calendarColor1 : calendarColor3;
    int x = calendarScrollX;
    drawStr3x5(wd, x, 2, cWd);  x += ((int)strlen(wd) + 1) * 4;   // +1 stride = word gap
    drawStr3x5(mo, x, 2, cMo);  x += ((int)strlen(mo) + 1) * 4;
    drawStr3x5(dy, x, 2, cDy);
    int totalChars = (int)strlen(wd) + 1 + (int)strlen(mo) + 1 + (int)strlen(dy);  // incl. both word gaps
    // Advance on an independent wall-clock timer (calendarScrollMs per pixel, set
    // from the page's speed slider; default 80ms ≈ 12.5 px/s) so the scroll rate
    // tracks the slider, not the animation frame tick.
    static uint32_t lastCalScrollMs = 0;
    uint32_t now = millis();
    if (now - lastCalScrollMs >= calendarScrollMs) {
      lastCalScrollMs = now;
      calendarScrollX--;
      if (calendarScrollX < -(totalChars * 4)) calendarScrollX = MATRIX_W;  // 4px stride per char
    }
  }
  else if (calendarStyle == "bignum") {
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", mday);
    drawStrCentered3x5(buf, 2, calendarColor1);            // centered, rows 2-6
  }
  else if (calendarStyle == "grid") {
    // Weekday of the 1st of this month (0 = Sun), derived from today's wday/mday.
    int firstW = (((wday - (mday - 1)) % 7) + 7) % 7;
    int days   = calDaysInMonth(mon0, year);
    for (int d = 1; d <= days; d++) {
      int cell = firstW + (d - 1);
      int col  = cell % 7;        // 7 columns: Sun..Sat
      int row  = cell / 7;        // up to 6 week-rows
      if (row > 5) break;         // safety (a month spans at most 6 rows)
      // today = primary, weekend (Sun/Sat columns) = accent, weekday = secondary.
      // All at FULL brightness — the old (color2/4) dimming dropped non-today
      // cells below the visibility threshold at low brightness, so only one dot lit.
      CRGB c = (d == mday)            ? calendarColor1
             : (col == 0 || col == 6) ? calendarColor3
                                      : calendarColor2;
      setPixel(col, row, c);
    }
  }
  else if (calendarStyle == "square") {
    // One square torn off a desk calendar: weekday on top, big day number below.
    // Two 3×3 letters (SU MO TU WE TH FR SA — unambiguous, watch-style), because
    // three 3×3 glyphs are 11px wide and the matrix is 8. Weekday in color2,
    // day in color1. Rows: 3×3 on 0-2, 3×5 on 3-7 (literals — fonts.ino's ROW_*
    // macros aren't visible here, see PITFALLS on .ino concatenation order).
    char wd[3] = { CAL_WDAYS[wday][0], CAL_WDAYS[wday][1], '\0' };
    drawStrCentered3x3(wd, 0, calendarColor2);
    char dbuf[4];
    snprintf(dbuf, sizeof(dbuf), "%d", mday);
    drawStrCentered3x5(dbuf, 3, calendarColor1);
  }
  else {  // "clock" — month (top-left) + weekday letter (top-right) + day (bottom), no colon
    int month = mon0 + 1;
    // Month, top-left, tiny 3×3, in calendarColor2. Months 10-12: a 1px "1" bar in
    // col 0 + the units digit at cols 1-3, leaving room for the weekday letter.
    if (month >= 10) {
      setPixel(0, 0, calendarColor2); setPixel(0, 1, calendarColor2); setPixel(0, 2, calendarColor2);
      drawChar3x3('0' + (month % 10), 1, 0, calendarColor2);
    } else {
      drawChar3x3('0' + month, 0, 0, calendarColor2);
    }
    // Weekday first letter, top-right (cols 5-7), tiny 3×3, in calendarColor3.
    drawChar3x3(CAL_WDAYS[wday][0], 5, 0, calendarColor3);
    // Day number, bottom (rows 3-7), 3×5, centered, in calendarColor1 — no colon.
    char dbuf[4];
    snprintf(dbuf, sizeof(dbuf), "%d", mday);
    drawStrCentered3x5(dbuf, 3, calendarColor1);
  }
}
