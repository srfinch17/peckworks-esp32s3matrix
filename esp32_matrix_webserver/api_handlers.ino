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

// Escapes a string for safe embedding inside a JSON string value.
// Caller-supplied text (scroll text, zip, request paths) can contain '"' or '\',
// which would otherwise produce malformed JSON in our hand-built responses.
static String escapeJson(const String& s) {
  String out;
  out.reserve(s.length() + 4);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if ((uint8_t)c < 0x20) out += ' ';   // control chars also break JSON
    else out += c;
  }
  return out;
}

// POST /api/display/clear
// Stops all animations and text, blanks all LEDs.
void handleClear() {
  stopAll();
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  resumeKind = "off"; resumeDirty = true; resumeDirtyMs = millis();   // auto-resume: stay blank on next boot
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
  resumeBri  = brightness;   // only user-committed brightness persists to NVS (not grid-test's 255)
  FastLED.setBrightness(brightness);
  FastLED.show();
  resumeDirty = true; resumeDirtyMs = millis();   // debounced auto-resume save (avoids NVS churn on slider drags)
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
  scrollColor3   = hexToColor(String(doc["color3"] | "#00CC64"));
  scrollColor4   = hexToColor(String(doc["color4"] | "#0064FF"));
  scrollGradient = (bool)(doc["gradient"] | false);
  scrollSmall    = (bool)(doc["small"]    | false);
  scrollTiny     = (bool)(doc["tiny"]     | false);
  if (scrollTiny) scrollSmall = false;   // tiny overrides small
  scrollSpeed    = (uint32_t)constrain((int)(doc["scroll_speed"] | 100), 10, 5000);   // negative would wrap the uint32 to ~49 days/step
  scrollOffset   = 0;
  scrollPausing  = false;

  // Total pixel length of the text: used for gradient interpolation and loop reset.
  // Each character takes (charW + 1 gap) pixels. TINY and SMALL have narrower strides.
  scrollPixelLen = scrollText.length() * (scrollTiny ? TINY_CHAR_TOTAL : (scrollSmall ? SMALL_CHAR_TOTAL : CHAR_TOTAL));
  lastScrollMs   = millis();
  textActive     = true;

  renderScrollFrame();   // render the first frame immediately
  FastLED.show();
  sendJson(200, "{\"status\":\"ok\",\"text\":\"" + escapeJson(scrollText) + "\"}");
}

// POST /api/display/animation
// Starts one of the built-in animations. This is the big one —
// it initializes every animation type, so there are a lot of cases.
// Params: type (required), plus animation-specific params.
// Start NTP using a POSIX TZ string (DST-aware) when 'tz' is given, else a fixed
// UTC offset from 'timezone'. Shared by the clock and calendar modes.
//
// Only (re)starts SNTP when the requested config actually CHANGED: configTzTime/
// configTime restart the SNTP client from scratch, so clicking through calendar
// styles (each click re-sends the same tz) right after boot kept aborting the
// FIRST sync before it could complete — every style sat pulsing white "waiting
// for NTP" while the restarts piled up.
static String ntpActiveCfg = "";   // what SNTP was last started with; "" = never
static void startNtp(JsonDocument& doc) {
  const char* tz = doc["tz"] | "";
  int offset = (int)(doc["timezone"] | -7);
  String cfg = (strlen(tz) > 0) ? String("tz:") + tz : String("off:") + String(offset);
  if (cfg == ntpActiveCfg) return;   // same config — let the in-flight/periodic sync run
  ntpActiveCfg = cfg;
  if (strlen(tz) > 0) {
    clockTZ = String(tz);
    configTzTime(clockTZ.c_str(), "pool.ntp.org", "time.nist.gov");
  } else {
    clockTZ = "";   // fixed offset is now the active config — don't let status report a stale tz string
    clockTimezone = offset;
    configTime((long)clockTimezone * 3600L, 0, "pool.ntp.org", "time.nist.gov");
  }
}

// Every name the loop() dispatch chain knows how to render. An unrecognized type
// must be rejected here: it would otherwise be accepted, persisted for auto-resume,
// match no dispatch branch, and the board would "resume" into a black screen.
static const char* const KNOWN_ANIMS[] = {
  "fire", "rainbow", "breathe", "wave", "solid", "liquid", "imu", "chiptemp",
  "weather", "weather2", "timer_fill", "timer_snow", "timer_text", "clock",
  "matrix_rain", "dancefloor", "spiral", "starfield", "fireworks", "fireworks2",
  "comet", "sun", "frostbite", "calendar", "sound"
};

