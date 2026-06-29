# ESP32-S3 Matrix — Firmware Project Brief (read me first)

Waveshare ESP32-S3-Matrix (8×8 WS2812B) firmware + its self-contained onboard web UI.
The board runs standalone (no computer): WiFi captive-portal onboarding, an animation
selector, weather/clock, a calibration lab, and an HTTP API. It exposes that API
(`docs/API.md`) so external clients can drive it.

> **Privacy:** never use the maintainer's real name in code, comments, or docs —
> this repo is distributable; refer to "the user" instead.

> **The Claude integration lives elsewhere.** The MCP server, the Expression Studio,
> the trigger manifest, and the Claude Code hooks are in the separate
> **`claude-expression-studio`** repo. They drive this board ONLY via `docs/API.md`;
> no code is shared. See `docs/PITFALLS.md` before debugging hardware.

---

## How we work (the dev loop)

**Claude cannot compile, flash, or see the LEDs.** Claude edits firmware (`.ino`) and
web UI (`data/*.html`); **you** flash and report back:

1. **Sketch → Upload** (firmware), and if `data/` changed, **Tools → ESP32 LittleFS
   Data Upload** (web files). *Two separate steps.*
2. Paste the **Serial Monitor** output and/or describe the LED behavior.

End users instead flash one pre-merged binary (`install/`, produced by
`npm run build:release`). **Never claim a change "works" until confirmed on hardware.**

## Hardware facts (don't re-derive these)

| Thing | Value |
|---|---|
| Board | Waveshare ESP32-S3-Matrix |
| LEDs | 8×8 WS2812B, 64 total, **data pin 14** |
| `LED_TYPE` | `WS2812B` |
| **`COLOR_ORDER`** | **`RGB`** ⚠️ not the usual GRB — `CRGB(r,g,b)` maps straight through |
| IMU | QMI8658C 6-axis, I2C **SDA=11 SCL=12**, addr **0x6B** |
| Flash / PSRAM | **4MB** flash + 2MB PSRAM. LittleFS in the 1MB SPIFFS region of `huge_app`. |
| Default brightness | 40 / 255 |

### Coordinate system
`XY(x, y)` returns `y * 8 + x` — **plain row-major, NOT serpentine.** Origin top-left,
x→right, y→down. Out-of-bounds returns `-1`. Always draw via `setPixel(x, y, CRGB)`
(bounds-checked, in `esp32_matrix_webserver.ino`).

## Arduino IDE setup

**Libraries:** FastLED · ArduinoJson · PNGdec · **WiFiManager *by tzapu***.
**Board settings:** Board `Waveshare ESP32-S3-Matrix`; **PSRAM `Enabled`** (the board
has 2MB — leaving it off starved the heap and caused WiFi/web instability); USB Mode
`Hardware CDC and JTAG`; USB CDC On Boot `Enabled`; Upload Speed `921600`; Flash Size
`4MB`; Partition `Huge APP (3MB No OTA / 1MB SPIFFS)`.
**Web-file upload (IDE 2.x):** install `arduino-littlefs-upload` `.vsix`, then
**Ctrl+Shift+P → "Upload LittleFS to Pico/ESP8266/ESP32"** (Command Palette; close the
Serial Monitor first). See `docs/PITFALLS.md`.

## WiFi

WiFiManager captive portal. Boot tries saved WiFi (LEDs blue); on failure opens hotspot
**`ESP32-Matrix-Setup`** (amber) at `192.168.4.1`. Hold **BOOT (GPIO 0)** at power-on to
wipe creds. Reachable at `http://esp32matrix.local`. A gitignored `secrets.h`
(`WIFI_SSID`/`WIFI_PASSWORD`) skips the portal — but **a distributable `.bin` must be
built WITHOUT it** (`build-release.mjs` refuses if present; `--allow-secrets` = personal).

## Firmware layout (all `.ino` in `esp32_matrix_webserver/` compile as one unit)

`esp32_matrix_webserver.ino` (globals, setup/loop, `XY`/`setPixel`, dispatch) ·
`api_handlers.ino` (HTTP routes) · `anim_*.ino` (one animation each) · `scroll_text.ino`
· `fonts.ino` · `weather.ino` · `clock_timer.ino` · `anim_presence.ino` (native presence
render) · `data/*.html` + the shared web design system (`app.css`, `backnav.js`,
`header.js`, `bright.js`, `previews.js`, `palettes.js` — all `data-auto` self-injecting).

### Adding an animation (recipe)
1. `anim_<name>.ino` — state + `run<Name>Frame()`. 2. Dispatch branch in the main `.ino`
loop. 3. `api_handlers.ino` `handleAnimation()` — parse params + set `animationName`.
4. `data/<name>.html` control page (clone `rainbow.html`; shared design system). 5. Card
in `data/animations.html` (the hub, NOT index). See the `add-animation` skill.

## API, settings, NVS, calibration

- **API:** full HTTP surface in `docs/API.md` (the contract `claude-expression-studio`
  depends on).
- **Auto-resume (NVS):** persists last animation + brightness (`Preferences`, namespace
  `matrix`); restores on boot. `transient:true` on an animation POST skips NVS write.
- **Settings (NVS):** `POST/GET /api/settings` (partial merge). Keys: `idle_*`,
  `default_brightness`, `boot_animation`, `timezone`, `calibration_correction`.
- **Idle screensaver:** armed by `POST /api/idle/arm`; rotates `idle_apps` at
  `idle_brightness` after `idle_after_secs`.
- **Calibration:** the Lab (`data/calibrate.html`) measures into `data/calibration.json`
  (`GET/POST /api/calibration`, live-reload). Correction runs at the `matrixShow()`
  chokepoint (save→correct-in-place→restore). White-balance green gain **0.863**; gamma
  kept at 1.0 (global value-domain gamma crushed dim content). See the calibration specs.

## Versioning & discovery

Canonical `VERSION` → `version.h` (`FW_VERSION`) + `data/version.json`. `GET /api/status`
reports `fw_version`/`fw_built`/`web_version`. `npm run check` flags drift. Board address =
`ESP32_URL` env (default `http://esp32matrix.local`). (The MCP server is versioned
separately in the `claude-expression-studio` repo.)

Deeper material: `docs/PITFALLS.md`, `docs/superpowers/specs/`, `docs/superpowers/plans/`.
