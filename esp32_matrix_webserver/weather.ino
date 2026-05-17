// ============================================================
// SECTION 6.7: CHIP TEMP + WEATHER DISPLAY
// ============================================================

// ── Temperature pixel layout (8×8 matrix) ─────────────────────────────────────
// A=hundreds("1"), B=tens, C=units (3×5 MINI_FONT), D=degree dot, N=minus.
//
// Two digits XX (1-pixel gap between digits at col 4):
//   row 2: . . . . . . . D
//   row 3: . B B B . C C C    B = cols 1-3, C = cols 5-7
//   row 4: . B . B . C . C
//   row 5: . B B B . C C C
//   row 6: . B . B . C . C
//   row 7: . B B B . C C C
// ─────────────────────────────────────────────────────────────────────────────

// Warm palette (≥72°F): orange-red tens / amber units.
// Cool palette (<72°F): blue tens / teal units.
// Brightness ~half max; hues ~17° apart per digit for readability.
void drawTempOverlay(float tempF) {
  bool warm = (tempF >= 72.0f);

  CRGB colA, colB, colC, colD;
  if (warm) {
    colA = CHSV(5,  90, 100);
    colB = CHSV(5,  85, 120);
    colC = CHSV(22, 75, 120);
    colD = CHSV(12, 110, 75);
  } else {
    colA = CHSV(165, 90, 100);
    colB = CHSV(165, 85, 120);
    colC = CHSV(148, 75, 120);
    colD = CHSV(157, 110, 75);
  }

  int tempInt   = (int)roundf(tempF);
  bool negative = (tempInt < 0);
  int  absTemp  = abs(tempInt);

  setPixel(7, 2, colD);

  if (absTemp >= 100) {
    for (int r = 3; r <= 7; r++) setPixel(0, r, colA);
    int tens  = (absTemp / 10) % 10;
    int units = absTemp % 10;
    for (int col = 0; col < 3; col++) {
      uint8_t bT = pgm_read_byte(&MINI_FONT[tens][col]);
      uint8_t bU = pgm_read_byte(&MINI_FONT[units][col]);
      for (int row = 0; row < 5; row++) {
        if ((bT >> row) & 1) setPixel(col + 1, row + 3, colB);
        if ((bU >> row) & 1) setPixel(col + 4, row + 3, colC);
      }
    }
  } else if (absTemp >= 10) {
    if (negative) { setPixel(0, 1, colA); setPixel(1, 1, colA); }
    int tens  = absTemp / 10;
    int units = absTemp % 10;
    for (int col = 0; col < 3; col++) {
      uint8_t bT = pgm_read_byte(&MINI_FONT[tens][col]);
      uint8_t bU = pgm_read_byte(&MINI_FONT[units][col]);
      for (int row = 0; row < 5; row++) {
        if ((bT >> row) & 1) setPixel(col + 0, row + 3, colB);
        if ((bU >> row) & 1) setPixel(col + 4, row + 3, colC);
      }
    }
  } else {
    if (negative) { setPixel(1, 5, colA); setPixel(2, 5, colA); }
    for (int col = 0; col < 3; col++) {
      uint8_t bits = pgm_read_byte(&MINI_FONT[absTemp][col]);
      for (int row = 0; row < 5; row++)
        if ((bits >> row) & 1) setPixel(col + 3, row + 3, colC);
    }
  }
}

void stepChipTempFrame() {
  static uint8_t  phase    = 0;
  static uint32_t lastRead = 0;
  static float    cachedC  = 0.0f;
  static float    cachedF  = 0.0f;

  phase += 3;

  uint32_t now = millis();
  if (now - lastRead >= 5000 || lastRead == 0) {
    cachedC = temperatureRead();
    cachedF = cachedC * 9.0f / 5.0f + 32.0f;
    lastRead = now;
  }

  float displayVal = (chipTempUnit == "F") ? cachedF : cachedC;

  float t   = constrain((cachedC - 30.0f) / 35.0f, 0.0f, 1.0f);
  uint8_t h = (uint8_t)((1.0f - t) * 160.0f);

  for (int y = 0; y < MATRIX_H; y++) {
    uint8_t rowBase = (uint8_t)map(y, 0, 7, 220, 70);
    uint8_t sinVal  = sin8(phase + (uint8_t)(y * 28));
    uint8_t bright  = scale8(rowBase, 130 + sinVal / 4);
    CRGB col = CHSV(h, 230, bright);
    for (int x = 0; x < MATRIX_W; x++) setPixel(x, y, col);
  }

  drawTempOverlay(displayVal);
}

