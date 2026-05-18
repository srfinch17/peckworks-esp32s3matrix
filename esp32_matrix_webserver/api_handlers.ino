// ============================================================
// SECTION 10: HTTP ROUTE HANDLERS
//
// One function per API endpoint. All registered in setup().
//
// COMMON PATTERN for POST handlers:
//   1. Deserialize the JSON body from server.arg("plain")
//   2. Validate — return 400 if JSON is malformed
//   3. Update firmware state
//   4. Return JSON response: {"status":"ok"} on success
//
// ArduinoJson's | operator: doc["key"] | defaultValue
//   Returns defaultValue if "key" is missing or null.
//   This eliminates the need for separate null checks on every field.
// ============================================================

// POST /api/display/clear
// Stops all animations and text, blanks all LEDs.
void handleClear() {
  stopAll();
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  sendJson(200, "{\"status\":\"ok\"}");
}

// POST /api/brightness — body: {"level": 0-255}
// Sets the global FastLED brightness. Takes effect immediately.
void handleBrightness() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }
  // constrain() clamps a value to [lo, hi] — prevents bad values from crashing FastLED
  int level = constrain((int)(doc["level"] | 50), 0, 255);
  brightness = (uint8_t)level;
  FastLED.setBrightness(brightness);
  FastLED.show();
  sendJson(200, "{\"status\":\"ok\",\"brightness\":" + String(brightness) + "}");
}

// POST /api/display/text
// Starts a scrolling text animation.
// Params: text, color (hex), color2 (hex), gradient (bool),
//         small (bool), tiny (bool), scroll_speed (ms per tick)
void handleText() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }

  stopAll();

  // Populate all the scroll_* globals used by renderScrollFrame()
  scrollText = String(doc["text"] | "HELLO");
  scrollText.toUpperCase();   // font only has uppercase glyphs
  scrollColor    = hexToColor(String(doc["color"]  | "#FFFFFF"));
  scrollColor2   = hexToColor(String(doc["color2"] | "#FF4400"));
  scrollGradient = (bool)(doc["gradient"] | false);
  scrollSmall    = (bool)(doc["small"]    | false);
  scrollTiny     = (bool)(doc["tiny"]     | false);
  if (scrollTiny) scrollSmall = false;   // tiny overrides small
  scrollSpeed    = doc["scroll_speed"] | 100;
  scrollOffset   = 0;
  scrollPausing  = false;

  // Total pixel length of the text: used for gradient interpolation and loop reset.
  // Each character takes (charW + 1 gap) pixels. TINY and SMALL have narrower strides.
  scrollPixelLen = scrollText.length() * (scrollTiny ? TINY_CHAR_TOTAL : (scrollSmall ? SMALL_CHAR_TOTAL : CHAR_TOTAL));
  lastScrollMs   = millis();
  textActive     = true;

  renderScrollFrame();   // render the first frame immediately
  FastLED.show();
  sendJson(200, "{\"status\":\"ok\",\"text\":\"" + scrollText + "\"}");
}

