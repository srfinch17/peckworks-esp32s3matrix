// settings.ino — persistent board settings (NVS namespace "matrix").
// Merge-on-boot: an upgraded flash keeps existing user values and only fills in
// newly-added keys with their defaults. NVS survives a normal Sketch upload, so
// settings persist across flashes; only a full chip-erase wipes them.

// SETTINGS_VERSION is #defined in the MAIN ino (esp32_matrix_webserver.ino),
// NOT here — because handleStatus (in api_handlers.ino) references it, and that
// file concatenates BEFORE settings.ino in the Arduino build. A `static` const
// defined here would be invisible to that earlier reference (hard compile error).
// See Task 1 Step 2. Bump ONLY for a deliberate breaking change that must reset
// users to defaults; normal additions are pure merges and need no bump.

struct Settings {
  bool     idleOn;        // master switch for the screensaver engine
  String   idleApps;      // CSV of enabled screensaver type names
  uint32_t idleAfterS;    // seconds of board silence before screensaver starts
  uint32_t idleRotS;      // seconds between screensaver re-picks
  uint8_t  idleBri;       // brightness during the screensaver
  String   bootAnim;      // pinned boot animation type ("" = auto-resume)
  String   tz;            // POSIX TZ for the clock ("" = none)
};
// NOTE: there is deliberately NO separate "default brightness" field. It is
// unified with the existing live/auto-resume brightness (the NVS "bri" key) so
// it can never become an inert parallel value — see settingsToJson/applySettingsJson.

// The rotation universe (mirrors mcp_server/idle.ts IDLE_APPS). Keep aligned.
static const char* IDLE_APPS_DEFAULT =
  "fire,matrix_rain,clock,fireworks,frostbite,snow,dancefloor";

Settings settings;

void loadSettings() {
  // Per-key defaulting: read if present, else write the default. isKey() avoids
  // the harmless NOT_FOUND log noise on a fresh NVS (same pattern as auto-resume).
  settings.idleOn     = prefs.isKey("idle_on")   ? prefs.getBool("idle_on", true)            : (prefs.putBool("idle_on", true), true);
  settings.idleApps   = prefs.isKey("idle_apps") ? prefs.getString("idle_apps", IDLE_APPS_DEFAULT) : (prefs.putString("idle_apps", IDLE_APPS_DEFAULT), String(IDLE_APPS_DEFAULT));
  settings.idleAfterS = prefs.isKey("idle_after")? prefs.getUInt("idle_after", 120)          : (prefs.putUInt("idle_after", 120), 120);
  settings.idleRotS   = prefs.isKey("idle_rot")  ? prefs.getUInt("idle_rot", 240)            : (prefs.putUInt("idle_rot", 240), 240);
  settings.idleBri    = prefs.isKey("idle_bri")  ? prefs.getUChar("idle_bri", 5)             : (prefs.putUChar("idle_bri", 5), 5);
  settings.bootAnim   = prefs.isKey("boot_anim") ? prefs.getString("boot_anim", "")          : (prefs.putString("boot_anim", ""), String(""));
  settings.tz         = prefs.isKey("tz")        ? prefs.getString("tz", "")                 : (prefs.putString("tz", ""), String(""));

  uint16_t stored = prefs.getUShort("set_ver", 0);
  if (stored != SETTINGS_VERSION) {
    // v1: no migration needed — just stamp. Future breaking changes branch here.
    prefs.putUShort("set_ver", SETTINGS_VERSION);
  }
  Serial.printf("Settings loaded: idleOn=%d after=%us rot=%us idleBri=%u apps=%s\n",
                settings.idleOn, settings.idleAfterS, settings.idleRotS,
                settings.idleBri, settings.idleApps.c_str());
}

void saveSettings() {
  prefs.putBool("idle_on", settings.idleOn);
  prefs.putString("idle_apps", settings.idleApps);
  prefs.putUInt("idle_after", settings.idleAfterS);
  prefs.putUInt("idle_rot", settings.idleRotS);
  prefs.putUChar("idle_bri", settings.idleBri);
  prefs.putString("boot_anim", settings.bootAnim);
  prefs.putString("tz", settings.tz);
}

String settingsToJson() {
  String j = "{";
  j += "\"settings_version\":" + String(SETTINGS_VERSION);
  j += ",\"idle_enabled\":"   + String(settings.idleOn ? "true" : "false");
  j += ",\"idle_apps\":\""    + escapeJson(settings.idleApps) + "\"";
  j += ",\"idle_after_secs\":" + String(settings.idleAfterS);
  j += ",\"idle_rotate_secs\":" + String(settings.idleRotS);
  j += ",\"idle_brightness\":"  + String(settings.idleBri);
  // default_brightness is the SAME value as the live/auto-resume brightness — no
  // separate stored key, so it can never go inert. (resumeBri is the last
  // user-committed brightness, which is what auto-resume restores on boot.)
  j += ",\"default_brightness\":" + String(resumeBri);
  j += ",\"boot_animation\":\"" + escapeJson(settings.bootAnim) + "\"";
  j += ",\"timezone\":\""      + escapeJson(settings.tz) + "\"";
  j += "}";
  return j;
}

// Partial update: only keys present in the body change. Returns false on bad JSON.
bool applySettingsJson(const String& body) {
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) return false;
  if (!doc["idle_enabled"].isNull())     settings.idleOn     = doc["idle_enabled"].as<bool>();
  if (!doc["idle_apps"].isNull())        settings.idleApps   = String((const char*)(doc["idle_apps"] | settings.idleApps.c_str()));
  if (!doc["idle_after_secs"].isNull())  settings.idleAfterS = constrain((long)(doc["idle_after_secs"] | (long)settings.idleAfterS), 5L, 3600L);
  if (!doc["idle_rotate_secs"].isNull()) settings.idleRotS   = constrain((long)(doc["idle_rotate_secs"] | (long)settings.idleRotS), 10L, 3600L);
  if (!doc["idle_brightness"].isNull())  settings.idleBri    = constrain((int)(doc["idle_brightness"] | settings.idleBri), 1, 255);
  if (!doc["boot_animation"].isNull())   settings.bootAnim   = String((const char*)(doc["boot_animation"] | settings.bootAnim.c_str()));
  if (!doc["timezone"].isNull()) {
    settings.tz = String((const char*)(doc["timezone"] | settings.tz.c_str()));
    // Task 4 wires tz live (calls configTzTime to apply to the running NTP client).
    // For now the stored value will take effect after the user restarts the clock animation.
  }
  saveSettings();
  // default_brightness: unified with the live brightness. Apply immediately AND
  // persist via the existing auto-resume "bri" key, so it is identical to the
  // value the board restores on boot — never an inert parallel setting.
  if (!doc["default_brightness"].isNull()) {
    int b = constrain((int)(doc["default_brightness"] | resumeBri), 0, 255);
    brightness = (uint8_t)b; resumeBri = brightness;
    FastLED.setBrightness(brightness); FastLED.show();
    prefs.putUChar("bri", resumeBri);
  }
  return true;
}