// ============================================================
// SECTION 6.8: WEATHER DISPLAY
// Fetches from wttr.in (no API key). Animated icon matched to
// current conditions. 2s numbers / 3s icon alternating display.
// ============================================================

static PNG    s_png;
static float  s_iR[64], s_iG[64], s_iB[64];
static float  s_iN[64];
static int    s_iW, s_iH;

int weatherIconPNGRow(PNGDRAW *pDraw) {
  uint16_t rowBuf[128] = {};
  s_png.getLineAsRGB565(pDraw, rowBuf, PNG_RGB565_LITTLE_ENDIAN, 0x000000);

  int dstRow = (pDraw->y * 8) / s_iH;
  for (int x = 0; x < s_iW && x < 128; x++) {
    uint16_t px = rowBuf[x];
    if (px == 0) continue;
    uint8_t r = (uint8_t)(((px >> 11) & 0x1F) << 3);
    uint8_t g = (uint8_t)(((px >>  5) & 0x3F) << 2);
    uint8_t b = (uint8_t)( (px        & 0x1F) << 3);
    int dstCol = (x * 8) / s_iW;
    int idx    = dstRow * 8 + dstCol;
    s_iR[idx] += r;  s_iG[idx] += g;  s_iB[idx] += b;  s_iN[idx] += 1.0f;
  }
  return 1;
}

void fetchWeatherIcon(const String& url) {
  WiFiClientSecure sc;
  sc.setInsecure();
  HTTPClient http;
  http.begin(sc, url);
  http.setTimeout(8000);
  if (http.GET() != 200) { http.end(); return; }

  const int MAX_ICON = 16384;
  uint8_t* buf = (uint8_t*)malloc(MAX_ICON);
  if (!buf) { http.end(); return; }

  WiFiClient* stream = http.getStreamPtr();
  int got = stream->readBytes(buf, MAX_ICON);
  http.end();

  memset(s_iR, 0, sizeof(s_iR));
  memset(s_iG, 0, sizeof(s_iG));
  memset(s_iB, 0, sizeof(s_iB));
  memset(s_iN, 0, sizeof(s_iN));

  if (s_png.openRAM(buf, got, weatherIconPNGRow) == PNG_SUCCESS) {
    s_iW = s_png.getWidth();
    s_iH = s_png.getHeight();
    s_png.decode(NULL, 0);
    s_png.close();

    for (int i = 0; i < 64; i++) {
      if (s_iN[i] > 0.0f) {
        uint8_t r = (uint8_t)(s_iR[i] / s_iN[i]);
        uint8_t g = (uint8_t)(s_iG[i] / s_iN[i]);
        uint8_t b = (uint8_t)(s_iB[i] / s_iN[i]);
        CHSV hsv = rgb2hsv_approximate(CRGB(r, g, b));
        hsv.s = qadd8(hsv.s, 80);
        hsv.v = max(hsv.v, (uint8_t)160);
        weatherIconBuf[i] = CHSV(hsv.h, hsv.s, hsv.v);
      } else {
        weatherIconBuf[i] = CRGB::Black;
      }
    }
    weatherHasIcon = true;
    Serial.println("Weather icon loaded OK");
  } else {
    Serial.println("Weather icon PNG decode failed");
  }
  free(buf);
}

