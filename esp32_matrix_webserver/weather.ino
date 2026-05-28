// ============================================================
// SECTION 6.7: CHIP TEMP + WEATHER DISPLAY
//
// Two separate things live here:
//
// 1. CHIP TEMP — reads the ESP32's internal temperature sensor
//    (which runs 10-15°C above ambient — it's a chip temp, not
//    a room temp) and overlays the number on a color gradient.
//
// 2. WEATHER — fetches current conditions from wttr.in (no API
//    key required), displays an animated weather icon, and
//    alternates between the icon and a live data overlay showing
//    temperature, humidity, UV index, or pressure.
// ============================================================

// ── Temperature number overlay ────────────────────────────────
//
// Pixel layout on the 8×8 matrix for a 2-digit temperature:
//
//   col:  0  1  2  3  4  5  6  7
//   row 2:                        D  ← degree symbol dot
//   row 3:    B  B  B     C  C  C
//   row 4:    B     B     C     C
//   row 5:    B  B  B     C  C  C
//   row 6:    B     B     C     C
//   row 7:    B  B  B     C  C  C
//
//   B = tens digit (cols 1-3), C = units digit (cols 5-7)
//   For 3-digit temps (100+): A = "1" at col 0, tens/units shift right.
//   For negative temps: minus sign drawn at row 1.
//   For 1-digit temps (<10): number centered in cols 3-5.
//
// Color scheme:
//   Warm (≥72°F): orange-red tens / amber units
//   Cool (<72°F): blue tens / teal units

void drawTempOverlay(float tempF) {
  bool warm = (tempF >= 72.0f);

  // CHSV(hue, saturation, value) — FastLED's HSV color type.
  // Using HSV makes it easy to pick nearby hues by tweaking the hue number.
  CRGB colA, colB, colC, colD;
  if (warm) {
    colA = CHSV(5,  90, 100);    // hundreds "1" — dim orange-red
    colB = CHSV(5,  85, 120);    // tens digit  — orange-red
    colC = CHSV(22, 75, 120);    // units digit — amber (slightly yellower)
    colD = CHSV(12, 110, 75);    // degree dot  — small, muted
  } else {
    colA = CHSV(165, 90, 100);   // dim blue
    colB = CHSV(165, 85, 120);   // blue
    colC = CHSV(148, 75, 120);   // teal (cooler hue)
    colD = CHSV(157, 110, 75);   // degree dot
  }

  int tempInt   = (int)roundf(tempF);
  bool negative = (tempInt < 0);
  int  absTemp  = abs(tempInt);

  setPixel(7, 2, colD);   // degree symbol dot — always top-right

  if (absTemp >= 100) {
    // 3-digit: draw a vertical "1" bar at col 0, tens/units in cols 1-6
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
    // 2-digit: optional minus sign at row 1, digits fill cols 0-6
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
    // 1-digit: optional minus sign, digit centered
    if (negative) { setPixel(1, 5, colA); setPixel(2, 5, colA); }
    for (int col = 0; col < 3; col++) {
      uint8_t bits = pgm_read_byte(&MINI_FONT[absTemp][col]);
      for (int row = 0; row < 5; row++)
        if ((bits >> row) & 1) setPixel(col + 3, row + 3, colC);
    }
  }
}

