// ============================================================
// SECTION: CALENDAR ANIMATIONS
//
// Four ways to show today's date (synced from NTP, same as clock mode):
//   "scroll" — "TUE JUN 9" scrolls across (3×5 font)
//   "bignum" — the day-of-month as a big centered number
//   "grid"   — a mini month grid: 7 cols (Sun–Sat) × weeks, today highlighted
//   "clock"  — month over day, in the clock's tiny-top / big-bottom layout
//
// Reuses font helpers (fonts.ino), drawTimeDisplay/blendColors (clock_timer.ino),
// and the same getLocalTime() NTP source as the clock. configTime() is started
// in handleAnimation() when calendar mode begins.
//
// Colors:
//   calendarColor1 — primary  (the day / today / scroll text)
//   calendarColor2 — secondary(the month / other days in the grid)
//   calendarColor3 — accent   (separator dots in clock style)
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
    // NTP not synced yet — pulse dim white while waiting (same as clock).
    uint8_t pulse = (uint8_t)(128 + 60 * sinf(millis() / 800.0f));
    fill_solid(leds, NUM_LEDS, CRGB(pulse, pulse, pulse));
    return;
  }
  ntpSynced = true;

  int mday = t.tm_mday;          // 1-31
  int mon0 = t.tm_mon;           // 0-11
  int wday = t.tm_wday;          // 0 = Sunday
  int year = t.tm_year + 1900;

  fill_solid(leds, NUM_LEDS, CRGB::Black);

  if (calendarStyle == "scroll") {
    char buf[20];
    snprintf(buf, sizeof(buf), "%s %s %d", CAL_WDAYS[wday], CAL_MONTHS[mon0], mday);
    drawStr3x5(buf, calendarScrollX, 2, calendarColor1);   // rows 2-6
    // Advance on an independent ~80ms timer (~12.5 px/s, readable) so the scroll
    // speed doesn't change with animationSpeed / the animation tick rate.
    static uint32_t lastCalScrollMs = 0;
    uint32_t now = millis();
    if (now - lastCalScrollMs >= 80) {
      lastCalScrollMs = now;
      calendarScrollX--;
      if (calendarScrollX < -((int)strlen(buf) * 4)) calendarScrollX = MATRIX_W;  // 4px stride per char
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
    CRGB other = CRGB(calendarColor2.r / 4, calendarColor2.g / 4, calendarColor2.b / 4);
    for (int d = 1; d <= days; d++) {
      int cell = firstW + (d - 1);
      int col  = cell % 7;        // 7 columns: Sun..Sat
      int row  = cell / 7;        // up to 6 week-rows
      if (row > 5) break;         // safety (a month spans at most 6 rows)
      setPixel(col, row, (d == mday) ? calendarColor1 : other);
    }
  }
  else {  // "clock" — month (tiny top) over day (big bottom), like the clock
    drawTimeDisplay(mon0 + 1, mday, calendarColor2, calendarColor3, calendarColor1);
  }
}