void fetchWeather() {
  HTTPClient http;
  String url = "http://wttr.in/" + weatherZip + "?format=j1";
  http.begin(url);
  http.setTimeout(10000);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("Weather HTTP err: %d\n", code);
    http.end();
    lastWeatherFetch = millis();
    return;
  }

  // getString() downloads the full 40-50KB body before parsing —
  // streaming into deserializeJson drops mid-parse on large responses.
  String payload = http.getString();
  http.end();
  lastWeatherFetch = millis();
  Serial.printf("Weather payload: %d bytes\n", payload.length());

  JsonDocument filter;
  filter["current_condition"][0]["temp_F"]                     = true;
  filter["current_condition"][0]["temp_C"]                     = true;
  filter["current_condition"][0]["weatherCode"]                = true;
  filter["current_condition"][0]["humidity"]                   = true;
  filter["current_condition"][0]["uvIndex"]                    = true;
  filter["current_condition"][0]["pressure"]                   = true;
  filter["current_condition"][0]["weatherIconUrl"][0]["value"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("Weather JSON err: %s\n", err.c_str());
    return;
  }

  // All wttr.in numeric values arrive as JSON strings — use const char* then atoi().
  JsonObject cond = doc["current_condition"][0];
  const char* tf  = cond["temp_F"];
  const char* tc  = cond["temp_C"];
  const char* wc  = cond["weatherCode"];
  const char* hu  = cond["humidity"];
  const char* uv  = cond["uvIndex"];
  const char* pr  = cond["pressure"];
  const char* ico = doc["current_condition"][0]["weatherIconUrl"][0]["value"];

  int tF = tf ? atoi(tf) : 0;
  int tC = tc ? atoi(tc) : 0;
  weatherTempVal  = (weatherUnit == "F") ? tF : tC;
  weatherCode     = wc ? atoi(wc) : 113;
  weatherHumidity = hu ? atoi(hu) : 0;
  weatherUvIndex  = uv ? atoi(uv) : 0;
  weatherPressure = pr ? atoi(pr) : 0;

  Serial.printf("Weather OK: %d°%s code=%d hum=%d%% uv=%d pres=%dhPa\n",
                weatherTempVal, weatherUnit.c_str(), weatherCode,
                weatherHumidity, weatherUvIndex, weatherPressure);

  String iconUrl = ico ? String(ico) : "";
  if (weatherIconSource == "remote" && iconUrl.length() > 0)
    fetchWeatherIcon(iconUrl);
}

// 0=sunny 1=partly cloudy 2=cloudy 3=fog 4=rain 5=snow 6=thunder
int weatherCategory(int code) {
  if (code == 113)                              return 0;
  if (code == 116)                              return 1;
  if (code == 119 || code == 122)               return 2;
  if (code == 143 || code == 248 || code == 260) return 3;
  if (code == 200 || code >= 386)               return 6;
  if (code == 179 || code == 227 || code == 230 ||
      (code >= 323 && code <= 350) ||
      code == 362 || code == 365 || code == 368 || code == 371 ||
      code == 374 || code == 377)               return 5;
  if (code >= 176)                              return 4;
  return 0;
}

void drawSunIcon(uint8_t f) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  CRGB core = CRGB(255, 140, 0);
  for (int y = 2; y <= 5; y++)
    for (int x = 2; x <= 5; x++)
      setPixel(x, y, core);
  // Round the corners to make the sun disc circular
  setPixel(2, 2, CRGB::Black);
  setPixel(5, 2, CRGB::Black);
  setPixel(2, 5, CRGB::Black);
  setPixel(5, 5, CRGB::Black);
  static const int8_t bx[8] = {3, 6, 7, 6, 4, 1, 0, 1};
  static const int8_t by[8] = {0, 1, 3, 6, 7, 6, 4, 1};
  uint8_t slot = (f / 3) % 8;
  for (int i = 0; i < 8; i++) {
    int d = (i - slot + 8) % 8;
    if      (d == 0)           setPixel(bx[i], by[i], CRGB(255, 220, 0));
    else if (d == 1 || d == 7) setPixel(bx[i], by[i], CRGB(120, 80,  0));
    else if (d == 2 || d == 6) setPixel(bx[i], by[i], CRGB(40,  20,  0));
  }
}

void drawPartlyCloudyIcon(uint8_t f) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  CRGB sunC = CRGB(255, 180, 0);
  for (int y = 5; y <= 7; y++)
    for (int x = 0; x <= 2; x++)
      setPixel(x, y, sunC);
  uint8_t p = sin8(f * 6);
  setPixel(3, 5, CRGB(p, (uint8_t)(p / 2), 0));
  setPixel(0, 4, CRGB(p, (uint8_t)(p / 2), 0));
  int cx = (int)((f / 4) % 14) - 3;
  CRGB cloudC = CRGB(160, 160, 185);
  for (int dx = 0; dx < 6; dx++) {
    int sx = cx + dx;
    if (sx >= 0 && sx < 8) { setPixel(sx, 1, cloudC); setPixel(sx, 2, cloudC); setPixel(sx, 3, cloudC); }
    int top = cx + dx - 1;
    if (dx > 0 && dx < 5 && top >= 0 && top < 8) setPixel(top, 0, cloudC);
  }
}