// ── stepChipTempFrame ─────────────────────────────────────────
// Draws the ESP32's die temperature as a color-shifting gradient
// background with the number overlaid on top.
//
// The background hue shifts from blue (cool ~30°C) to red (hot ~65°C)
// based on the actual chip temperature. A sine wave per row adds a
// subtle shimmer so it doesn't look like a static gradient.
// Temperature is re-read every 5 seconds (thermometer resolution is low,
// no point reading every 66ms frame).
void stepChipTempFrame() {
  static uint8_t  phase    = 0;         // drives the shimmer sine wave
  static uint32_t lastRead = 0;
  static float    cachedC  = 0.0f;
  static float    cachedF  = 0.0f;

  phase += 3;   // advance shimmer phase each frame

  uint32_t now = millis();
  if (now - lastRead >= 5000 || lastRead == 0) {
    cachedC  = temperatureRead();   // built-in ESP32 function, returns Celsius
    cachedF  = cachedC * 9.0f / 5.0f + 32.0f;
    lastRead = now;
  }

  float displayVal = (chipTempUnit == "F") ? cachedF : cachedC;

  // t: 0.0 at 30°C (baseline), 1.0 at 65°C (very hot)
  // h: hue from 160 (blue) down to 0 (red) as chip heats up
  float t   = constrain((cachedC - 30.0f) / 35.0f, 0.0f, 1.0f);
  uint8_t h = (uint8_t)((1.0f - t) * 160.0f);

  for (int y = 0; y < MATRIX_H; y++) {
    // rowBase: each row has a slightly different brightness base (top row brighter)
    uint8_t rowBase = (uint8_t)map(y, 0, 7, 220, 70);
    // sin8(): FastLED's fast 8-bit sine wave, returns 0-255.
    // The y*28 term staggers the phase per row for a wave-like shimmer.
    uint8_t sinVal  = sin8(phase + (uint8_t)(y * 28));
    uint8_t bright  = scale8(rowBase, 130 + sinVal / 4);
    CRGB col = CHSV(h, 230, bright);
    for (int x = 0; x < MATRIX_W; x++) setPixel(x, y, col);
  }

  drawTempOverlay(displayVal);   // number drawn on top of the gradient
}

// ============================================================
// SECTION 6.8: WEATHER DISPLAY
//
// DATA SOURCE: wttr.in — a free weather service that returns
// JSON with no API key. We request format=j1 (full JSON).
// The response is ~40-50KB, which is large for an embedded
// device — we use ArduinoJson's filter feature to only parse
// the fields we care about.
//
// DISPLAY: alternates every few seconds between a data overlay
// (temp/humidity/UV/pressure) and an animated weather icon.
//
// ICON SOURCE (selectable):
//   animated  — local pixel art animations drawn each frame
//   remote    — PNG image fetched from wttr.in's icon URL,
//               decoded and stored in weatherIconBuf[64]
// ============================================================

// PNG decode state — used during fetchWeatherIcon()
static PNG    s_png;
static float  s_iR[64], s_iG[64], s_iB[64];   // accumulator for averaging
static float  s_iN[64];                         // pixel count per output cell
static int    s_iW, s_iH;                       // source image dimensions

// ── weatherIconPNGRow ─────────────────────────────────────────
// PNGdec decode callback — called once per source image row.
// We're scaling the source image (typically 128×128) down to 8×8
// by averaging all source pixels that map to each output cell.
// s_iR/G/B accumulate color sums; s_iN counts how many pixels
// contributed. The final average is computed in fetchWeatherIcon().
int weatherIconPNGRow(PNGDRAW *pDraw) {
  uint16_t rowBuf[128] = {};
  // Decode the row into RGB565 format (the only output format PNGdec supports)
  s_png.getLineAsRGB565(pDraw, rowBuf, PNG_RGB565_LITTLE_ENDIAN, 0x000000);

  // Map this source row to its output row in the 8×8 grid
  int dstRow = (pDraw->y * 8) / s_iH;
  for (int x = 0; x < s_iW && x < 128; x++) {
    uint16_t px = rowBuf[x];
    if (px == 0) continue;   // skip pure black pixels (usually transparency mapped to black)

    // Unpack RGB565: 5 bits red, 6 bits green, 5 bits blue → expand to 8 bits each
    uint8_t r = (uint8_t)(((px >> 11) & 0x1F) << 3);
    uint8_t g = (uint8_t)(((px >>  5) & 0x3F) << 2);
    uint8_t b = (uint8_t)( (px        & 0x1F) << 3);
    int dstCol = (x * 8) / s_iW;
    int idx    = dstRow * 8 + dstCol;
    s_iR[idx] += r;  s_iG[idx] += g;  s_iB[idx] += b;  s_iN[idx] += 1.0f;
  }
  return 1;
}

