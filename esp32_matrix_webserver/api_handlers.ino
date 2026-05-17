// ============================================================
// SECTION 10: HTTP ROUTE HANDLERS
// One function per API endpoint. Registered in setup().
// ============================================================

void handleClear() {
  stopAll();
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  sendJson(200, "{\"status\":\"ok\"}");
}

void handleBrightness() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }
  int level = constrain((int)(doc["level"] | 50), 0, 255);
  brightness = (uint8_t)level;
  FastLED.setBrightness(brightness);
  FastLED.show();
  sendJson(200, "{\"status\":\"ok\",\"brightness\":" + String(brightness) + "}");
}

void handleText() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }

  stopAll();
  scrollText = String(doc["text"] | "HELLO");
  scrollText.toUpperCase();
  scrollColor    = hexToColor(String(doc["color"]  | "#FFFFFF"));
  scrollColor2   = hexToColor(String(doc["color2"] | "#FF4400"));
  scrollGradient = (bool)(doc["gradient"] | false);
  scrollSmall    = (bool)(doc["small"]    | false);
  scrollTiny     = (bool)(doc["tiny"]     | false);
  if (scrollTiny) scrollSmall = false;
  scrollSpeed    = doc["scroll_speed"] | 100;
  scrollOffset   = 0;
  scrollPausing  = false;
  scrollPixelLen = scrollText.length() * (scrollTiny ? TINY_CHAR_TOTAL : (scrollSmall ? SMALL_CHAR_TOTAL : CHAR_TOTAL));
  lastScrollMs   = millis();
  textActive     = true;

  renderScrollFrame();
  FastLED.show();
  sendJson(200, "{\"status\":\"ok\",\"text\":\"" + scrollText + "\"}");
}

void handleAnimation() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }

  stopAll();
  animationName  = String(doc["type"] | "fire");
  animationSpeed = doc["speed"]  | 66;
  solidColor     = hexToColor(String(doc["color"] | "#0064FF"));

  // Accept "palette" (fire-friendly name) or "theme" (matrix_rain name); palette wins
  String theme = "classic";
  if (!doc["palette"].isNull()) theme = String(doc["palette"].as<const char*>());
  else if (!doc["theme"].isNull()) theme = String(doc["theme"].as<const char*>());
  if      (theme == "blue")   activePalette = PALETTE_BLUE;
  else if (theme == "green")  activePalette = PALETTE_GREEN;
  else if (theme == "purple") activePalette = PALETTE_PURPLE;
  else                        activePalette = PALETTE_CLASSIC;

  fireIntensity = constrain((int)(doc["intensity"] | 6), 1, 10);
  fireTendrils  = constrain((int)(doc["tendrils"]  | 0), 0, 10);
  sparkRate = constrain((int)(doc["sparks"] | 0), 0, 10);

  rainbowHue = breathePhase = waveOffset = 0;
  memset(fireHeat, 0, sizeof(fireHeat));
  memset(columnDrift, 0, sizeof(columnDrift));
  memset(columnActive, 1, sizeof(columnActive));
  initSparks();

  if (animationName == "liquid") {
    float vis     = constrain((float)(int)(doc["viscosity"] | 5), 0.0f, 10.0f);
    liquidDamping = 0.97f - vis * 0.02f;
    for (int x = 0; x < MATRIX_W; x++) {
      liquidHeight[x]   = MATRIX_H * 0.5f;
      liquidVelocity[x] = 0.0f;
    }
  }

  if (animationName == "chiptemp") {
    chipTempUnit = String(doc["units"] | "F");
  }

  if (animationName == "matrix_rain") {
    if      (theme == "blue")   { matrixTrailColor = CRGB(0, 80, 220);  matrixHeadColor = CRGB(180, 220, 255); }
    else if (theme == "red")    { matrixTrailColor = CRGB(220, 20, 0);  matrixHeadColor = CRGB(255, 200, 180); }
    else if (theme == "purple") { matrixTrailColor = CRGB(160, 0, 220); matrixHeadColor = CRGB(230, 200, 255); }
    else                        { matrixTrailColor = CRGB(0, 180, 20);  matrixHeadColor = CRGB::White; }
    initMatrixDrops();
  }

  if (animationName == "clock") {
    clockTimezone = (int)(doc["timezone"] | -7);
    String colorStr = String(doc["color"] | "#003366");
    clockBgColor  = hexToColor(colorStr);
    clockPrevHour = -1;
    clockPrevMin  = -1;
    ntpSynced     = false;
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
    fetchWeather();
  }

  if (animationName == "timer_fill" || animationName == "timer_snow" || animationName == "timer_text") {
    uint32_t dur = (uint32_t)(doc["duration"] | 300);
    timerTotalMs      = dur * 1000UL;
    timerEndMs        = millis() + timerTotalMs;
    timerExpiredState = 0;

    if (animationName == "timer_snow") {
      timerColor1     = CRGB(0, 40, 255);
      timerColor2     = CRGB(220, 240, 255);
      timerColorColon = CRGB::White;
    } else if (animationName == "timer_text") {
      timerColor1     = CRGB(255, 200, 0);    // minutes: yellow
      timerColor2     = CRGB(255, 100, 0);    // seconds: orange
      timerColorColon = CRGB::White;           // colon: white
    } else {
      timerColor1     = CRGB(255, 200, 0);
      timerColor2     = CRGB(255, 0, 0);
      timerColorColon = CRGB::White;
    }
    const char* c1 = doc["color1"] | "";
    const char* c2 = doc["color2"] | "";
    const char* c3 = doc["color3"] | "";
    if (strlen(c1) > 0) timerColor1     = hexToColor(String(c1));
    if (strlen(c2) > 0) timerColor2     = hexToColor(String(c2));
    if (strlen(c3) > 0) timerColorColon = hexToColor(String(c3));

    if (animationName == "timer_snow") {
      // Build bottom-to-top order, shuffle within each row
      for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) snowPos[r*8+c] = {(uint8_t)c, (uint8_t)r};
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

void handleTemperature() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }

  stopAll();

  if (doc["matrix"].is<JsonArray>()) {
    JsonArray matrix = doc["matrix"];
    for (int y = 0; y < 8 && y < (int)matrix.size(); y++) {
      JsonArray row = matrix[y];
      for (int x = 0; x < 8 && x < (int)row.size(); x++) {
        setPixel(x, y, hexToColor(row[x].as<String>()));
      }
    }
    FastLED.show();
  } else {
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

void handleSensorTemperature() {
  float tempC = temperatureRead();
  float tempF = tempC * 9.0f / 5.0f + 32.0f;
  String json = "{\"celsius\":"    + String(tempC, 1) +
                ",\"fahrenheit\":" + String(tempF, 1) +
                ",\"note\":\"Chip temperature — not room temperature\"}";
  sendJson(200, json);
}

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

// POST /api/weather/mode — switch data overlay while animation is running
void handleWeatherMode() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }
  weatherDataMode = String(doc["mode"] | "temp");
  sendJson(200, "{\"ok\":true}");
}

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