void drawCloudyIcon(uint8_t f) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  uint8_t br = 90 + sin8(f * 3) / 4;
  CRGB cloudC = CRGB(br, br, (uint8_t)(br + 20));
  for (int x = 1; x <= 6; x++) { setPixel(x, 3, cloudC); setPixel(x, 4, cloudC); }
  for (int x = 2; x <= 5; x++) { setPixel(x, 2, cloudC); setPixel(x, 5, cloudC); }
  setPixel(3, 1, cloudC); setPixel(4, 1, cloudC);
  setPixel(2, 5, cloudC); setPixel(5, 5, cloudC);
}

void drawFogIcon(uint8_t f) {
  for (int y = 0; y < MATRIX_H; y++) {
    uint8_t br = 40 + sin8(f * 2 + (uint8_t)(y * 35)) / 5;
    CRGB fogC  = CRGB(br, br, (uint8_t)(br + 10));
    for (int x = 0; x < MATRIX_W; x++) setPixel(x, y, fogC);
  }
}

void drawRainIcon(uint8_t f) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  CRGB cloudC = CRGB(120, 120, 150);
  for (int x = 0; x < 8; x++) { setPixel(x, 2, cloudC); setPixel(x, 3, cloudC); }
  for (int x = 2; x < 6; x++)   setPixel(x, 1, cloudC);
  setPixel(3, 0, cloudC); setPixel(4, 0, cloudC);
  static const int8_t  rcol[5] = {0, 2, 4, 5, 7};
  static const uint8_t roff[5] = {0, 2, 1, 3, 0};
  for (int i = 0; i < 5; i++) {
    int row = 4 + (int)((f + roff[i]) % 4);
    if (row < 8)     setPixel(rcol[i], row,     CRGB(0, 80,  255));
    if (row - 1 >= 4) setPixel(rcol[i], row - 1, CRGB(0, 25,  80));
  }
}

void drawSnowIcon(uint8_t f) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  CRGB cloudC = CRGB(160, 160, 185);
  for (int x = 0; x < 8; x++) { setPixel(x, 2, cloudC); setPixel(x, 3, cloudC); }
  for (int x = 2; x < 6; x++)   setPixel(x, 1, cloudC);
  setPixel(3, 0, cloudC); setPixel(4, 0, cloudC);
  static const int8_t  scol[4] = {1, 3, 5, 7};
  static const uint8_t soff[4] = {0, 2, 1, 3};
  for (int i = 0; i < 4; i++) {
    int row = 4 + (int)((f / 2 + soff[i]) % 4);
    if (row < 8) setPixel(scol[i], row, CRGB(200, 220, 255));
  }
}

void drawThunderIcon(uint8_t f) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  CRGB cloudC = CRGB(60, 60, 80);
  for (int x = 0; x < 8; x++) { setPixel(x, 2, cloudC); setPixel(x, 3, cloudC); }
  for (int x = 1; x < 7; x++)   setPixel(x, 1, cloudC);
  for (int x = 2; x < 6; x++)   setPixel(x, 0, cloudC);
  if ((f % 20) < 2) {
    CRGB bolt = CRGB(255, 255, 180);
    setPixel(4, 4, bolt); setPixel(3, 5, bolt); setPixel(4, 5, bolt);
    setPixel(3, 6, bolt); setPixel(2, 7, bolt);
  }
}

// colT = tens digit colour, colU = units digit colour.
// Pass weatherPressure/10 for pressure to fit 2-3 digits.
void drawValueOverlay(int val, CRGB colT, CRGB colU) {
  int v = constrain(abs(val), 0, 999);
  if (v >= 100) {
    for (int r = 3; r <= 7; r++) setPixel(0, r, colT);
    int t = (v / 10) % 10, u = v % 10;
    for (int c = 0; c < 3; c++)
      for (int row = 0; row < 5; row++) {
        if ((pgm_read_byte(&MINI_FONT[t][c]) >> row) & 1) setPixel(c + 1, row + 3, colT);
        if ((pgm_read_byte(&MINI_FONT[u][c]) >> row) & 1) setPixel(c + 4, row + 3, colU);
      }
  } else if (v >= 10) {
    int t = v / 10, u = v % 10;
    for (int c = 0; c < 3; c++)
      for (int row = 0; row < 5; row++) {
        if ((pgm_read_byte(&MINI_FONT[t][c]) >> row) & 1) setPixel(c + 1, row + 3, colT);
        if ((pgm_read_byte(&MINI_FONT[u][c]) >> row) & 1) setPixel(c + 5, row + 3, colU);
      }
  } else {
    for (int c = 0; c < 3; c++)
      for (int row = 0; row < 5; row++)
        if ((pgm_read_byte(&MINI_FONT[v][c]) >> row) & 1) setPixel(c + 4, row + 3, colU);
  }
}