// ── fetchWeatherIcon ──────────────────────────────────────────
// Downloads a PNG from wttr.in's icon URL, decodes it via PNGdec,
// scales it down to 8×8, and stores the result in weatherIconBuf[].
// After this call, weatherHasIcon = true and the remote icon will
// be used instead of the built-in pixel art animations.
//
// setInsecure() skips SSL certificate validation — fine for a local
// hobby project, not appropriate for anything security-sensitive.
void fetchWeatherIcon(const String& url) {
  WiFiClientSecure sc;
  sc.setInsecure();   // skip cert validation (wttr.in uses HTTPS)
  HTTPClient http;
  http.begin(sc, url);
  http.setTimeout(8000);
  if (http.GET() != 200) { http.end(); return; }

  const int MAX_ICON = 16384;   // 16KB ceiling for the PNG
  uint8_t* buf = (uint8_t*)malloc(MAX_ICON);
  if (!buf) { http.end(); return; }

  WiFiClient* stream = http.getStreamPtr();
  int got = stream->readBytes(buf, MAX_ICON);
  http.end();

  // Reset accumulators before decode
  memset(s_iR, 0, sizeof(s_iR));
  memset(s_iG, 0, sizeof(s_iG));
  memset(s_iB, 0, sizeof(s_iB));
  memset(s_iN, 0, sizeof(s_iN));

  if (s_png.openRAM(buf, got, weatherIconPNGRow) == PNG_SUCCESS) {
    s_iW = s_png.getWidth();
    s_iH = s_png.getHeight();
    s_png.decode(NULL, 0);   // triggers weatherIconPNGRow for every source row
    s_png.close();

    // Finalize: average accumulated colors, boost saturation, ensure minimum brightness
    for (int i = 0; i < 64; i++) {
      if (s_iN[i] > 0.0f) {
        uint8_t r = (uint8_t)(s_iR[i] / s_iN[i]);
        uint8_t g = (uint8_t)(s_iG[i] / s_iN[i]);
        uint8_t b = (uint8_t)(s_iB[i] / s_iN[i]);
        // Boost saturation so the icon looks vibrant on the LEDs,
        // and ensure minimum brightness so dark areas are still visible.
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

// ── fetchWeather ──────────────────────────────────────────────
// Fetches current weather from wttr.in as JSON, then parses the
// fields we need with ArduinoJson.
//
// WHY getString() INSTEAD OF STREAMING:
//   The wttr.in response is 40-50KB of JSON. ArduinoJson's
//   streaming deserializer sometimes fails mid-parse on large
//   payloads from slow WiFi connections (the stream stalls and
//   the parser times out). getString() buffers the whole response
//   in heap RAM first, then parses it — slower but reliable.
//
// WHY atoi() ON STRINGS:
//   wttr.in sends all numeric values as JSON strings, not numbers.
//   e.g. "temp_F":"72" not "temp_F":72. So we cast to const char*
//   then use atoi() to convert to int.
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

  String payload = http.getString();   // buffer full response before parsing
  http.end();
  lastWeatherFetch = millis();
  Serial.printf("Weather payload: %d bytes\n", payload.length());

  // ArduinoJson filter: only keep the fields we actually use.
  // This dramatically reduces RAM usage — the full doc would overflow the heap.
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

  JsonObject cond = doc["current_condition"][0];
  const char* tf  = cond["temp_F"];
  const char* tc  = cond["temp_C"];
  const char* wc  = cond["weatherCode"];
  const char* hu  = cond["humidity"];
  const char* uv  = cond["uvIndex"];
  const char* pr  = cond["pressure"];
  const char* ico = doc["current_condition"][0]["weatherIconUrl"][0]["value"];

  // All values arrive as strings — use atoi() to convert. Null-check first.
  int tF = tf ? atoi(tf) : 0;
  int tC = tc ? atoi(tc) : 0;
  weatherTempF    = tF;
  weatherTempC    = tC;
  weatherTempVal  = (weatherUnit == "F") ? tF : tC;
  weatherCode     = wc ? atoi(wc) : 113;
  weatherHumidity = hu ? atoi(hu) : 0;
  weatherUvIndex  = uv ? atoi(uv) : 0;
  weatherPressure = pr ? atoi(pr) : 0;

  Serial.printf("Weather OK: %d°%s code=%d hum=%d%% uv=%d pres=%dhPa\n",
                weatherTempVal, weatherUnit.c_str(), weatherCode,
                weatherHumidity, weatherUvIndex, weatherPressure);

  // Optionally fetch the wttr.in PNG icon for remote icon mode
  String iconUrl = ico ? String(ico) : "";
  if (weatherIconSource == "remote" && iconUrl.length() > 0)
    fetchWeatherIcon(iconUrl);
}

// ── weatherCategory ───────────────────────────────────────────
// Maps a wttr.in weather code to one of 7 icon categories.
// wttr.in uses the same code table as the old Yahoo Weather API.
// Returns: 0=sunny, 1=partly cloudy, 2=cloudy, 3=fog,
//          4=rain, 5=snow, 6=thunder
int weatherCategory(int code) {
  if (code == 113)                              return 0;   // clear/sunny
  if (code == 116)                              return 1;   // partly cloudy
  if (code == 119 || code == 122)               return 2;   // overcast/cloudy
  if (code == 143 || code == 248 || code == 260) return 3;  // mist/fog
  if (code == 200 || code >= 386)               return 6;   // thunderstorm
  if (code == 179 || code == 227 || code == 230 ||
      (code >= 323 && code <= 350) ||
      code == 362 || code == 365 || code == 368 || code == 371 ||
      code == 374 || code == 377)               return 5;   // snow/sleet
  if (code >= 176)                              return 4;   // rain
  return 0;   // default to sunny
}

// ── Weather Icon Draw Functions ────────────────────────────────
// Each function draws one frame of a looping pixel art animation.
// The `f` parameter is weatherFrame (increments each frame) and
// drives any motion within the icon (spinning rays, scrolling clouds, etc.).

// Sun: rotating outer rays around a 4×4 core disc.
// The 8 ray positions cycle through brightnesses so the light
// appears to sweep around the sun clockwise.
void drawSunIcon(uint8_t f) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  CRGB core = CRGB(255, 140, 0);
  // 4×4 disc in the center (corners trimmed to make it look round)
  for (int y = 2; y <= 5; y++)
    for (int x = 2; x <= 5; x++)
      setPixel(x, y, core);
  setPixel(2, 2, CRGB::Black); setPixel(5, 2, CRGB::Black);
  setPixel(2, 5, CRGB::Black); setPixel(5, 5, CRGB::Black);

  // 8 ray positions around the perimeter
  static const int8_t bx[8] = {3, 6, 7, 6, 4, 1, 0, 1};
  static const int8_t by[8] = {0, 1, 3, 6, 7, 6, 4, 1};
  uint8_t slot = (f / 3) % 8;   // active (brightest) ray slot advances over time
  for (int i = 0; i < 8; i++) {
    int d = (i - slot + 8) % 8;   // distance from active slot
    if      (d == 0)           setPixel(bx[i], by[i], CRGB(255, 220, 0));    // brightest
    else if (d == 1 || d == 7) setPixel(bx[i], by[i], CRGB(120, 80,  0));   // fading
    else if (d == 2 || d == 6) setPixel(bx[i], by[i], CRGB(40,  20,  0));   // dim
    // d==3,4,5 → no pixel → gap in the ray pattern
  }
}

// Partly cloudy: static sun in the lower-left, scrolling cloud across top.
void drawPartlyCloudyIcon(uint8_t f) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  CRGB sunC = CRGB(255, 180, 0);
  for (int y = 5; y <= 7; y++)
    for (int x = 0; x <= 2; x++)
      setPixel(x, y, sunC);
  // Animated glow at the sun-cloud boundary
  uint8_t p = sin8(f * 6);
  setPixel(3, 5, CRGB(p, (uint8_t)(p / 2), 0));
  setPixel(0, 4, CRGB(p, (uint8_t)(p / 2), 0));
  // Cloud scrolls slowly left-to-right across the top 4 rows
  int cx = (int)((f / 4) % 14) - 3;   // cx cycles from -3 to +10
  CRGB cloudC = CRGB(160, 160, 185);
  for (int dx = 0; dx < 6; dx++) {
    int sx = cx + dx;
    if (sx >= 0 && sx < 8) { setPixel(sx, 1, cloudC); setPixel(sx, 2, cloudC); setPixel(sx, 3, cloudC); }
    int top = cx + dx - 1;
    if (dx > 0 && dx < 5 && top >= 0 && top < 8) setPixel(top, 0, cloudC);
  }
}

// Overcast: oval cloud shape with a pulsing brightness.
void drawCloudyIcon(uint8_t f) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  uint8_t br = 90 + sin8(f * 3) / 4;   // brightness pulses gently
  CRGB cloudC = CRGB(br, br, (uint8_t)(br + 20));   // slight blue tint
  // Oval cloud shape
  for (int x = 1; x <= 6; x++) { setPixel(x, 3, cloudC); setPixel(x, 4, cloudC); }
  for (int x = 2; x <= 5; x++) { setPixel(x, 2, cloudC); setPixel(x, 5, cloudC); }
  setPixel(3, 1, cloudC); setPixel(4, 1, cloudC);
  setPixel(2, 5, cloudC); setPixel(5, 5, cloudC);
}

