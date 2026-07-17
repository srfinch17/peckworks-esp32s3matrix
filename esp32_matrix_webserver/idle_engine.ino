// idle_engine.ino — board-side "dead-man's switch" screensaver.
// Armed by Claude's Stop hook (POST /api/idle/arm). While the host goof-watcher
// keeps pushing frames, idleNoteActivity(true) resets the timer WITHOUT disarming,
// so the board stays out of the way. When the host falls silent (cap reached or
// laptop sleeps), the timer expires and the board enters a low-brightness rotation
// of enabled screensaver apps. Any real command (idleNoteActivity(false)) disarms.

static bool     idleArmed         = false;  // Claude signaled idle; eligible to screensaver
static bool     screensaverOn     = false;  // currently rotating
static uint32_t idleLastActivityMs= 0;      // last command received (idle or not)
static uint32_t idleNextPickMs    = 0;      // when to pick the next app
static String   idleLastPick      = "";     // avoid immediate repeats

// Split the enabled-apps CSV into a temp list and pick one at random (no repeat).
static String idlePickType() {
  // Count tokens.
  String csv = settings.idleApps; if (csv.length() == 0) return "";
  // Collect into a small fixed array (rotation universe is tiny).
  String types[16]; int n = 0;
  int start = 0;
  while (start < (int)csv.length() && n < 16) {
    int comma = csv.indexOf(',', start);
    if (comma < 0) comma = csv.length();
    String t = csv.substring(start, comma); t.trim();
    if (t.length()) types[n++] = t;
    start = comma + 1;
  }
  if (n == 0) return "";
  if (n == 1) return types[0];
  // Pick, avoiding an immediate repeat.
  String pick;
  for (int tries = 0; tries < 8; tries++) {
    pick = types[random(n)];
    if (pick != idleLastPick) break;
  }
  return pick;
}

// RANDOM-OFF launch params mirroring mcp_server/idle.ts IDLE_APPS, so an app looks
// the SAME in the screensaver as via the on-demand matrix_idle tool. Keep aligned
// with idle.ts (random-ON uses idleRandomParamsFor below and intentionally diverges).
// Returns the params object body (without the leading "{" / type).
static String idleParamsFor(const String& type) {
  if (type == "fire")        return ",\"speed\":50,\"intensity\":70";
  if (type == "dancefloor")  return ",\"palette\":0,\"hold\":6";
  if (type == "fireworks")   return ",\"color1\":\"#ff0050\",\"color2\":\"#00e0ff\",\"color3\":\"#ffd000\"";
  if (type == "frostbite")   return ",\"color\":\"#66ccff\",\"sparkle\":5,\"mist\":4";
  if (type == "matrix_rain") return ",\"theme\":\"classic\",\"speed\":60";
  if (type == "snow")        return ",\"speed\":110";
  if (type == "clock") {
    String p = ",\"color1\":\"#00ff88\",\"color2\":\"#0088ff\",\"color3\":\"#ff4040\"";
    if (settings.tz.length()) p += ",\"tz\":\"" + settings.tz + "\"";  // honor the tz setting
    return p;
  }
  return "";
}

// Format a hue as "#RRGGBB" at full saturation. val=255 for full brightness;
// lower val for a deliberately dim variant (wave trough). Full-sat/full-val
// hues stay visible at screensaver brightness 6-8; dim or pastel rolls
// would round to black there. (No default arg: .ino auto-prototypes choke.)
static String idleHueHex(uint8_t hue, uint8_t val) {
  CRGB c = CHSV(hue, 255, val);
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
  return String(buf);
}