// Draws a compact label above the number area using tiny inline bitmaps.
// bits[x]: bit0=row0, bit1=row1, bit2=row2. rows=2 for 2-char, rows=3 for 3-char.
//
// "UV"  (3×2 font, rows 0-1): {3,2,3,0,1,2,1,0}
//   U = X.X/XXX (cols 0-2), gap, V = X.X/.X. (cols 4-6)
//
// "HUM" (2×3 font, rows 0-2): {7,2,0,7,4,0,7,5}
//   H = X./XX/X. | gap | U = X./X./XX | gap | M = XX/X./XX
void drawMetricLabel(const uint8_t bits[8], int rows, CRGB color) {
  for (int x = 0; x < 8; x++)
    for (int r = 0; r < rows; r++)
      if ((bits[x] >> r) & 1) setPixel(x, r, color);
}

void stepWeatherFrame() {
  if ((millis() - lastWeatherFetch) >= 600000UL) fetchWeather();

  // Phase timer: 2s numbers on black → 3s icon → repeat
  uint32_t elapsed = millis() - weatherPhaseStart;
  if (!weatherShowIcon && elapsed >= 2000UL) {
    weatherShowIcon   = true;
    weatherPhaseStart = millis();
  } else if (weatherShowIcon && elapsed >= 3000UL) {
    weatherShowIcon   = false;
    weatherPhaseStart = millis();
  }

  weatherFrame++;

  if (weatherShowIcon) {
    if (weatherIconSource == "remote" && weatherHasIcon) {
      for (int i = 0; i < NUM_LEDS; i++) leds[i] = weatherIconBuf[i];
    } else {
      int cat = weatherCategory(weatherCode);
      switch (cat) {
        case 0:  drawSunIcon(weatherFrame);          break;
        case 1:  drawPartlyCloudyIcon(weatherFrame); break;
        case 2:  drawCloudyIcon(weatherFrame);       break;
        case 3:  drawFogIcon(weatherFrame);          break;
        case 4:  drawRainIcon(weatherFrame);         break;
        case 5:  drawSnowIcon(weatherFrame);         break;
        case 6:  drawThunderIcon(weatherFrame);      break;
        default: drawSunIcon(weatherFrame);          break;
      }
    }
  } else {
    fill_solid(leds, NUM_LEDS, CRGB::Black);

    static uint32_t cycleStart = 0;
    static int      cyclePhase = 0;
    String mode = weatherDataMode;
    if (mode == "cycle") {
      if (millis() - cycleStart >= 6000UL) {
        cyclePhase = (cyclePhase + 1) % 4;
        cycleStart = millis();
      }
      switch (cyclePhase) {
        case 0: mode = "temp";     break;
        case 1: mode = "humidity"; break;
        case 2: mode = "uv";       break;
        case 3: mode = "pressure"; break;
      }
    }

    if (mode == "temp" && weatherTempVal != 0) {
      drawTempOverlay((float)weatherTempVal);
    } else if (mode == "humidity") {
      static const uint8_t humBits[8] = {7, 2, 0, 7, 4, 0, 7, 5};
      drawMetricLabel(humBits, 3, CHSV(130, 200, 130));
      drawValueOverlay(weatherHumidity,     CHSV(130, 200, 110), CHSV(147, 180, 110));
    } else if (mode == "uv") {
      static const uint8_t uvBits[8]  = {3, 2, 3, 0, 1, 2, 1, 0};
      drawMetricLabel(uvBits,  2, CHSV(25,  230, 130));
      drawValueOverlay(weatherUvIndex,       CHSV(25,  230, 110), CHSV(42,  210, 110));
    } else if (mode == "pressure" && weatherPressure != 0) {
      drawValueOverlay(weatherPressure / 10, CHSV(192, 200, 110), CHSV(212, 185, 110));
    }
  }
}