// Fog: horizontal bands at varying brightness that ripple over time.
void drawFogIcon(uint8_t f) {
  for (int y = 0; y < MATRIX_H; y++) {
    // Each row has a different sine phase → rolling fog bands
    uint8_t br = 40 + sin8(f * 2 + (uint8_t)(y * 35)) / 5;
    CRGB fogC  = CRGB(br, br, (uint8_t)(br + 10));
    for (int x = 0; x < MATRIX_W; x++) setPixel(x, y, fogC);
  }
}

// Rain: dark cloud at top, 5 animated raindrops falling below.
// Drops are offset so they don't all hit the same row simultaneously.
void drawRainIcon(uint8_t f) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  CRGB cloudC = CRGB(120, 120, 150);
  for (int x = 0; x < 8; x++) { setPixel(x, 2, cloudC); setPixel(x, 3, cloudC); }
  for (int x = 2; x < 6; x++)   setPixel(x, 1, cloudC);
  setPixel(3, 0, cloudC); setPixel(4, 0, cloudC);
  // 5 raindrop columns with per-column row offsets so they fall out of phase
  static const int8_t  rcol[5] = {0, 2, 4, 5, 7};
  static const uint8_t roff[5] = {0, 2, 1, 3, 0};
  for (int i = 0; i < 5; i++) {
    int row = 4 + (int)((f + roff[i]) % 4);
    if (row < 8)      setPixel(rcol[i], row,     CRGB(0, 80,  255));   // bright droplet head
    if (row - 1 >= 4) setPixel(rcol[i], row - 1, CRGB(0, 25,  80));    // dim tail
  }
}

