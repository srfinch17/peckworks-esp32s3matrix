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

// The `struct Settings` and the `Settings settings;` global are defined in the MAIN
// ino (esp32_matrix_webserver.ino) so they are visible to setup() and all files.
// This file holds only the logic. "default brightness" is unified with the existing
// live/auto-resume brightness (NVS "bri" key) — see settingsToJson/applySettingsJson.

// The rotation universe. Random-OFF mode keeps the historical tuned params in
// idleParamsFor (idle_engine.ino); random-ON rolls params per launch.
static const char* IDLE_APPS_DEFAULT =
  "fire,matrix_rain,clock,fireworks,fireworks2,frostbite,snow,dancefloor,spiral,wave,starfield,rainbow";

void loadSettings() {
  // Per-key defaulting: read if present, else write the default. isKey() avoids
  // the harmless NOT_FOUND log noise on a fresh NVS (same pattern as auto-resume).
  settings.idleOn     = prefs.isKey("idle_on")   ? prefs.getBool("idle_on", true)            : (prefs.putBool("idle_on", true), true);
  settings.idleApps   = prefs.isKey("idle_apps") ? prefs.getString("idle_apps", IDLE_APPS_DEFAULT) : (prefs.putString("idle_apps", IDLE_APPS_DEFAULT), String(IDLE_APPS_DEFAULT));
  settings.idleAfterS = prefs.isKey("idle_after")? prefs.getUInt("idle_after", 120)          : (prefs.putUInt("idle_after", 120), 120);
  settings.idleRotS   = prefs.isKey("idle_rot")  ? prefs.getUInt("idle_rot", 240)            : (prefs.putUInt("idle_rot", 240), 240);
  settings.idleBri    = prefs.isKey("idle_bri")  ? prefs.getUChar("idle_bri", 5)             : (prefs.putUChar("idle_bri", 5), 5);
  settings.idleRandom = prefs.isKey("idle_rand") ? prefs.getBool("idle_rand", true)          : (prefs.putBool("idle_rand", true), true);
  settings.bootAnim   = prefs.isKey("boot_anim") ? prefs.getString("boot_anim", "")          : (prefs.putString("boot_anim", ""), String(""));
  settings.tz         = prefs.isKey("tz")        ? prefs.getString("tz", "")                 : (prefs.putString("tz", ""), String(""));
  settings.calibCorrection = prefs.isKey("calib_corr") ? prefs.getBool("calib_corr", true)   : (prefs.putBool("calib_corr", true), true);
  settings.mqttOn     = prefs.isKey("mqtt_on")   ? prefs.getBool("mqtt_on", false)           : (prefs.putBool("mqtt_on", false), false);
  settings.mqttHost   = prefs.isKey("mqtt_host") ? prefs.getString("mqtt_host", "")          : (prefs.putString("mqtt_host", ""), String(""));
  settings.mqttPort   = prefs.isKey("mqtt_port") ? prefs.getUShort("mqtt_port", 1883)        : (prefs.putUShort("mqtt_port", 1883), (uint16_t)1883);
  settings.mqttEveryS = prefs.isKey("mqtt_every")? prefs.getUInt("mqtt_every", 3)            : (prefs.putUInt("mqtt_every", 3), (uint32_t)3);

  uint16_t stored = prefs.getUShort("set_ver", 0);
  if (stored != SETTINGS_VERSION) {
    // v1: no migration needed — just stamp. Future breaking changes branch here.
    prefs.putUShort("set_ver", SETTINGS_VERSION);
  }
  Serial.printf("Settings loaded: idleOn=%d after=%us rot=%us idleBri=%u random=%d apps=%s\n",
                settings.idleOn, settings.idleAfterS, settings.idleRotS,
                settings.idleBri, settings.idleRandom, settings.idleApps.c_str());
}

void saveSettings() {
  prefs.putBool("idle_on", settings.idleOn);
  prefs.putString("idle_apps", settings.idleApps);
  prefs.putUInt("idle_after", settings.idleAfterS);
  prefs.putUInt("idle_rot", settings.idleRotS);
  prefs.putUChar("idle_bri", settings.idleBri);
  prefs.putBool("idle_rand", settings.idleRandom);
  prefs.putString("boot_anim", settings.bootAnim);
  prefs.putString("tz", settings.tz);
  prefs.putBool("calib_corr", settings.calibCorrection);
  prefs.putBool("mqtt_on", settings.mqttOn);
  prefs.putString("mqtt_host", settings.mqttHost);
  prefs.putUShort("mqtt_port", settings.mqttPort);
  prefs.putUInt("mqtt_every", settings.mqttEveryS);
}