// Random launch params, one fresh roll per launch (settings.idleRandom on).
// Multi-color apps do NOT roll hues independently: h1 is a random base and
// h2/h3 are spread around the wheel with jitter, so rolls never smear into
// one hue. uint8_t arithmetic wraps the color wheel naturally.
static String idleRandomParamsFor(const String& type) {
  uint8_t h1 = (uint8_t)random(256);
  uint8_t h2 = (uint8_t)(h1 + 70 + random(31));    // roughly a third around
  uint8_t h3 = (uint8_t)(h1 + 155 + random(31));   // roughly two thirds around
  if (type == "fire") {
    static const char* FIRE_PALETTES[4] = {"classic", "blue", "green", "purple"};
    return ",\"palette\":\"" + String(FIRE_PALETTES[random(4)]) + "\""
           ",\"intensity\":" + String(4 + random(7)) +    // 4-10
           ",\"sparks\":"    + String(random(11)) +       // 0-10
           ",\"tendrils\":"  + String(random(11)) +       // 0-10
           ",\"speed\":"     + String(30 + random(61));   // 30-90 ms/frame
  }
  if (type == "matrix_rain") {
    static const char* RAIN_THEMES[4] = {"classic", "blue", "red", "purple"};
    return ",\"theme\":\"" + String(RAIN_THEMES[random(4)]) + "\""
           ",\"speed\":"   + String(40 + random(51));     // 40-90
  }
  if (type == "snow") {
    // Non-confetti already rolls its own flake hue in anim_snow's launch code.
    String p = ",\"speed\":" + String(80 + random(61));   // 80-140
    if (random(2)) p += ",\"confetti\":true";
    return p;
  }
  if (type == "fireworks" || type == "fireworks2") {
    return ",\"color1\":\"" + idleHueHex(h1, 255) + "\""
           ",\"color2\":\"" + idleHueHex(h2, 255) + "\""
           ",\"color3\":\"" + idleHueHex(h3, 255) + "\"";
  }
  if (type == "frostbite") {
    return ",\"color\":\""  + idleHueHex(h1, 255) + "\""
           ",\"sparkle\":"  + String(5 + random(36)) +    // 5-40
           ",\"mist\":"     + String(2 + random(7));      // 2-8 (subtle, idle character)
  }
  if (type == "dancefloor") {
    return ",\"palette\":" + String(random(64)) +         // 0-63
           ",\"hold\":"    + String(4 + random(9));       // 4-12
  }
  if (type == "spiral") {
    return ",\"color1\":\"" + idleHueHex(h1, 255) + "\""
           ",\"color2\":\"" + idleHueHex(h3, 255) + "\"";
  }
  if (type == "wave") {
    // Crest + dim same-hue trough so it still reads as water, not two colors.
    // Trough val 90 (not lower): at screensaver brightness 6-8 a channel needs
    // ~37+ to light at all, and val 40 goes black for many hues.
    return ",\"color1\":\"" + idleHueHex(h1, 255) + "\""
           ",\"color2\":\"" + idleHueHex(h1, 90) + "\"";
  }
  if (type == "starfield") {
    String p = ",\"color1\":\"" + idleHueHex(h1, 255) + "\""
               ",\"color2\":\"" + idleHueHex(h3, 255) + "\""
               ",\"density\":"  + String(4 + random(9));  // 4-12
    if (random(2)) p += ",\"inward\":true";
    return p;
  }
  if (type == "rainbow") {
    // Coin-flip: classic wheel, or a 4-color palette on exact wheel quarters.
    if (random(2) == 0) return "";
    return ",\"usePalette\":true"
           ",\"color1\":\"" + idleHueHex(h1, 255) + "\""
           ",\"color2\":\"" + idleHueHex((uint8_t)(h1 + 64), 255) + "\""
           ",\"color3\":\"" + idleHueHex((uint8_t)(h1 + 128), 255) + "\""
           ",\"color4\":\"" + idleHueHex((uint8_t)(h1 + 192), 255) + "\"";
  }
  if (type == "clock") {
    String p = ",\"color1\":\"" + idleHueHex(h1, 255) + "\""    // hours
               ",\"color2\":\"" + idleHueHex(h2, 255) + "\""    // minutes
               ",\"color3\":\"" + idleHueHex(h3, 255) + "\"";   // colon
    if (settings.tz.length()) p += ",\"tz\":\"" + settings.tz + "\"";
    return p;
  }
  if (type == "claudesweep") {
    return ",\"color\":\"" + idleHueHex(h1, 255) + "\"";
  }
  // Unknown app in a stored CSV: launch with API defaults (fail-safe).
  return "";
}

static void idleLaunch(const String& type) {
  idleLastPick = type;
  // Launch via the shared animation path (does NOT set brightness or touch auto-resume).
  if (settings.idleRandom) {
    // Roll brightness 6-8; frostbite 7-8 (its mist wash needs the extra step to read).
    uint8_t bri = (type == "frostbite") ? (uint8_t)(7 + random(2)) : (uint8_t)(6 + random(3));
    FastLED.setBrightness(bri);
    applyAnimationBody("{\"type\":\"" + type + "\"" + idleRandomParamsFor(type) + "}");
  } else {
    FastLED.setBrightness(settings.idleBri);
    applyAnimationBody("{\"type\":\"" + type + "\"" + idleParamsFor(type) + "}");
  }
}

void idleArm() {
  if (!settings.idleOn) return;
  idleArmed = true;
  idleLastActivityMs = millis();   // start counting from now
}

// Called by every received display command. isIdleContent=true for the host
// goof/Zz pushes (keep armed, just reset timer); false for real user/Claude actions.
void idleNoteActivity(bool isIdleContent) {
  idleLastActivityMs = millis();
  if (isIdleContent) return;
  idleArmed = false;
  if (screensaverOn) {
    screensaverOn = false;             // a real command takes over...
    FastLED.setBrightness(brightness); // ...restore live brightness (screensaver had dimmed to idleBri or a rolled 6-8)
  }
}

void idleTick() {
  if (!settings.idleOn) { screensaverOn = false; return; }
  uint32_t now = millis();
  if (!screensaverOn) {
    if (idleArmed && (now - idleLastActivityMs) > (uint32_t)settings.idleAfterS * 1000UL) {
      screensaverOn = true;
      idleNextPickMs = now;  // pick immediately (subtraction form: (now - now) == 0)
    }
  }
  if (screensaverOn && (now - idleNextPickMs) < 0x80000000UL) {
    String t = idlePickType();
    if (t.length()) idleLaunch(t);
    idleNextPickMs = now + (uint32_t)settings.idleRotS * 1000UL;
  }
}