// Snow: cloud at top, 4 white flakes drifting slowly downward.
void drawSnowIcon(uint8_t f) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  CRGB cloudC = CRGB(160, 160, 185);
  for (int x = 0; x < 8; x++) { setPixel(x, 2, cloudC); setPixel(x, 3, cloudC); }
  for (int x = 2; x < 6; x++)   setPixel(x, 1, cloudC);
  setPixel(3, 0, cloudC); setPixel(4, 0, cloudC);
  static const int8_t  scol[4] = {1, 3, 5, 7};
  static const uint8_t soff[4] = {0, 2, 1, 3};
  for (int i = 0; i < 4; i++) {
    // Snow falls at half speed (f/2) to look lighter than rain
    int row = 4 + (int)((f / 2 + soff[i]) % 4);
    if (row < 8) setPixel(scol[i], row, CRGB(200, 220, 255));
  }
}

// Thunder: dark cloud + brief lightning bolt that flashes every ~1.3 seconds.
void drawThunderIcon(uint8_t f) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  CRGB cloudC = CRGB(60, 60, 80);   // very dark grey cloud
  for (int x = 0; x < 8; x++) { setPixel(x, 2, cloudC); setPixel(x, 3, cloudC); }
  for (int x = 1; x < 7; x++)   setPixel(x, 1, cloudC);
  for (int x = 2; x < 6; x++)   setPixel(x, 0, cloudC);
  // Lightning bolt visible for 2 frames out of every 20 (brief flash)
  if ((f % 20) < 2) {
    CRGB bolt = CRGB(255, 255, 180);
    setPixel(4, 4, bolt); setPixel(3, 5, bolt); setPixel(4, 5, bolt);
    setPixel(3, 6, bolt); setPixel(2, 7, bolt);
  }
}