String settingsToJson() {
  String j = "{";
  j += "\"settings_version\":" + String(SETTINGS_VERSION);
  j += ",\"idle_enabled\":"   + String(settings.idleOn ? "true" : "false");
  j += ",\"idle_apps\":\""    + escapeJson(settings.idleApps) + "\"";
  j += ",\"idle_after_secs\":" + String(settings.idleAfterS);
  j += ",\"idle_rotate_secs\":" + String(settings.idleRotS);
  j += ",\"idle_brightness\":"  + String(settings.idleBri);
  j += ",\"idle_random\":"      + String(settings.idleRandom ? "true" : "false");
  // default_brightness is the SAME value as the live/auto-resume brightness — no
  // separate stored key, so it can never go inert. (resumeBri is the last
  // user-committed brightness, which is what auto-resume restores on boot.)
  j += ",\"default_brightness\":" + String(resumeBri);
  j += ",\"boot_animation\":\"" + escapeJson(settings.bootAnim) + "\"";
  j += ",\"timezone\":\""      + escapeJson(settings.tz) + "\"";
  j += ",\"calibration_correction\":" + String(settings.calibCorrection ? "true" : "false");
  j += ",\"mqtt_enabled\":"    + String(settings.mqttOn ? "true" : "false");
  j += ",\"mqtt_host\":\""     + escapeJson(settings.mqttHost) + "\"";
  j += ",\"mqtt_port\":"       + String(settings.mqttPort);
  j += ",\"mqtt_every_secs\":" + String(settings.mqttEveryS);
  j += "}";
  return j;
}

// Partial update: only keys present in the body change. Returns false on bad JSON.
bool applySettingsJson(const String& body) {
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) return false;
  if (!doc["idle_enabled"].isNull())     settings.idleOn     = doc["idle_enabled"].as<bool>();
  if (!doc["idle_apps"].isNull()) {
    String v = String((const char*)(doc["idle_apps"] | settings.idleApps.c_str()));
    if (v.length() <= 512) settings.idleApps = v;   // cap: NVS entry limit; keeps idlePickType cheap
  }
  if (!doc["idle_after_secs"].isNull())  settings.idleAfterS = constrain((long)(doc["idle_after_secs"] | (long)settings.idleAfterS), 5L, 3600L);
  if (!doc["idle_rotate_secs"].isNull()) settings.idleRotS   = constrain((long)(doc["idle_rotate_secs"] | (long)settings.idleRotS), 10L, 3600L);
  if (!doc["idle_brightness"].isNull())  settings.idleBri    = constrain((int)(doc["idle_brightness"] | settings.idleBri), 1, 255);
  if (!doc["idle_random"].isNull())      settings.idleRandom = doc["idle_random"].as<bool>();
  if (!doc["boot_animation"].isNull())   settings.bootAnim   = String((const char*)(doc["boot_animation"] | settings.bootAnim.c_str()));
  if (!doc["timezone"].isNull()) {
    settings.tz = String((const char*)(doc["timezone"] | settings.tz.c_str()));
    // Live-apply: update the running NTP client so the clock reflects the new TZ
    // immediately without restarting the animation (matches clock/calendar handler pattern).
    if (settings.tz.length()) {
      clockTZ = settings.tz;
      configTzTime(clockTZ.c_str(), "pool.ntp.org", "time.nist.gov");
      ntpStarted = true;   // SNTP started here; keep the MQTT publisher from kicking UTC over it
    }
  }
  if (!doc["calibration_correction"].isNull()) settings.calibCorrection = doc["calibration_correction"].as<bool>();
  if (!doc["mqtt_enabled"].isNull())    settings.mqttOn     = doc["mqtt_enabled"].as<bool>();
  if (!doc["mqtt_host"].isNull())       settings.mqttHost   = String((const char*)(doc["mqtt_host"] | settings.mqttHost.c_str()));
  if (!doc["mqtt_port"].isNull())       settings.mqttPort   = (uint16_t)constrain((long)(doc["mqtt_port"] | (long)settings.mqttPort), 1L, 65535L);
  if (!doc["mqtt_every_secs"].isNull()) settings.mqttEveryS = (uint32_t)constrain((long)(doc["mqtt_every_secs"] | (long)settings.mqttEveryS), 1L, 3600L);
  saveSettings();
  mqttApplySettings();   // mqtt_publisher.ino: re-point / reconnect if the broker config changed
  // default_brightness: unified with the live brightness. Apply immediately AND
  // persist via the existing auto-resume "bri" key, so it is identical to the
  // value the board restores on boot — never an inert parallel setting.
  if (!doc["default_brightness"].isNull()) {
    int b = constrain((int)(doc["default_brightness"] | resumeBri), 0, 255);
    brightness = (uint8_t)b; resumeBri = brightness;
    FastLED.setBrightness(brightness); matrixShow();
    prefs.putUChar("bri", resumeBri);
  }
  return true;
}