// POST /api/display/animation
// Starts one of the built-in animations. This is the big one —
// it initializes every animation type, so there are a lot of cases.
// Params: type (required), plus animation-specific params.
void handleAnimation() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }

  stopAll();   // stop any currently running animation or text scroll
  animationName  = String(doc["type"] | "fire");
  animationSpeed = doc["speed"]  | 66;   // ms per frame; MCP server translates 1-5 scale to this
  solidColor     = hexToColor(String(doc["color"] | "#0064FF"));

  // Theme/palette — same concept, two names (fire calls it "palette", matrix_rain calls it "theme").
  // We accept either. If both are provided, "palette" wins.
  String theme = "classic";
  if (!doc["palette"].isNull()) theme = String(doc["palette"].as<const char*>());
  else if (!doc["theme"].isNull()) theme = String(doc["theme"].as<const char*>());
  if      (theme == "blue")   activePalette = PALETTE_BLUE;
  else if (theme == "green")  activePalette = PALETTE_GREEN;
  else if (theme == "purple") activePalette = PALETTE_PURPLE;
  else                        activePalette = PALETTE_CLASSIC;

  // Fire-specific params
  fireIntensity = constrain((int)(doc["intensity"] | 6), 1, 10);
  fireTendrils  = constrain((int)(doc["tendrils"]  | 0), 0, 10);
  sparkRate = constrain((int)(doc["sparks"] | 0), 0, 10);

  // Reset shared animation state so every animation starts clean
  rainbowHue = breathePhase = waveOffset = 0;
  memset(fireHeat, 0, sizeof(fireHeat));
  memset(columnDrift, 0, sizeof(columnDrift));
  memset(columnActive, 1, sizeof(columnActive));   // all columns active by default
  initSparks();

  if (animationName == "liquid") {
    // viscosity param: 0 = thin/sloshy, 10 = thick/sluggish
    // Maps to liquidDamping: higher viscosity → lower damping coeff (more energy lost per frame)
    float vis     = constrain((float)(int)(doc["viscosity"] | 5), 0.0f, 10.0f);
    liquidDamping = 0.97f - vis * 0.02f;
    // Initialize all columns to mid-height (flat surface)
    for (int x = 0; x < MATRIX_W; x++) {
      liquidHeight[x]   = MATRIX_H * 0.5f;
      liquidVelocity[x] = 0.0f;
    }
  }

  if (animationName == "chiptemp") {
    chipTempUnit = String(doc["units"] | "F");
  }

  if (animationName == "matrix_rain") {
    // Set head and trail colors based on the chosen theme
    if      (theme == "blue")   { matrixTrailColor = CRGB(0, 80, 220);  matrixHeadColor = CRGB(180, 220, 255); }
    else if (theme == "red")    { matrixTrailColor = CRGB(220, 20, 0);  matrixHeadColor = CRGB(255, 200, 180); }
    else if (theme == "purple") { matrixTrailColor = CRGB(160, 0, 220); matrixHeadColor = CRGB(230, 200, 255); }
    else                        { matrixTrailColor = CRGB(0, 180, 20);  matrixHeadColor = CRGB::White; }   // classic green
    initMatrixDrops();   // stagger drop start positions so they don't all begin at row 0
  }

  if (animationName == "clock") {
    clockTimezone = (int)(doc["timezone"] | -7);
    String colorStr = String(doc["color"] | "#003366");
    clockBgColor  = hexToColor(colorStr);
    clockPrevHour = -1;
    clockPrevMin  = -1;
    ntpSynced     = false;
    // configTime sets up the ESP32's POSIX time library.
    // timezone * 3600 converts the UTC offset (hours) to seconds.
    // pool.ntp.org and time.nist.gov are public NTP servers.
    configTime((long)clockTimezone * 3600L, 0, "pool.ntp.org", "time.nist.gov");
  }

  if (animationName == "weather") {
    weatherZip        = String(doc["zipcode"]     | "85013");
    weatherUnit       = String(doc["units"]       | "F");
    weatherDataMode   = String(doc["data_mode"]   | "temp");
    weatherIconSource = String(doc["icon_source"] | "animated");
    weatherFrame      = 0;
    weatherHasIcon    = false;
    weatherShowIcon   = false;
    weatherPhaseStart = millis();
    fetchWeather();   // fetch immediately; loop will re-fetch every 10 minutes
  }

  if (animationName == "timer_fill" || animationName == "timer_snow" || animationName == "timer_text") {
    uint32_t dur  = (uint32_t)(doc["duration"] | 300);   // default 5 minutes
    timerTotalMs  = dur * 1000UL;
    timerEndMs    = millis() + timerTotalMs;
    timerExpiredState = 0;

    // Default colors per timer type — overridden by color1/color2/color3 params below
    if (animationName == "timer_snow") {
      timerColor1     = CRGB(0, 40, 255);       // blue snowflakes
      timerColor2     = CRGB(220, 240, 255);    // near-white snow at the top
      timerColorColon = CRGB::White;
    } else if (animationName == "timer_text") {
      timerColor1     = CRGB(255, 200, 0);      // minutes: yellow
      timerColor2     = CRGB(255, 100, 0);      // seconds: orange
      timerColorColon = CRGB::White;
    } else {
      timerColor1     = CRGB(255, 200, 0);      // fill start: yellow
      timerColor2     = CRGB(255, 0, 0);        // fill end: red
      timerColorColon = CRGB::White;
    }

    // User-supplied colors override the defaults
    const char* c1 = doc["color1"] | "";
    const char* c2 = doc["color2"] | "";
    const char* c3 = doc["color3"] | "";
    if (strlen(c1) > 0) timerColor1     = hexToColor(String(c1));
    if (strlen(c2) > 0) timerColor2     = hexToColor(String(c2));
    if (strlen(c3) > 0) timerColorColon = hexToColor(String(c3));

    if (animationName == "timer_snow") {
      // Build the settlement order: 64 cells (8 rows × 8 cols), sorted bottom-to-top.
      // Within each row, cells are shuffled randomly using Fisher-Yates.
      // This gives each timer run a unique snowfall pattern while
      // guaranteeing cells settle from the bottom row upward.
      for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) snowPos[r*8+c] = {(uint8_t)c, (uint8_t)r};
        // Fisher-Yates shuffle: swap element i with a random element in 0..i
        for (int i = 7; i > 0; i--) {
          int j = random(i + 1);
          SnowCell tmp = snowPos[r*8+i]; snowPos[r*8+i] = snowPos[r*8+j]; snowPos[r*8+j] = tmp;
        }
      }
      snowSettledCount = 0;
      snowFallActive   = false;
    }
  }

  animationActive = true;
  sendJson(200, "{\"status\":\"ok\",\"animation\":\"" + animationName + "\"}");
}