// ── drawValueOverlay ──────────────────────────────────────────
// Generic number overlay using MINI_FONT — handles 1, 2, and 3-digit values.
// colT = color for the tens digit, colU = color for the units digit.
// Used for humidity (%), UV index, and pressure (hPa/10).
void drawValueOverlay(int val, CRGB colT, CRGB colU) {
  int v = constrain(abs(val), 0, 999);
  if (v >= 100) {
    // 3-digit: "1" bar at col 0, tens at cols 1-3, units at cols 4-6
    for (int r = 3; r <= 7; r++) setPixel(0, r, colT);
    int t = (v / 10) % 10, u = v % 10;
    for (int c = 0; c < 3; c++)
      for (int row = 0; row < 5; row++) {
        if ((pgm_read_byte(&MINI_FONT[t][c]) >> row) & 1) setPixel(c + 1, row + 3, colT);
        if ((pgm_read_byte(&MINI_FONT[u][c]) >> row) & 1) setPixel(c + 4, row + 3, colU);
      }
  } else if (v >= 10) {
    // 2-digit
    int t = v / 10, u = v % 10;
    for (int c = 0; c < 3; c++)
      for (int row = 0; row < 5; row++) {
        if ((pgm_read_byte(&MINI_FONT[t][c]) >> row) & 1) setPixel(c + 1, row + 3, colT);
        if ((pgm_read_byte(&MINI_FONT[u][c]) >> row) & 1) setPixel(c + 5, row + 3, colU);
      }
  } else {
    // 1-digit: centered
    for (int c = 0; c < 3; c++)
      for (int row = 0; row < 5; row++)
        if ((pgm_read_byte(&MINI_FONT[v][c]) >> row) & 1) setPixel(c + 4, row + 3, colU);
  }
}

// ── drawMetricLabel ───────────────────────────────────────────
// Draws a tiny text label (like "HUM" or "UV") above the number area.
// Uses inline pixel bitmaps: 8 bytes, one per column, bit0=top row.
// rows=2 for 2-row-tall labels, rows=3 for 3-row-tall labels.
//
// "UV"  — stored as {3,2,3,0,1,2,1,0}: U in cols 0-2, gap, V in cols 4-6
// "HUM" — stored as {7,2,0,7,4,0,7,5}: H in cols 0-1, gap, U in cols 3-4, gap, M in cols 6-7
void drawMetricLabel(const uint8_t bits[8], int rows, CRGB color) {
  for (int x = 0; x < 8; x++)
    for (int r = 0; r < rows; r++)
      if ((bits[x] >> r) & 1) setPixel(x, r, color);
}

// ── stepWeatherFrame ──────────────────────────────────────────
// Main weather animation tick. Manages three things:
//   1. Refresh: re-fetches wttr.in data every 10 minutes
//   2. Phase switch: toggles between data overlay (2s) and icon (3s)
//   3. Rendering: draws either the icon or the data overlay
void stepWeatherFrame() {
  // Refresh weather data every 10 minutes (600000ms)
  if ((millis() - lastWeatherFetch) >= 600000UL) fetchWeather();

  // Phase timer: show data for 2s, then icon for 3s, repeat
  uint32_t elapsed = millis() - weatherPhaseStart;
  if (!weatherShowIcon && elapsed >= 2000UL) {
    weatherShowIcon   = true;
    weatherPhaseStart = millis();
  } else if (weatherShowIcon && elapsed >= 3000UL) {
    weatherShowIcon   = false;
    weatherPhaseStart = millis();
  }

  weatherFrame++;   // drives animations in all the icon draw functions

  if (weatherShowIcon) {
    // Icon phase: remote PNG or animated pixel art based on icon_source setting
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
    // Data phase: show temperature, humidity, UV, or pressure
    fill_solid(leds, NUM_LEDS, CRGB::Black);

    // "cycle" mode rotates through all four metrics every 6 seconds each
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
      drawMetricLabel(humBits, 3, CHSV(130, 200, 130));   // teal "HUM" label
      drawValueOverlay(weatherHumidity,      CHSV(130, 200, 110), CHSV(147, 180, 110));
    } else if (mode == "uv") {
      static const uint8_t uvBits[8]  = {3, 2, 3, 0, 1, 2, 1, 0};
      drawMetricLabel(uvBits,  2, CHSV(25,  230, 130));   // amber "UV" label
      drawValueOverlay(weatherUvIndex,        CHSV(25,  230, 110), CHSV(42,  210, 110));
    } else if (mode == "pressure" && weatherPressure != 0) {
      // Divide by 10 so 3-digit hPa values (e.g. 1013 → 101) fit the 3-digit display
      drawValueOverlay(weatherPressure / 10,  CHSV(192, 200, 110), CHSV(212, 185, 110));
    }
  }
}

// ============================================================
// WEATHER 2 — Icon (rows 0-3) + Temp (rows 5-7) simultaneously
// ============================================================

