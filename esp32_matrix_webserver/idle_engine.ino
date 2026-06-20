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
static uint8_t  briBeforeIdle     = 40;     // restore target if a real command interrupts

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

// Per-type launch params mirroring mcp_server/idle.ts IDLE_APPS, so an app looks
// the SAME in the screensaver as via the on-demand matrix_idle tool. Keep aligned
// with idle.ts. Returns the params object body (without the leading "{" / type).
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

static void idleLaunch(const String& type) {
  idleLastPick = type;
  FastLED.setBrightness(settings.idleBri);
  // Launch via the shared animation path with the app's tuned params.
  String body = "{\"type\":\"" + type + "\"" + idleParamsFor(type) + "}";
  applyAnimationBody(body);   // sets animationName, animationActive, etc. (does NOT set brightness or touch auto-resume)
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
    FastLED.setBrightness(brightness); // ...restore live brightness (screensaver had dimmed to idleBri)
  }
}

void idleTick() {
  if (!settings.idleOn) { screensaverOn = false; return; }
  uint32_t now = millis();
  if (!screensaverOn) {
    if (idleArmed && (now - idleLastActivityMs) > (uint32_t)settings.idleAfterS * 1000UL) {
      briBeforeIdle = brightness;
      screensaverOn = true;
      idleNextPickMs = 0;   // pick immediately
    }
  }
  if (screensaverOn && now >= idleNextPickMs) {
    String t = idlePickType();
    if (t.length()) idleLaunch(t);
    idleNextPickMs = now + (uint32_t)settings.idleRotS * 1000UL;
  }
}