// POST /api/display/matrix — body: {"matrix": [[8 rows of 8 hex color strings]]}
// Lets the caller paint arbitrary pixels by sending a full 8×8 color grid.
// Each cell is a hex string like "#FF0000". Stops all animations first.
void handleMatrix() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJson(400, "{\"error\":\"Invalid JSON — body may exceed buffer\"}");
    return;
  }

  stopAll();
  JsonArray matrix = doc["matrix"];
  if (matrix.isNull() || matrix.size() != 8) {
    sendJson(400, "{\"error\":\"matrix must have exactly 8 rows\"}");
    return;
  }
  for (int y = 0; y < 8; y++) {
    JsonArray row = matrix[y];
    if (row.isNull() || row.size() != 8) {
      sendJson(400, "{\"error\":\"each row must have exactly 8 columns\"}");
      return;
    }
    for (int x = 0; x < 8; x++) {
      setPixel(x, y, hexToColor(row[x].as<String>()));
    }
  }
  FastLED.show();
  sendJson(200, "{\"status\":\"ok\"}");
}

// POST /api/display/temperature
// Legacy endpoint: accepts either a 8×8 matrix (direct pixel control)
// or a {value, unit, color} object (scrolls the temperature as text).
// Used by early web UI; prefer matrix_set_animation type=chiptemp for new code.
void handleTemperature() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }

  stopAll();

  if (doc["matrix"].is<JsonArray>()) {
    // Direct pixel mode: paint whatever matrix the caller sends
    JsonArray matrix = doc["matrix"];
    for (int y = 0; y < 8 && y < (int)matrix.size(); y++) {
      JsonArray row = matrix[y];
      for (int x = 0; x < 8 && x < (int)row.size(); x++) {
        setPixel(x, y, hexToColor(row[x].as<String>()));
      }
    }
    FastLED.show();
  } else {
    // Text mode: scroll the temperature value as a number string
    float  value = doc["value"] | 0.0f;
    String unit  = String(doc["unit"] | "F");
    CRGB   color = hexToColor(String(doc["color"] | "#FFFFFF"));

    scrollText     = String((int)round(value)) + unit;
    scrollColor    = color;
    scrollSpeed    = 150;
    scrollOffset   = 0;
    scrollPixelLen = scrollText.length() * CHAR_TOTAL;
    lastScrollMs   = millis();
    textActive     = true;

    renderScrollFrame();
    FastLED.show();
  }

  sendJson(200, "{\"status\":\"ok\"}");
}