// Applies an animation command from a JSON body. Shared by the HTTP handler
// (handleAnimation) and the boot-time auto-resume in setup(). Returns false on
// malformed JSON or an unknown animation type (checked BEFORE stopping the
// current display, so a bad request leaves the board showing what it was).
// Does NOT send an HTTP response or persist anything.
bool applyAnimationBody(const String& body) {
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) return false;

  String reqType = String(doc["type"] | "fire");
  bool known = false;
  for (auto n : KNOWN_ANIMS) if (reqType == n) { known = true; break; }
  if (!known) return false;

  stopAll();   // stop any currently running animation or text scroll
  animationName  = reqType;
  animationSpeed = (uint32_t)constrain((int)(doc["speed"] | 66), 10, 10000);   // ms per frame; MCP translates 1-5 to this. Clamp: negative wraps the uint32
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

    // Color: either the shared palette (already set above from theme) or a
    // custom top/bottom gradient.
    liquidGradient = doc["gradient"] | false;
    if (liquidGradient) {
      liquidTopColor    = hexToColor(String(doc["top"]    | "#E6FAFF"));  // surface/froth
      liquidBottomColor = hexToColor(String(doc["bottom"] | "#0028A0"));  // deep
    }

    // Reset the fluid to settle from a flat start.
    liquidLevel = 0.0f;  liquidLevelVel = 0.0f;
    liquidGX    = 0.0f;  liquidGY       = 1.0f;   // default "down" until the IMU reports
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
    // Accept color1/2/3 (the shared MCP convention) as aliases for the named
    // keys the web clock page sends. color1=hours, color2=minutes, color3=colon.
    clockColorHours  = hexToColor(String(doc["colorHours"]   | (doc["color1"] | "#FF3300")));
    clockColorColon  = hexToColor(String(doc["colorColon"]   | (doc["color3"] | "#FFFFFF")));
    clockColorMins   = hexToColor(String(doc["colorMinutes"] | (doc["color2"] | "#00CCFF")));
    clockPrevHour    = -1;
    clockPrevMin     = -1;
    ntpSynced        = false;
    startNtp(doc);   // tz (POSIX, DST-aware) or timezone (fixed offset)
  }

  if (animationName == "calendar") {
    calendarStyle   = String(doc["style"] | "scroll");   // scroll | bignum | grid | clock | square
    calendarColor1  = hexToColor(String(doc["color1"] | "#00C8FF"));   // primary (day/text/today)
    calendarColor2  = hexToColor(String(doc["color2"] | "#FF7800"));   // secondary (month/other days)
    calendarColor3  = hexToColor(String(doc["color3"] | "#50505A"));   // accent (weekday letter / weekend cols)
    calendarScrollX = MATRIX_W;
    calendarScrollMono = doc["scroll_mono"] | false;   // scroll: single-color (color1) vs weekday/month/day in color1/2/3
    // Scroll style: ms per 1px advance — calendar.html's speed slider maps to 150 (slow) … 24 (fast)
    calendarScrollMs = (uint32_t)constrain((int)(doc["speed"] | 80), 24, 400);
    ntpSynced       = false;
    startNtp(doc);   // same NTP plumbing as the clock (tz POSIX/DST or fixed offset)
  }

  if (animationName == "sound") {
    soundColor1      = hexToColor(String(doc["color1"] | "#0050FF"));  // VU bottom
    soundColor2      = hexToColor(String(doc["color2"] | "#FF00A0"));  // VU top
    soundSensitivity = constrain((float)(int)(doc["sensitivity"] | 5), 0.0f, 10.0f);
    soundBaseline    = 1.0f;   // re-track from rest
    soundEnergy      = 0.0f;
    soundPeak        = 0.0f;
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
    weatherNeedsFetch = true;   // loop() fetches on the next frame (off the request/boot path)
    weatherFetchOk    = false;  // new config → fast (30s) retry until THIS config's first success, not 10 min
  }

  if (animationName == "weather2") {
    weatherZip    = String(doc["zipcode"] | "85013");
    weather2Unit  = String(doc["units"]   | "F");
    const char* c1 = doc["color1"] | "#FFA500";
    const char* c2 = doc["color2"] | "#FFDC50";
    weather2Color1 = hexToColor(String(c1));
    weather2Color2 = hexToColor(String(c2));
    weatherNeedsFetch = true;   // loop() fetches on the next frame (off the request/boot path)
    weatherFetchOk    = false;  // new config → fast (30s) retry until THIS config's first success, not 10 min
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

  if (animationName == "rainbow") {
    rainbowUsePalette = (bool)(doc["usePalette"] | false);
    if (rainbowUsePalette) {
      rainbowPalColors[0] = hexToColor(String(doc["color1"] | "#FF0000"));
      rainbowPalColors[1] = hexToColor(String(doc["color2"] | "#FFC800"));
      rainbowPalColors[2] = hexToColor(String(doc["color3"] | "#00C800"));
      rainbowPalColors[3] = hexToColor(String(doc["color4"] | "#0064FF"));
    }
  }

  if (animationName == "spiral") {
    const char* c1 = doc["color1"] | "#FF0000";
    const char* c2 = doc["color2"] | "#0000FF";
    spiralColor1 = hexToColor(String(c1));
    spiralColor2 = hexToColor(String(c2));
  }

  if (animationName == "dancefloor") {
    dfPalette = (uint8_t)constrain((int)(doc["palette"] | 0), 0, 63);
    dfHoldMin = (uint8_t)constrain((int)(doc["hold"]    | 12), 4, 40);
    dfInit    = false;
  }

  if (animationName == "starfield") {
    const char* c1 = doc["color1"] | "#FFFFFF";
    const char* c2 = doc["color2"] | "#0064FF";
    starColor1  = hexToColor(String(c1));
    starColor2  = hexToColor(String(c2));
    starDensity = constrain((int)(doc["density"] | 8), 1, 16);
    starInward  = (bool)(doc["inward"] | false);
    starsInitialized = false;
  }

  if (animationName == "fireworks") {
    const char* c1 = doc["color1"] | "#FF3200";
    const char* c2 = doc["color2"] | "#FFC800";
    const char* c3 = doc["color3"] | "#0064FF";
    fwColor1 = hexToColor(String(c1));
    fwColor2 = hexToColor(String(c2));
    fwColor3 = hexToColor(String(c3));
  }

  if (animationName == "fireworks2") {
    const char* c1 = doc["color1"] | "#FF3200";
    const char* c2 = doc["color2"] | "#FFC800";
    const char* c3 = doc["color3"] | "#0064FF";
    fw2Color1 = hexToColor(String(c1));
    fw2Color2 = hexToColor(String(c2));
    fw2Color3 = hexToColor(String(c3));
    fw2Phase       = FW_IDLE;
    fw2IdleStartMs = 0;
  }

  if (animationName == "wave") {
    const char* c1 = doc["color1"] | "#0000FF";
    const char* c2 = doc["color2"] | "#000028";
    waveColor1 = hexToColor(String(c1));
    waveColor2 = hexToColor(String(c2));
  }

  if (animationName == "comet") {
    const char* c1 = doc["color1"] | "#FFC832";
    const char* c2 = doc["color2"] | "#FF6400";
    const char* c3 = doc["color3"] | "#C83200";
    const char* c4 = doc["color4"] | "#500A00";
    cometColor1 = hexToColor(String(c1));
    cometColor2 = hexToColor(String(c2));
    cometColor3 = hexToColor(String(c3));
    cometColor4 = hexToColor(String(c4));
  }

  if (animationName == "sun") {
    const char* c1 = doc["color1"] | "#FFB700";
    const char* c2 = doc["color2"] | "#FF6600";
    const char* c3 = doc["color3"] | "#FF3300";
    const char* c4 = doc["color4"] | "#CC1100";
    const char* c5 = doc["color5"] | "#880000";
    sunColor1 = hexToColor(String(c1));
    sunColor2 = hexToColor(String(c2));
    sunColor3 = hexToColor(String(c3));
    sunColor4 = hexToColor(String(c4));
    sunColor5 = hexToColor(String(c5));
    sunDiscBri = (uint8_t)constrain((int)(doc["discBri"] | 78) * 255 / 100, 0, 255);
    sunRingBri = (uint8_t)constrain((int)(doc["ringBri"] | 78) * 255 / 100, 0, 255);
  }

  if (animationName == "frostbite") {
    const char* c = doc["color"] | "#DCE6FF";
    fbColor     = hexToColor(String(c));
    fbSparkRate = (uint8_t)constrain((int)(doc["sparkle"] | 20), 0, 100);
    fbMistMax   = (uint8_t)constrain((int)(doc["mist"]    | 40) * 2, 8, 210);
    fbInit      = false;
  }

  animationActive = true;
  return true;
}

