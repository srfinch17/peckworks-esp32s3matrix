# Idle Screensaver Random Settings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every idle screensaver launch rolls random params (colors, themes, speeds, brightness 6-8) behind a new `idle_random` setting (default on), with a new 12-app default rotation.

**Architecture:** All randomization lives in `idle_engine.ino` (spec Approach A). The idle engine already launches apps by building a JSON string and calling `applyAnimationBody()`; random mode just builds that string with random values. One new NVS-backed setting flows through the standard `settings.ino` plumbing and one new checkbox on `data/settings.html`.

**Tech Stack:** Arduino C++ (`.ino`, single translation unit), FastLED (`CHSV`/`CRGB`), vanilla JS settings page, NVS via `Preferences`.

**Spec:** `docs/superpowers/specs/2026-07-17-idle-random-settings-design.md`

## Global Constraints

- **No em-dashes or en-dashes** in any prose, comment, or commit message (house hard rule).
- **Claude cannot compile or flash.** Firmware "verification" inside tasks is careful self-review; the real gate is the hardware checkpoint (Task 5). Do not claim anything works until the user confirms on hardware.
- **Single translation unit:** all `.ino` files concatenate into one build. Shared state (the `Settings` struct) lives in the MAIN ino (`esp32_matrix_webserver.ino`). Do not use C++ default arguments on `.ino` free functions (the IDE's auto-generated prototypes can double-apply them).
- **Docs in the same change** (house merge gate): `docs/API.md` + `CLAUDE.md`.
- **Privacy:** never the maintainer's real name; "the user".
- New default rotation CSV, exact: `fire,matrix_rain,clock,fireworks,fireworks2,frostbite,snow,dancefloor,spiral,wave,starfield,rainbow`
- Brightness rolls: 6, 7, or 8; frostbite 7 or 8 only.
- Random colors are always full-saturation, full-value hues (`CHSV(h,255,255)`), never random RGB bytes (dim rolls round to black at brightness 6-8).

---

### Task 1: `idle_random` setting plumbing

**Files:**
- Modify: `esp32_matrix_webserver/esp32_matrix_webserver.ino:175-180` (Settings struct)
- Modify: `esp32_matrix_webserver/settings.ino` (default CSV, load/save/toJson/applyJson)

**Interfaces:**
- Produces: `settings.idleRandom` (bool, default true), NVS key `idle_rand`, JSON key `idle_random`, new `IDLE_APPS_DEFAULT` CSV. Task 2 reads `settings.idleRandom`; Task 3 reads/writes JSON `idle_random`.

- [ ] **Step 1: Add the struct field**

In `esp32_matrix_webserver.ino`, inside `struct Settings` (line ~175), after the `idleBri` member:

```cpp
  uint8_t  idleBri;       // brightness during the screensaver
  bool     idleRandom;    // roll random params + brightness 6-8 per screensaver launch
```

- [ ] **Step 2: New default rotation + comment in settings.ino**

Replace lines 18-20 of `settings.ino`:

```cpp
// The rotation universe (mirrors mcp_server/idle.ts IDLE_APPS). Keep aligned.
static const char* IDLE_APPS_DEFAULT =
  "fire,matrix_rain,clock,fireworks,frostbite,snow,dancefloor,claudesweep";
```

with:

```cpp
// The rotation universe. Random-OFF mode still mirrors mcp_server/idle.ts
// IDLE_APPS tuned params; random-ON intentionally diverges (rolled params).
static const char* IDLE_APPS_DEFAULT =
  "fire,matrix_rain,clock,fireworks,fireworks2,frostbite,snow,dancefloor,spiral,wave,starfield,rainbow";
```

- [ ] **Step 3: loadSettings / saveSettings**

In `loadSettings()`, after the `idleBri` line (line 29), add:

```cpp
  settings.idleRandom = prefs.isKey("idle_rand") ? prefs.getBool("idle_rand", true)          : (prefs.putBool("idle_rand", true), true);
```

In `saveSettings()`, after `prefs.putUChar("idle_bri", settings.idleBri);`:

```cpp
  prefs.putBool("idle_rand", settings.idleRandom);
```

Also extend the boot log line (lines 43-45) so the flag is visible on serial:

```cpp
  Serial.printf("Settings loaded: idleOn=%d after=%us rot=%us idleBri=%u random=%d apps=%s\n",
                settings.idleOn, settings.idleAfterS, settings.idleRotS,
                settings.idleBri, settings.idleRandom, settings.idleApps.c_str());
```

- [ ] **Step 4: settingsToJson / applySettingsJson**

In `settingsToJson()`, after the `idle_brightness` line (line 70):

```cpp
  j += ",\"idle_random\":"      + String(settings.idleRandom ? "true" : "false");
```

In `applySettingsJson()`, after the `idle_brightness` parse (line 94):

```cpp
  if (!doc["idle_random"].isNull())      settings.idleRandom = doc["idle_random"].as<bool>();
```

- [ ] **Step 5: Self-review the diff**

Check: struct field added in the MAIN ino (not settings.ino); NVS key is `idle_rand` (15-char NVS limit respected); every one of the four plumbing sites touched (load, save, toJson, applyJson); no em-dashes in comments.

- [ ] **Step 6: Commit**

```bash
git add esp32_matrix_webserver/esp32_matrix_webserver.ino esp32_matrix_webserver/settings.ino
git commit -m "feat(settings): idle_random flag + new 12-app default rotation"
```

---

### Task 2: Random launcher in the idle engine

**Files:**
- Modify: `esp32_matrix_webserver/idle_engine.ino`

**Interfaces:**
- Consumes: `settings.idleRandom` (Task 1), existing `applyAnimationBody(String)`, existing `idleParamsFor(String)` (unchanged), FastLED `CHSV`/`CRGB`.
- Produces: `static String idleHueHex(uint8_t hue, uint8_t val)`, `static String idleRandomParamsFor(const String& type)`, modified `idleLaunch()`. Nothing outside this file calls them.

- [ ] **Step 1: Update the file header comment**

Replace the comment on lines 39-41 (above `idleParamsFor`):

```cpp
// Per-type launch params mirroring mcp_server/idle.ts IDLE_APPS, so an app looks
// the SAME in the screensaver as via the on-demand matrix_idle tool. Keep aligned
// with idle.ts. Returns the params object body (without the leading "{" / type).
```

with:

```cpp
// RANDOM-OFF launch params mirroring mcp_server/idle.ts IDLE_APPS, so an app looks
// the SAME in the screensaver as via the on-demand matrix_idle tool. Keep aligned
// with idle.ts (random-ON uses idleRandomParamsFor below and intentionally diverges).
// Returns the params object body (without the leading "{" / type).
```

- [ ] **Step 2: Add the hue helper + random params builder**

Insert after the `idleParamsFor()` function (after line 55), before `idleLaunch()`:

```cpp
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
    return ",\"color1\":\"" + idleHueHex(h1, 255) + "\""
           ",\"color2\":\"" + idleHueHex(h1, 40) + "\"";
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
```

- [ ] **Step 3: Branch idleLaunch on the setting**

Replace the whole `idleLaunch()` (lines 57-63):

```cpp
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
```

- [ ] **Step 4: Self-review the diff**

Check, character by character (this is hand-built JSON, the classic slip is a missing `\"`):
- Every color value is wrapped as `\"...\"`; every numeric value is bare.
- Every app in the new default CSV has a branch: fire, matrix_rain, clock, fireworks, fireworks2, frostbite, snow, dancefloor, spiral, wave, starfield, rainbow. Plus claudesweep.
- `random(N)` upper bound is exclusive: `random(3)` is 0-2, so `6 + random(3)` is 6-8 and `7 + random(2)` is 7-8. `4 + random(7)` is 4-10.
- No default arguments on any function.
- `idleParamsFor()` itself is unchanged.

- [ ] **Step 5: Commit**

```bash
git add esp32_matrix_webserver/idle_engine.ino
git commit -m "feat(idle): random params + brightness roll per screensaver launch"
```

---

### Task 3: Settings page UI

**Files:**
- Modify: `esp32_matrix_webserver/data/settings.html`

**Interfaces:**
- Consumes: JSON keys `idle_random` (Task 1) via `GET/POST /api/settings`.
- Produces: checkbox `#idle_random`, checked by default on a fresh board (reflects board state); checking disables + dims the Idle brightness slider live.

- [ ] **Step 1: Add the checkbox row and tag the brightness subcard**

Replace lines 33-36:

```html
        <div class="subcard">
          <div class="row"><label>Idle brightness <output id="ibri_o"></output></label></div>
          <div class="row"><input type="range" id="idle_brightness" min="1" max="60"></div>
        </div>
```

with:

```html
        <div class="subcard">
          <div class="row"><label>Randomize settings</label><input type="checkbox" id="idle_random"></div>
        </div>
        <div class="subcard" id="ibri_card">
          <div class="row"><label>Idle brightness <output id="ibri_o"></output></label></div>
          <div class="row"><input type="range" id="idle_brightness" min="1" max="60"></div>
        </div>
```

- [ ] **Step 2: Update the APPS universe**

Replace line 99:

```js
const APPS=["fire","matrix_rain","clock","fireworks","frostbite","snow","dancefloor","claudesweep"];
```

with (claudesweep stays listed and selectable; it is just no longer in the board's default CSV, so it renders unchecked):

```js
const APPS=["fire","matrix_rain","clock","fireworks","fireworks2","frostbite","snow","dancefloor","spiral","wave","starfield","rainbow","claudesweep"];
```

- [ ] **Step 3: Wire load, live gray-out, and save**

After the `bindOut` helper (line 110), add:

```js
function syncRandomUI(){
  const r=$("idle_random").checked;
  $("idle_brightness").disabled=r;
  $("ibri_card").style.opacity=r?0.45:"";
}
```

In `load()`, after the `idle_brightness` line (line 116), add:

```js
  $("idle_random").checked=s.idle_random!==false;
```

and at the end of `load()` (after the `bindOut` calls, line 128), add:

```js
  $("idle_random").addEventListener("change",syncRandomUI);syncRandomUI();
```

In the submit body (line 134), extend the first line:

```js
    idle_enabled:$("idle_enabled").checked, idle_apps:apps, idle_random:$("idle_random").checked,
```

- [ ] **Step 4: Self-review the diff**

Check: `s.idle_random!==false` (not `!!s.idle_random`) so an old firmware that omits the key still shows checked (matching the new firmware default); `syncRandomUI` runs once at load, not only on change; the disabled slider still POSTs its value (harmless: the board ignores `idle_brightness` in random mode but keeps it stored for random-off).

- [ ] **Step 5: Commit**

```bash
git add esp32_matrix_webserver/data/settings.html
git commit -m "feat(settings-ui): randomize checkbox gates the idle brightness slider"
```

---

### Task 4: Docs + version bump

**Files:**
- Modify: `docs/API.md:48-52`
- Modify: `CLAUDE.md` (idle screensaver bullet + settings keys line)
- Version: `npm run bump:minor` (stamps VERSION, version.h, data/version.json and commits itself)

**Interfaces:**
- Consumes: names fixed in Tasks 1-3 (`idle_random`, brightness 6-8, frostbite 7-8, new default CSV).

- [ ] **Step 1: API.md settings keys**

Replace lines 48-52:

```markdown
- **`/api/settings` keys** (partial-merge; only sent keys change): `idle_enabled`, `idle_apps`,
  `idle_after_secs`, `idle_rotate_secs`, `idle_brightness`, `default_brightness`,
```

first two lines become:

```markdown
- **`/api/settings` keys** (partial-merge; only sent keys change): `idle_enabled`, `idle_apps`,
  `idle_after_secs`, `idle_rotate_secs`, `idle_brightness`, `idle_random` (bool, default true:
  each screensaver launch rolls random params and a random brightness 6-8, frostbite 7-8, and
  the `idle_brightness` value is ignored; false restores tuned params at `idle_brightness`),
  `default_brightness`,
```

(the rest of the bullet, `boot_animation` onward, is unchanged).

- [ ] **Step 2: CLAUDE.md idle bullet**

In the "API, settings, NVS, calibration" section, replace:

```markdown
- **Idle screensaver:** armed by `POST /api/idle/arm`; rotates `idle_apps` at
  `idle_brightness` after `idle_after_secs`.
```

with:

```markdown
- **Idle screensaver:** armed by `POST /api/idle/arm`; rotates `idle_apps` after
  `idle_after_secs`. `idle_random` on (default) rolls random params + brightness 6-8
  (frostbite 7-8) per launch; off runs tuned params at `idle_brightness`.
```

(The settings-keys line already says `idle_*`, which covers `idle_random`; no edit there.)

- [ ] **Step 3: Commit docs, then bump**

```bash
git add docs/API.md CLAUDE.md
git commit -m "docs: idle_random setting + random screensaver behavior"
npm run bump:minor
```

Expected: bump prints `Bumped 0.12.0 -> 0.13.0` (arrow rendering aside) and makes its own `chore: bump v0.13.0` commit.

- [ ] **Step 4: Run the drift check**

```bash
npm run check
```

Expected: repo-side artifacts all report 0.13.0; the LIVE board still reports 0.12.0 until the user flashes (that pending drift is the point of the check; it clears in Task 5).

---

### Task 5: Hardware verification checkpoint (user in the loop)

**Files:** none (drive the board over HTTP; use the flash-and-verify skill).

The user flashes; Claude verifies via HTTP only. Never claim success before this passes on hardware.

- [ ] **Step 1: User uploads BOTH artifacts**

Firmware changed AND `data/` changed, so two steps: Sketch -> Upload, then LittleFS upload (Ctrl+Shift+P -> "Upload LittleFS to Pico/ESP8266/ESP32", Serial Monitor closed).

- [ ] **Step 2: Confirm the new setting exists and version is live**

```bash
curl http://esp32matrix.local/api/settings
curl http://esp32matrix.local/api/status
npm run check
```

Expected: settings JSON contains `"idle_random":true`; status reports fw 0.13.0; `npm run check` fully green.

- [ ] **Step 3: Push the new rotation to the board (one-time NVS migration)**

The board's stored CSV predates this change (NVS keeps old values by design):

```bash
curl -X POST http://esp32matrix.local/api/settings -H "Content-Type: application/json" -d "{\"idle_apps\":\"fire,matrix_rain,clock,fireworks,fireworks2,frostbite,snow,dancefloor,spiral,wave,starfield,rainbow\"}"
```

Expected: response echoes the new CSV.

- [ ] **Step 4: Settings page check**

`curl http://esp32matrix.local/settings.html` and confirm the served file contains `idle_random` and `ibri_card` (curl beats the browser: stale-cache lesson). Then a live browser/Playwright check: checkbox checked, slider grayed; uncheck, slider unlocks.

- [ ] **Step 5: Fast screensaver soak**

Temporarily shorten timers, arm, and watch several rotations:

```bash
curl -X POST http://esp32matrix.local/api/settings -H "Content-Type: application/json" -d "{\"idle_after_secs\":10,\"idle_rotate_secs\":15}"
curl -X POST http://esp32matrix.local/api/idle/arm
```

Then poll `GET /api/status` (animation name changes every ~15s) and `GET /api/display/framebuffer` across two visits of the SAME app type: pixel colors must differ between visits (that is the randomization proof). The user eyeballs the LEDs for legibility at brightness 6-8.

- [ ] **Step 6: Random-off regression**

```bash
curl -X POST http://esp32matrix.local/api/settings -H "Content-Type: application/json" -d "{\"idle_random\":false}"
```

Wait for a fire launch: classic orange again, brightness = `idle_brightness` (5). Then re-enable random.

- [ ] **Step 7: Restore the board (house rule)**

```bash
curl -X POST http://esp32matrix.local/api/settings -H "Content-Type: application/json" -d "{\"idle_after_secs\":120,\"idle_rotate_secs\":240,\"idle_random\":true}"
```

Restore the display/brightness to what the user had before testing.

- [ ] **Step 8: PR**

Push the branch, open a PR to master (house workflow), body notes the NVS one-time migration and the intentional idle.ts divergence in random mode.