// GET /api/sensors/temperature
// Returns the ESP32's internal die temperature in both C and F.
// temperatureRead() is a built-in ESP-IDF function.
// Note in the response: chip temp runs 10-15°C above ambient — this is normal.
void handleSensorTemperature() {
  float tempC = temperatureRead();
  float tempF = tempC * 9.0f / 5.0f + 32.0f;
  String json = "{\"celsius\":"    + String(tempC, 1) +
                ",\"fahrenheit\":" + String(tempF, 1) +
                ",\"note\":\"Chip temperature — not room temperature\"}";
  sendJson(200, json);
}

// GET /api/sensors/accelerometer
// Returns raw X/Y/Z accelerometer values in g-force units.
// Returns 503 if the IMU failed to initialize at boot.
void handleSensorAccelerometer() {
  float ax, ay, az;
  if (imuReady) {
    readAccel(ax, ay, az);
    String json = "{\"ax\":" + String(ax, 3) +
                  ",\"ay\":" + String(ay, 3) +
                  ",\"az\":" + String(az, 3) +
                  ",\"ready\":true}";
    sendJson(200, json);
  } else {
    sendJson(503, "{\"error\":\"IMU not detected\",\"ready\":false}");
  }
}

// POST /api/weather/mode — body: {"mode": "temp"|"humidity"|"uv"|"pressure"|"cycle"}
// Hot-swaps the data overlay while the weather animation is already running.
// Useful for cycling through metrics without restarting the animation.
void handleWeatherMode() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }
  weatherDataMode = String(doc["mode"] | "temp");
  sendJson(200, "{\"ok\":true}");
}

// GET /api/sensors/weather
// Returns the most recently fetched weather data.
// Data is cached from the last fetchWeather() call; it only refreshes
// when the weather animation is running (every 10 minutes).
void handleSensorWeather() {
  String json = "{\"zip\":\""    + weatherZip    + "\""
                ",\"code\":"     + String(weatherCode) +
                ",\"category\":" + String(weatherCategory(weatherCode)) +
                ",\"temp\":"     + String(weatherTempVal) +
                ",\"unit\":\""   + weatherUnit   + "\""
                ",\"humidity\":" + String(weatherHumidity) +
                ",\"uvIndex\":"  + String(weatherUvIndex) +
                ",\"pressure\":" + String(weatherPressure) +
                "}";
  sendJson(200, json);
}

// GET /api/status
// Returns the current board state as JSON:
//   - Always: brightness
//   - If text is scrolling: text content, font size, gradient, speed
//   - If animation is running: animation name, plus mode-specific fields
//     (timer has remaining/total seconds, weather has zip/data mode/units,
//      clock has timezone and NTP sync status)
//   - If idle: state = "idle"
// This is what the MCP server's matrix_status tool calls.
void handleStatus() {
  String json = "{";
  json += "\"brightness\":" + String(brightness);

  if (textActive) {
    json += ",\"state\":\"text\"";
    json += ",\"text\":\"" + scrollText + "\"";
    json += ",\"size\":\"" + String(scrollTiny ? "tiny" : scrollSmall ? "small" : "normal") + "\"";
    json += ",\"gradient\":" + String(scrollGradient ? "true" : "false");
    json += ",\"scroll_speed\":" + String(scrollSpeed);
  } else if (animationActive) {
    json += ",\"state\":\"animation\"";
    json += ",\"animation\":\"" + animationName + "\"";

    if (animationName == "timer_fill" || animationName == "timer_snow" || animationName == "timer_text") {
      long remaining = (long)(timerEndMs - millis());
      if (remaining < 0) remaining = 0;
      json += ",\"timer_remaining_seconds\":" + String(remaining / 1000);
      json += ",\"timer_total_seconds\":"     + String(timerTotalMs / 1000);
    }
    if (animationName == "weather") {
      json += ",\"zip\":\""       + weatherZip      + "\"";
      json += ",\"data_mode\":\"" + weatherDataMode + "\"";
      json += ",\"units\":\""     + weatherUnit     + "\"";
    }
    if (animationName == "clock") {
      json += ",\"timezone\":" + String(clockTimezone);
      json += ",\"ntp_synced\":" + String(ntpSynced ? "true" : "false");
    }
    if (animationName == "chiptemp") {
      json += ",\"units\":\"" + chipTempUnit + "\"";
    }
  } else {
    json += ",\"state\":\"idle\"";
  }

  json += "}";
  sendJson(200, json);
}