// ── Sparkle structures ────────────────────────────────────────
struct W2Ray {
  uint8_t row, col;
  uint8_t phase;   // 1-39 = active sparkle; 0 = waiting
  uint8_t delay;   // countdown frames before next sparkle
};

struct W2Precip {
  uint8_t col;
  uint8_t phase;
  uint8_t delay;
};

static W2Ray    w2Rays[5] = {
  {2, 0, 0,  2},
  {2, 7, 0, 10},
  {3, 2, 0,  5},
  {3, 4, 0, 18},
  {3, 6, 0, 13},
};
static W2Precip w2Precip[4] = {
  {0, 0,  0},
  {0, 0,  3},
  {0, 0,  5},
  {0, 0,  1},
};

// ── Shared cloud shape ────────────────────────────────────────
// Poofy bottom: narrow top (cols 2-5), body (cols 1-6), bottom (cols 1-6)
void drawW2Cloud(CRGB c) {
  for (int x = 2; x <= 5; x++) setPixel(x, 0, c);
  for (int x = 1; x <= 6; x++) setPixel(x, 1, c);
  for (int x = 1; x <= 6; x++) setPixel(x, 2, c);
}

// ── Temp + degree dot ─────────────────────────────────────────
void drawW2Digit(int digit, int startCol, CRGB color) {
  int idx = 26 + (digit % 10);
  for (int c = 0; c < 3; c++) {
    uint8_t bits = pgm_read_byte(&FONT_3X3[idx][c]);
    for (int r = 0; r < 3; r++)
      if ((bits >> r) & 1) setPixel(startCol + c, r + 5, color);
  }
}

void drawWeather2Temp() {
  setPixel(7, 4, CRGB(0, 200, 255));   // degree dot — always cyan

  int val    = (weather2Unit == "F") ? weatherTempF : weatherTempC;
  int absVal = abs(val);

  if (absVal >= 100) {
    for (int r = 5; r <= 7; r++) setPixel(0, r, weather2Color1);  // thin "1" bar
    drawW2Digit((absVal / 10) % 10, 2, weather2Color1);
    drawW2Digit(absVal % 10,        5, weather2Color2);
  } else if (absVal >= 10) {
    drawW2Digit(absVal / 10, 0, weather2Color1);
    drawW2Digit(absVal % 10, 4, weather2Color2);
  } else {
    drawW2Digit(absVal, 3, weather2Color2);
  }
}

// ── Icon: Sunny ───────────────────────────────────────────────
void drawSunnyIcon2() {
  CRGB core = CRGB(255, 220, 0);
  CRGB tip  = CRGB(255, 192, 0);
  CRGB ray  = CRGB(255, 120, 0);

  for (int x = 0; x < 8; x++) setPixel(x, 0, core);
  for (int x = 1; x <= 6; x++) setPixel(x, 1, core);
  for (int x = 2; x <= 5; x++) setPixel(x, 2, tip);

  for (int i = 0; i < 5; i++) {
    W2Ray& r = w2Rays[i];
    if (r.phase > 0) {
      uint8_t bri = (uint8_t)(sinf(r.phase * 3.14159f / 39.0f) * 255.0f);
      CRGB c = ray;
      c.nscale8(max(bri, (uint8_t)1));
      setPixel(r.col, r.row, c);
      if (++r.phase >= 40) { r.phase = 0; r.delay = 15 + (uint8_t)random(30); }
    } else if (r.delay > 0) {
      r.delay--;
    } else {
      r.phase = 1;
    }
  }
}

// ── Icon: Partly Cloudy ───────────────────────────────────────
void drawPartlyCloudyIcon2() {
  CRGB core  = CRGB(255, 220, 0);
  CRGB tip   = CRGB(255, 192, 0);
  CRGB ray   = CRGB(255, 120, 0);
  CRGB cloud = CRGB(216, 228, 240);

  // Sun dome
  for (int x = 0; x < 8; x++) setPixel(x, 0, core);
  for (int x = 1; x <= 6; x++) setPixel(x, 1, core);
  for (int x = 2; x <= 5; x++) setPixel(x, 2, tip);

  // Cloud overwrites right side
  for (int x = 5; x <= 7; x++) setPixel(x, 1, cloud);
  for (int x = 4; x <= 7; x++) setPixel(x, 2, cloud);

  // Only left-side rays (indices 0,2 — positions (2,0) and (3,2))
  for (int i : {0, 2}) {
    W2Ray& r = w2Rays[i];
    if (r.phase > 0) {
      uint8_t bri = (uint8_t)(sinf(r.phase * 3.14159f / 39.0f) * 255.0f);
      CRGB c = ray; c.nscale8(max(bri, (uint8_t)1));
      setPixel(r.col, r.row, c);
      if (++r.phase >= 40) { r.phase = 0; r.delay = 15 + (uint8_t)random(30); }
    } else if (r.delay > 0) { r.delay--;
    } else { r.phase = 1; }
  }
}