// POST /api/display/animation — HTTP wrapper: apply, persist for auto-resume, respond.
void handleAnimation() {
  String body = server.arg("plain");
  if (!applyAnimationBody(body)) { sendJson(400, "{\"error\":\"Invalid JSON or unknown animation type\"}"); return; }
  // Debounced auto-resume (flushed from loop() after ~8s) — see esp32_matrix_webserver.ino.
  resumeKind = "anim"; resumeBody = body; resumeDirty = true; resumeDirtyMs = millis();
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

// POST /api/display/frames — Claude's expression channel (frame-sequence player).
// Body: { "frames": ["<384 hex chars = RRGGBB × 64 px, row-major>", ...],
//         "frame_ms": 30-5000 (default 150), "loop": 0-1000 (0 = forever,
//         N = play N passes then hold the last frame) }
// Transient by design — never persisted for auto-resume (an expression is a
// moment, not a mode). Validates EVERYTHING before touching playback state so
// a malformed upload can't corrupt whatever is currently showing.
static uint8_t framesHexNib(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}
void handleFrames() {
  // This is our largest payload (up to MAX_PLAY_FRAMES × 384 hex chars ≈ 9KB),
  // and parsing it briefly allocates a copy of the body plus the JSON document.
  // On a tight heap that transient spike can trip loop()'s low-heap auto-restart
  // (< 14000) and freeze/reboot the board. Bail out gracefully instead. With
  // PSRAM enabled there's plenty of headroom; this only bites if PSRAM is off.
  if (ESP.getFreeHeap() < 30000) {
    sendJson(503, "{\"error\":\"low memory, retry shortly\"}");
    return;
  }

  // Parse ZERO-COPY: hold the body in a named buffer and let ArduinoJson store
  // pointers into it instead of duplicating every hex string into the document.
  // That roughly halves the transient allocation. `body` must outlive all use of
  // `doc` (it does — both are function-scoped and decoding finishes below).
  String body = server.arg("plain");
  JsonDocument doc;
  if (deserializeJson(doc, (char*)body.c_str(), body.length()) != DeserializationError::Ok) {
    sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }
  JsonArray arr = doc["frames"];
  if (arr.isNull() || arr.size() < 1 || arr.size() > MAX_PLAY_FRAMES) {
    sendJson(400, "{\"error\":\"frames must be an array of 1-" + String(MAX_PLAY_FRAMES) + " strings\"}");
    return;
  }
  // Pass 1: validate every frame before committing anything.
  for (JsonVariant v : arr) {
    const char* hex = v.as<const char*>();
    if (!hex || strlen(hex) != 384) {
      sendJson(400, "{\"error\":\"each frame must be exactly 384 hex chars (RRGGBB x 64)\"}");
      return;
    }
  }
  // Pass 2: decode into the playback buffer.
  int n = 0;
  for (JsonVariant v : arr) {
    const char* hex = v.as<const char*>();
    for (int i = 0; i < 64; i++) {
      const char* p = hex + i * 6;
      framesBuf[n * 64 + i] = CRGB(
        (framesHexNib(p[0]) << 4) | framesHexNib(p[1]),
        (framesHexNib(p[2]) << 4) | framesHexNib(p[3]),
        (framesHexNib(p[4]) << 4) | framesHexNib(p[5]));
    }
    n++;
  }

  stopAll();
  framesCount    = (uint8_t)n;
  framesLoops    = (uint16_t)constrain((int)(doc["loop"] | 0), 0, 1000);
  framesPlayed   = 0;
  framesIdx      = 0;
  animationName  = "frames";
  animationSpeed = (uint32_t)constrain((int)(doc["frame_ms"] | 150), 30, 5000);
  animationActive = true;

  stepFramesFrame();   // show the first frame immediately
  FastLED.show();
  sendJson(200, "{\"status\":\"ok\",\"frames\":" + String(n) + "}");
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
  String json = "{\"zip\":\""    + escapeJson(weatherZip) + "\""
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
// GET /api/display/framebuffer
// Returns the current 8×8 framebuffer as 64 "RRGGBB" hex strings, row-major
// (index = y*8 + x). Lets any web page show an exact, always-accurate live preview
// of whatever the board is actually displaying — every animation and every text
// style — by polling, instead of re-implementing the firmware's rendering in JS.
// Values are the raw leds[] colors (pre-brightness); the page applies its own
// ledsim brightness model, matching how the other previews render.
void handleFramebuffer() {
  String json;
  json.reserve(NUM_LEDS * 10 + 16);
  json = "{\"px\":[";
  char hex[10];
  for (int i = 0; i < NUM_LEDS; i++) {
    snprintf(hex, sizeof(hex), "\"%02X%02X%02X\"", leds[i].r, leds[i].g, leds[i].b);
    json += hex;
    if (i < NUM_LEDS - 1) json += ',';
  }
  json += "]}";
  sendJson(200, json);
}

//     (timer has remaining/total seconds, weather has zip/data mode/units,
//      clock has timezone and NTP sync status)
//   - If idle: state = "idle"
// This is what the MCP server's matrix_status tool calls.
void handleStatus() {
  String json = "{";
  // Version certainty: fw_version is the flashed firmware (from version.h);
  // fw_built is the compiler's automatic build timestamp (updates every reflash
  // even without a version bump); web_version is the uploaded LittleFS bundle.
  // Compare these against the repo /VERSION via `npm run check` / matrix_version.
  json += "\"fw_version\":\""  + String(FW_VERSION) + "\"";
  json += ",\"fw_built\":\""   + String(__DATE__ " " __TIME__) + "\"";
  json += ",\"web_version\":\"" + escapeJson(webVersion) + "\"";
  json += ",\"brightness\":" + String(brightness);

  if (textActive) {
    json += ",\"state\":\"text\"";
    json += ",\"text\":\"" + escapeJson(scrollText) + "\"";
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
      json += ",\"zip\":\""       + escapeJson(weatherZip) + "\"";
      json += ",\"data_mode\":\"" + weatherDataMode + "\"";
      json += ",\"units\":\""     + weatherUnit     + "\"";
    }
    if (animationName == "clock" || animationName == "calendar") {
      // Report whichever timezone config is actually active: the DST-aware POSIX
      // string (tz) when one was given, else the fixed integer offset.
      if (clockTZ.length() > 0) json += ",\"tz\":\"" + escapeJson(clockTZ) + "\"";
      else                      json += ",\"timezone\":" + String(clockTimezone);
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

// GET /api/presence — current semantic status, served verbatim.
void handlePresenceGet() {
  sendJson(200, presenceJson);
}

// POST /api/presence — replace the stored status. Body is a normalized
// PresenceMessage (validated by the MCP server); the board does a minimal
// defensive check (intent present) and stamps ts with its NTP clock.
void handlePresencePost() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }
  if (!doc["intent"].is<const char*>() || String((const char*)doc["intent"]).length() == 0) {
    sendJson(400, "{\"error\":\"intent (non-empty string) required\"}");
    return;
  }
  doc["ts"] = (uint32_t)time(nullptr);   // epoch seconds; card formats to local
  presenceJson = "";
  serializeJson(doc, presenceJson);
  sendJson(200, "{\"status\":\"ok\"}");
}

// POST /api/grid-test/set — body: {"mode": "color"|"brightness", "brightness": 0-255}
//
// Diagnostic app. Two static test patterns to calibrate what the board can display.
//
// "color" mode: fills 64 pixels with R = (linear_index + 1) * 4, capped at 255.
//   Pixel [row, col] (1-indexed, left=1, top=1):
//     linear_index = (row-1)*8 + (col-1)
//     [1,1]=R4  [1,2]=R8 ... [1,8]=R32
//     [2,1]=R36 ...          [2,8]=R64
//     ...
//     [8,8]=R255
//   Run at full brightness (255) to find the minimum R value that lights the LED.
//
// "brightness" mode: all 64 pixels = (255,0,0). Drag the brightness slider to find
//   the global brightness threshold below which LEDs go dark.
//
// Static display — no animation loop. Changing brightness via /api/brightness
// re-renders whatever is already in leds[] at the new brightness level.
void handleGridTest() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }

  stopAll();

  gridTestMode       = String(doc["mode"] | "color");
  gridTestBrightness = (uint8_t)constrain((int)(doc["brightness"] | 255), 0, 255);
  brightness         = gridTestBrightness;

  FastLED.setBrightness(brightness);

  if (gridTestMode == "color") {
    for (int i = 0; i < NUM_LEDS; i++) {
      uint8_t r = (uint8_t)constrain((i + 1) * 4, 0, 255);
      leds[i] = CRGB(r, 0, 0);
    }
  } else {
    fill_solid(leds, NUM_LEDS, CRGB(255, 0, 0));
  }

  FastLED.show();
  sendJson(200, "{\"status\":\"ok\",\"mode\":\"" + gridTestMode + "\",\"brightness\":" + String(brightness) + "}");
}