// ── Icon: Cloudy ──────────────────────────────────────────────
void drawCloudyIcon2(uint8_t f) {
  uint8_t br = 120 + sin8(f * 3) / 5;
  drawW2Cloud(CRGB(br, br, (uint8_t)(br + 15)));
}

// ── Icon: Fog ─────────────────────────────────────────────────
void drawFogIcon2(uint8_t f) {
  for (int y = 0; y < 4; y++) {
    uint8_t br = 30 + sin8(f * 2 + (uint8_t)(y * 35)) / 6;
    CRGB c = CRGB(br, br, (uint8_t)(br + 10));
    for (int x = 0; x < 8; x++) setPixel(x, y, c);
  }
}

// ── Icon: Rain ────────────────────────────────────────────────
void drawRainIcon2() {
  static const uint8_t RCOLS[4] = {1, 3, 5, 6};
  drawW2Cloud(CRGB(216, 228, 240));

  CRGB bright = CRGB(0, 85, 238);
  for (int i = 0; i < 4; i++) {
    W2Precip& p = w2Precip[i];
    p.col = RCOLS[i];
    if (p.phase > 0) {
      uint8_t bri = (uint8_t)(sinf(p.phase * 3.14159f / 5.0f) * 255.0f);
      CRGB c = bright; c.nscale8(max(bri, (uint8_t)1));
      setPixel(p.col, 3, c);
      if (++p.phase >= 6) { p.phase = 0; p.delay = 2 + (uint8_t)random(6); }
    } else if (p.delay > 0) { p.delay--;
    } else { p.phase = 1; }
  }
}

// ── Icon: Snow ────────────────────────────────────────────────
void drawSnowIcon2() {
  static const uint8_t SCOLS[3] = {2, 4, 6};
  drawW2Cloud(CRGB(216, 228, 240));

  CRGB bright = CRGB(136, 200, 255);
  for (int i = 0; i < 3; i++) {
    W2Precip& p = w2Precip[i];
    p.col = SCOLS[i];
    if (p.phase > 0) {
      uint8_t bri = (uint8_t)(sinf(p.phase * 3.14159f / 9.0f) * 255.0f);
      CRGB c = bright; c.nscale8(max(bri, (uint8_t)1));
      setPixel(p.col, 3, c);
      if (++p.phase >= 10) { p.phase = 0; p.delay = 8 + (uint8_t)random(15); }
    } else if (p.delay > 0) { p.delay--;
    } else { p.phase = 1; }
  }
}

// ── Icon: Thunder ─────────────────────────────────────────────
void drawThunderIcon2(uint8_t f) {
  drawW2Cloud(CRGB(70, 70, 95));
  if ((f % 25) < 2) {
    setPixel(4, 3, CRGB(255, 255, 180));
    setPixel(5, 3, CRGB(255, 255, 180));
  }
}

// ── stepWeather2Frame ─────────────────────────────────────────
void stepWeather2Frame() {
  static uint8_t w2f = 0;
  if ((millis() - lastWeatherFetch) >= 600000UL) fetchWeather();

  w2f++;
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  switch (weatherCategory(weatherCode)) {
    case 0:  drawSunnyIcon2();           break;
    case 1:  drawPartlyCloudyIcon2();    break;
    case 2:  drawCloudyIcon2(w2f);       break;
    case 3:  drawFogIcon2(w2f);          break;
    case 4:  drawRainIcon2();            break;
    case 5:  drawSnowIcon2();            break;
    case 6:  drawThunderIcon2(w2f);      break;
    default: drawSunnyIcon2();           break;
  }

  drawWeather2Temp();
}
