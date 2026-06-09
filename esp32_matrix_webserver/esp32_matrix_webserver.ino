// ============================================================
// ESP32-S3 Matrix Web Server Firmware
// Board: Waveshare ESP32-S3-Matrix (8x8 WS2812B)
// ============================================================
// PURPOSE:
//   This firmware turns the ESP32-S3 into a WiFi-connected
//   HTTP server. It listens for commands sent from your PC
//   (via the MCP Node.js server) and controls the LED matrix.
//
//   Think of it this way:
//     [Claude] → [MCP Server on PC] → [HTTP] → [This firmware] → [LEDs]
//
// LIBRARIES — install via Arduino IDE → Tools → Manage Libraries:
//   - FastLED      by Daniel Garcia   (version 3.6 or later)
//   - ArduinoJson  by Benoit Blanchon (version 7.x)
//   - PNGdec       by Larry Bank      (any recent version) — for remote weather icon
//   - WiFiManager  by tzapu           (any recent version) — captive-portal WiFi setup
//   WiFi, WebServer, and WiFiClientSecure are built into the ESP32 Arduino core.
//
// ARDUINO IDE BOARD SETTINGS (Tools menu):
//   Board:            "ESP32S3 Dev Module"
//   USB Mode:         "Hardware CDC and JTAG"
//   USB CDC On Boot:  "Enabled"
//   Upload Speed:     921600
//   Flash Size:       "8MB (64Mb)"
//   Partition Scheme: "8MB with spiffs (3MB APP, 5MB SPIFFS)"
//
// UPLOADING WEB FILES (do this AFTER flashing firmware):
//   Arduino IDE → Tools → ESP32 LittleFS Data Upload
//
// FILE STRUCTURE — each .ino in this folder is compiled together:
//   esp32_matrix_webserver.ino — globals, setup(), loop(), core utilities
//   anim_fire.ino              — fire animation + spark system
//   anim_liquid.ino            — IMU driver + liquid/imu animations
//   anim_effects.ino           — rainbow, breathe, wave, solid
//   scroll_text.ino            — FONT data, drawCharCol, renderScrollFrame
//   weather.ino                — weather fetch, icon draw, chip temp
//   clock_timer.ino            — MINI_FONT, clock, all three timer modes
//   api_handlers.ino           — all HTTP route handler functions
// ============================================================

#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <FastLED.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Wire.h>
#include <math.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <PNGdec.h>
#include <time.h>
#include <Preferences.h>   // NVS key/value store — used for auto-resume

// ============================================================
// SECTION 1: HARDWARE CONFIGURATION
// ============================================================

#define LED_PIN    14
#define MATRIX_W   8
#define MATRIX_H   8
#define NUM_LEDS   (MATRIX_W * MATRIX_H)
#define LED_TYPE     WS2812B
#define COLOR_ORDER  RGB

#define IMU_SDA      11
#define IMU_SCL      12
#define QMI8658_ADDR 0x6B

// ============================================================
// SECTION 2: WIFI SETUP
// ============================================================
// WiFi credentials are configured at runtime via a captive portal —
// no hardcoded passwords, no recompiling. On first boot (or when saved
// credentials fail) the board starts a hotspot named "ESP32-Matrix-Setup".
// Connect to that network on any phone or laptop, enter your home WiFi
// name + password, and the board saves them to flash and reboots.
// From then on it connects automatically. To reconfigure, hold the BOOT
// button for 3 seconds while powering on (see setup() below).

// ============================================================
// SECTION 3: GLOBAL STATE
//
// All variables here are written by HTTP handlers and read by
// animation frame functions. Because the ESP32 is single-threaded
// (no RTOS tasks in this firmware), there are no race conditions —
// a handler runs to completion before loop() resumes.
// ============================================================

// FastLED LED buffer — one CRGB (24-bit RGB) per physical LED.
// FastLED.show() copies this array to the WS2812B strip.
CRGB     leds[NUM_LEDS];

// Built-in Arduino WebServer — handles HTTP on port 80.
WebServer server(80);

// NVS (non-volatile storage) — remembers the last display + brightness so the
// board resumes itself after a power cycle (see setup() and the API handlers).
Preferences prefs;

// ── Animation control ────────────────────────────────────────
uint8_t  brightness      = 40;      // global FastLED brightness (0-255)
bool     animationActive = false;   // true while any animation is running
String   animationName   = "";      // which animation is active (e.g. "fire")
uint32_t animationSpeed  = 66;      // ms between frames (lower = faster)
uint32_t lastFrameMs     = 0;       // millis() timestamp of the last frame
CRGB     solidColor      = CRGB(0, 100, 255);   // color used by solid/breathe/wave

// ── Text scroll state ────────────────────────────────────────
bool     textActive         = false;
String   scrollText         = "";
CRGB     scrollColor        = CRGB::White;
CRGB     scrollColor2       = CRGB(255,  68,   0);   // gradient colors 2-4
CRGB     scrollColor3       = CRGB(  0, 204, 100);
CRGB     scrollColor4       = CRGB(  0, 100, 255);
bool     scrollGradient     = false;
bool     scrollSmall        = false;               // use 3×5 font
bool     scrollTiny         = false;               // use 3×3 font (overrides small)
bool     scrollPausing      = false;               // true during the 1s gap between loops
uint32_t scrollPauseUntilMs = 0;
uint32_t scrollSpeed        = 100;   // ms per scroll step (lower = faster)
uint32_t lastScrollMs       = 0;
int      scrollOffset       = 0;     // how many pixels the text strip has advanced
int      scrollPixelLen     = 0;     // total pixel width of the text string

// Per-font character stride (char width + 1px gap between chars)
#define CHAR_W          5
#define CHAR_GAP        1
#define CHAR_TOTAL      (CHAR_W + CHAR_GAP)   // = 6 pixels per normal char

#define SMALL_CHAR_W     3
#define SMALL_CHAR_TOTAL 4   // 3 wide + 1 gap
#define TINY_CHAR_TOTAL  4   // 3×3 font: same stride as small

// ── Chip temperature ─────────────────────────────────────────
String   chipTempUnit      = "F";   // "F" or "C"

// ── Weather state ─────────────────────────────────────────────
String   weatherZip        = "85013";   // US zip code
String   weatherUnit       = "F";       // temperature unit: "F" or "C"
int      weatherTempVal    = 0;         // last fetched temperature
int      weatherCode       = 113;       // wttr.in condition code (113 = clear)
int      weatherHumidity   = 0;         // percent
int      weatherUvIndex    = 0;
int      weatherPressure   = 0;         // hPa
uint32_t lastWeatherFetch  = 0;         // millis() of last successful fetch
bool     weatherNeedsFetch = false;     // handler sets this; loop() does the fetch (off the request/boot path)
uint8_t  weatherFrame      = 0;         // frame counter for icon animations
String   weatherDataMode   = "temp";    // which data to show: temp/humidity/uv/pressure/cycle
String   weatherIconSource = "animated";// "animated" (built-in) or "remote" (fetched PNG)
uint32_t weatherPhaseStart = 0;         // millis() when the current phase (data/icon) started
bool     weatherShowIcon   = false;     // true = showing icon, false = showing data
CRGB     weatherIconBuf[64];            // decoded remote PNG icon, scaled to 8×8
bool     weatherHasIcon    = false;     // true once the remote icon has been fetched

// ── Weather 2 state ───────────────────────────────────────────
String   weather2Unit    = "F";
CRGB     weather2Color1  = CRGB(255, 165,   0);  // temp tens digit (amber)
CRGB     weather2Color2  = CRGB(255, 220,  80);  // temp units digit (gold)
int      weatherTempF    = 0;   // raw °F — stored separately so W1/W2 can use independent units
int      weatherTempC    = 0;   // raw °C

// ── Clock state ───────────────────────────────────────────────
CRGB     clockColorHours = CRGB(255,  51,   0);  // hours digit color    (#FF3300)
CRGB     clockColorColon = CRGB(255, 255, 255);  // colon dot color      (#FFFFFF)
CRGB     clockColorMins  = CRGB(  0, 204, 255);  // minutes digit color  (#00CCFF)
int      clockTimezone   = -7;                   // UTC offset in hours (e.g. -7 = Arizona MST)
String   clockTZ         = "";                    // POSIX TZ string (DST-aware, e.g. "MST7MDT,M3.2.0,M11.1.0"); empty = use offset
bool     ntpSynced       = false;
int      clockPrevHour   = -1;                   // used to skip redraws when nothing changed
int      clockPrevMin    = -1;

// ── Timer state ───────────────────────────────────────────────
uint32_t timerEndMs        = 0;             // millis() when the timer expires
uint32_t timerTotalMs      = 0;             // original duration in ms
CRGB     timerColor1       = CRGB(255, 200, 0);   // start color (bottom of fill, minutes)
CRGB     timerColor2       = CRGB(255,   0, 0);   // end color (top of fill, seconds)
CRGB     timerColorColon   = CRGB::White;          // colon color (timer_text only)
int      timerExpiredState = 0;             // 0=running, 1=blinking, 2=solid (expired)
uint32_t timerExpiredMs    = 0;

// ── Snow timer state ──────────────────────────────────────────
// SnowCell: one LED position. row 0 = bottom, row 7 = top.
// (The Y coordinate is inverted from matrix coordinates where y=0 is top.)
struct SnowCell { uint8_t col; uint8_t row; };
SnowCell snowPos[64];            // settlement order: cells sorted bottom-to-top, shuffled within each row
int      snowSettledCount   = 0; // how many cells have settled so far
bool     snowFallActive     = false;
int      snowFallCol        = 0;
int      snowFallTargetY    = 7;
uint32_t snowFallStartMs    = 0;
uint32_t snowFallDurationMs = 500;

// ── Liquid/IMU state ──────────────────────────────────────────
// Liquid is a 2D closed-container model: gravity is a vector in the matrix
// plane, and the fluid fills the most "downhill" cells up to a threshold that
// sloshes with momentum. (See docs/superpowers/specs/2026-06-08-liquid-fixes-design.md)
bool    imuReady              = false;         // set to true if IMU is detected at boot
float   liquidLevel           = 0.0f;          // fill threshold along the gravity axis
float   liquidLevelVel        = 0.0f;          // slosh velocity of that threshold
float   liquidGX              = 0.0f;          // smoothed in-plane gravity direction (x)
float   liquidGY              = 1.0f;          // smoothed in-plane gravity direction (y)
float   liquidDamping         = 0.88f;         // energy loss per frame (set from viscosity param)
bool    liquidGradient        = false;         // true = custom top/bottom gradient instead of palette
CRGB    liquidTopColor        = CRGB(230, 250, 255);  // surface/froth color (custom mode)
CRGB    liquidBottomColor     = CRGB(  0,  40, 160);  // deep color (custom mode)

// ── Calendar state ────────────────────────────────────────────
// Styles: "scroll" (date scrolls), "bignum" (day), "grid" (month grid), "clock"
// (month over day, clock-style). Date comes from NTP, same as clock mode.
String  calendarStyle  = "scroll";   // default matches the Calendar web page's selected style
CRGB    calendarColor1 = CRGB(0, 200, 255);    // primary  (day / today / text)
CRGB    calendarColor2 = CRGB(255, 120, 0);    // secondary(month / other days)
CRGB    calendarColor3 = CRGB(80, 80, 90);     // accent   (separator / grid frame)
int     calendarScrollX = MATRIX_W;            // scroll position for "scroll" style

// ── Sound (vibration) visualizer state ────────────────────────
// No microphone — the IMU feels low-frequency vibration (bass through a surface).
CRGB    soundColor1     = CRGB(0, 80, 255);     // bottom of the VU bar
CRGB    soundColor2     = CRGB(255, 0, 160);    // top of the VU bar
float   soundSensitivity = 5.0f;                // 0-10
float   soundBaseline   = 1.0f;                 // slow-tracked steady magnitude (gravity)
float   soundEnergy     = 0.0f;                 // smoothed current level 0-1
float   soundPeak       = 0.0f;                 // decaying peak marker 0-1

// ── Gradient Spiral ───────────────────────────────────────────────────────────
CRGB     spiralColor1   = CRGB(255,   0,   0);
CRGB     spiralColor2   = CRGB(  0,   0, 255);

// ── Gradient Starfield ────────────────────────────────────────────────────────
CRGB     starColor1     = CRGB(255, 255, 255);
CRGB     starColor2     = CRGB(  0, 100, 255);
uint8_t  starDensity    = 8;
bool     starInward     = false;

// ── Dance Floor ───────────────────────────────────────────────────────────────
uint8_t  dfPalette  =  0;
uint8_t  dfHoldMin  = 12;

// ── Fireworks 1 ───────────────────────────────────────────────────────────────
CRGB     fwColor1       = CRGB(255,  50,   0);
CRGB     fwColor2       = CRGB(255, 200,   0);
CRGB     fwColor3       = CRGB(  0, 100, 255);

// ── Fireworks 2 ───────────────────────────────────────────────────────────────
CRGB     fw2Color1      = CRGB(255,  50,   0);
CRGB     fw2Color2      = CRGB(255, 200,   0);
CRGB     fw2Color3      = CRGB(  0, 100, 255);

// ── Wave ──────────────────────────────────────────────────────────────────────
CRGB     waveColor1     = CRGB(  0,   0, 255);   // surface color
CRGB     waveColor2     = CRGB(  0,   0,  40);   // depth color

// ── Rainbow ───────────────────────────────────────────────────────────────────
bool  rainbowUsePalette   = false;
CRGB  rainbowPalColors[4] = { CRGB(255,0,0), CRGB(255,200,0), CRGB(0,200,0), CRGB(0,100,255) };

// ── Comet ─────────────────────────────────────────────────────────────────────
// cometColor3 bumped to R=200 — at nscale8(64) and bri=40 gives effective=8 (was borderline 6)
CRGB     cometColor1    = CRGB(255, 200,  50);
CRGB     cometColor2    = CRGB(255, 100,   0);
CRGB     cometColor3    = CRGB(200,  50,   0);
CRGB     cometColor4    = CRGB( 80,  10,   0);

// ── Sun ───────────────────────────────────────────────────────────────────────
CRGB     sunColor1      = CRGB(255, 183,   0);
CRGB     sunColor2      = CRGB(255, 102,   0);
CRGB     sunColor3      = CRGB(255,  51,   0);
CRGB     sunColor4      = CRGB(204,  17,   0);
CRGB     sunColor5      = CRGB(136,   0,   0);
uint8_t  sunDiscBri     = 200;   // disc brightness 0-255
uint8_t  sunRingBri     = 200;   // orbit dot brightness 0-255

// ── Frostbite ─────────────────────────────────────────────────────────────────
CRGB     fbColor     = CRGB(220, 230, 255);   // cool ice-white default
uint8_t  fbSparkRate = 20;                    // sparkle spawn probability 0-100
uint8_t  fbMistMax   = 80;                    // shimmer brightness ceiling 0-255

// ── Grid Test ─────────────────────────────────────────────────────────────────
// Diagnostic app for establishing per-pixel color/brightness thresholds.
// "color" mode: 64 pixels with R = (linear index + 1) * 4 (R=4 at [1,1] → R=255 at [8,8])
// "brightness" mode: all pixels at full red (255,0,0), vary brightness to find cutoff.
// Static display — no animation loop; leds[] is redrawn only when the endpoint is called.
String  gridTestMode       = "color";   // "color" or "brightness"
uint8_t gridTestBrightness = 255;

// ============================================================
// SECTION 5: LED BRIGHTNESS CALIBRATION REFERENCE
//
// Established via grid test on 2026-05-27. Confirmed empirically
// against this specific Waveshare ESP32-S3-Matrix board.
//
// ── FORMULA ──────────────────────────────────────────────────
//   FastLED's nscale8x3 (used internally by FastLED.setBrightness)
//   applies the following to every channel before writing to the strip:
//
//       effective = (channel × (brightness + 1)) >> 8
//
//   The LED is physically ON  when effective >= 1.
//   The LED is physically OFF when effective == 0.
//
// ── MINIMUM VISIBLE CHANNEL VALUE ────────────────────────────
//   min_visible = ceil(256 / (brightness + 1))
//
//   brightness = 255 → min = 1    (anything non-zero is visible)
//   brightness = 100 → min = 3
//   brightness =  40 → min = 7
//   brightness =  20 → min = 13
//   brightness =  10 → min = 24
//   brightness =   5 → min = 43
//   brightness =   3 → min = 64
//
//   Any channel (R, G, or B) below min_visible is physically dark
//   regardless of what color you set.
//
// ── DESIGN RULES FOR APP DEVELOPMENT ────────────────────────
//   1. Before using a color in an app, verify every channel
//      satisfies: channel × (brightness + 1) >= 256.
//      If not, that channel contributes nothing to the output.
//
//   2. For gradient/fade effects (trails, halos, backgrounds):
//      The dimmest step MUST still clear the threshold.
//      e.g. a 5-step trail at bri=40 whose dimmest step = 20:
//        20 × 41 / 256 = 3.2 → 3.  OK (visible), but barely.
//      At bri=10 the same step: 20 × 11 / 256 = 0.86 → 0. GONE.
//
//   3. Subtle color differences (e.g. R=50 vs R=60) may be
//      indistinguishable at low brightness because both map to
//      the same small effective integer.
//
//   4. In JS previews, use this exact formula:
//         Math.floor(channel * (brightness + 1) / 256)
//      NOT  channel * brightness / 255  — that formula is wrong
//      and was the reason previews did not match the board.
//
// ── BRIGHTNESS SWEEP OBSERVATIONS ────────────────────────────
//   brightness = 0  → all LEDs off (FastLED special-cases 0).
//   brightness = 1  → full red (255,0,0) barely visible; effective = 1.
//   brightness 1→255 → visibly and smoothly increases.
//
// ── RELIABILITY NOTE ─────────────────────────────────────────
//   The formula is reliable for brightness >= 3. At brightness 1-2,
//   individual LED variation on this board makes the threshold
//   slightly inconsistent from pixel to pixel. Design for >= 3.
// ============================================================

// ============================================================
// SECTION 4: COORDINATE MAPPING
//
// The Waveshare ESP32-S3-Matrix wires its 8×8 WS2812B grid in
// a simple raster layout — every row goes left-to-right, top
// row first. So LED 0 = top-left, LED 7 = top-right,
// LED 8 = start of row 1, LED 63 = bottom-right.
//
// x=0 is left, x=7 is right. y=0 is top, y=7 is bottom.
// ============================================================

// Converts (x, y) matrix coordinates to a flat leds[] index.
// Returns -1 for any out-of-bounds coordinate — callers check for this.
int XY(int x, int y) {
  if (x < 0 || x >= MATRIX_W || y < 0 || y >= MATRIX_H) return -1;
  return y * MATRIX_W + x;
}

// Safe pixel setter: does nothing if coordinates are out of bounds.
void setPixel(int x, int y, CRGB color) {
  int idx = XY(x, y);
  if (idx >= 0) leds[idx] = color;
}

// Parses a CSS hex color string ("#RRGGBB") into a CRGB value.
// strtol(..., 16) converts the 6-char hex string to a 24-bit integer,
// then bit-masking extracts each 8-bit channel.
// Returns white if the input format is invalid.
CRGB hexToColor(String hex) {
  if (hex.startsWith("#")) hex = hex.substring(1);
  if (hex.length() != 6) return CRGB::White;
  long v = strtol(hex.c_str(), nullptr, 16);
  return CRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// ============================================================
// SECTION 9: HTTP HELPERS
// ============================================================

void sendJson(int code, const String& body) {
  server.send(code, "application/json", body);
}

String getContentType(String path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css"))  return "text/css";
  if (path.endsWith(".js"))   return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".ico"))  return "image/x-icon";
  return "text/plain";
}

void stopAll() {
  animationActive = false;
  textActive = false;
}

// ============================================================
// SECTION 10: HTTP ROUTE HANDLERS (continued in api_handlers.ino)
// ============================================================

void handleRoot() {
  if (LittleFS.exists("/index.html")) {
    File file = LittleFS.open("/index.html", "r");
    server.streamFile(file, "text/html");
    file.close();
    return;
  }
  String ip = WiFi.localIP().toString();
  String html = "<!DOCTYPE html><html><body style='font-family:monospace;padding:20px;background:#111;color:#eee'>";
  html += "<h2 style='color:#f90'>ESP32-S3 Matrix — Firmware OK, web files not uploaded yet</h2>";
  html += "<p>Board is running at <strong>" + ip + "</strong>. The API is ready.</p>";
  html += "<p style='margin-top:12px'>To load the web UI, upload the <code>data/</code> folder:<br>";
  html += "<strong>Arduino IDE &rarr; Tools &rarr; ESP32 LittleFS Data Upload</strong></p>";
  html += "<h3 style='margin-top:20px'>API Endpoints</h3><pre style='background:#222;padding:16px;border-radius:6px'>";
  html += "POST /api/display/clear\n";
  html += "POST /api/brightness          {\"level\": 0-255}\n";
  html += "POST /api/display/text        {\"text\": \"HELLO\", \"color\": \"#FF0000\", \"scroll_speed\": 100}\n";
  html += "POST /api/display/animation   {\"type\": \"fire|rainbow|breathe|wave|solid\",\n";
  html += "                               \"speed\": 66, \"theme\": \"classic|blue|green|purple\",\n";
  html += "                               \"intensity\": 1-10, \"tendrils\": 0-10}\n";
  html += "POST /api/display/matrix      {\"matrix\": [[8 rows of 8 hex colors]]}\n";
  html += "POST /api/display/temperature {\"value\": 72, \"unit\": \"F\", \"color\": \"#FF4400\"}\n";
  html += "GET  /api/sensors/temperature\n";
  html += "</pre></body></html>";
  server.send(200, "text/html", html);
}

// ============================================================
// SECTION 11: SETUP — runs once on boot
// ============================================================

bool applyAnimationBody(const String& body);   // defined in api_handlers.ino (used by auto-resume)

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32-S3 Matrix Web Server ===");

  prefs.begin("matrix", false);   // open NVS (read/write) for auto-resume state

  initIMU();

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(brightness);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  Serial.println("LEDs initialized.");

  // Hold BOOT (GPIO 0) during power-on to wipe saved credentials and force
  // the config portal — useful if you're moving the board to a new network.
  pinMode(0, INPUT_PULLUP);
  if (digitalRead(0) == LOW) {
    Serial.println("BOOT held — clearing saved WiFi credentials.");
    WiFiManager wm;
    wm.resetSettings();
  }

  // Blue = trying to connect (or waiting for portal input)
  fill_solid(leds, NUM_LEDS, CRGB::Blue);
  FastLED.show();

  WiFiManager wm;
  wm.setConnectTimeout(10);  // give up on saved credentials after 10 s

  // Amber = config portal is open; user needs to connect to "ESP32-Matrix-Setup"
  wm.setAPCallback([](WiFiManager*) {
    Serial.println("No saved WiFi found — config portal open.");
    Serial.println("Connect to hotspot: ESP32-Matrix-Setup");
    Serial.println("Then open 192.168.4.1 in your browser.");
    fill_solid(leds, NUM_LEDS, CRGB(255, 80, 0));
    FastLED.show();
  });

  // autoConnect() tries saved credentials first. If that fails it starts the
  // "ESP32-Matrix-Setup" AP and blocks here until the user configures WiFi.
  if (!wm.autoConnect("ESP32-Matrix-Setup")) {
    Serial.println("WiFi setup failed — restarting in 5 s...");
    delay(5000);
    ESP.restart();
  }

  Serial.println("WiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Keep WiFi alive. The firmware previously connected once and never recovered
  // if the link dropped (so any glitch = permanently offline until reflash).
  //   - setAutoReconnect: STA auto-reconnects on disconnect
  //   - setSleep(false):  disable modem power-save (a common silent-drop cause)
  //   - onEvent loggers:  print the disconnect REASON code so we can see WHY,
  //     and re-announce mDNS after a reconnect.
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    Serial.printf("WiFi DISCONNECTED, reason=%d — retrying\n", info.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) {
    Serial.print("WiFi reconnected, IP "); Serial.println(WiFi.localIP());
    MDNS.end(); MDNS.begin("esp32matrix");
  }, ARDUINO_EVENT_WIFI_STA_GOT_IP);

  if (MDNS.begin("esp32matrix")) {
    Serial.println("mDNS started — board reachable at http://esp32matrix.local");
  } else {
    Serial.println("WARNING: mDNS failed. Use IP address above to reach the board.");
  }

  fill_solid(leds, NUM_LEDS, CRGB::Green);
  FastLED.show();
  delay(800);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  if (!LittleFS.begin(true)) {
    Serial.println("WARNING: LittleFS mount failed — web UI files not available yet.");
    Serial.println("  Upload data/ folder via: Tools → ESP32 LittleFS Data Upload");
  } else {
    Serial.println("LittleFS mounted. Web UI ready at http://" + WiFi.localIP().toString());
  }

  server.on("/",                          HTTP_GET,  handleRoot);
  server.on("/api/display/clear",         HTTP_POST, handleClear);
  server.on("/api/brightness",            HTTP_POST, handleBrightness);
  server.on("/api/display/text",          HTTP_POST, handleText);
  server.on("/api/display/animation",     HTTP_POST, handleAnimation);
  server.on("/api/display/matrix",        HTTP_POST, handleMatrix);
  server.on("/api/display/temperature",   HTTP_POST, handleTemperature);
  server.on("/api/sensors/temperature",   HTTP_GET,  handleSensorTemperature);
  server.on("/api/sensors/accelerometer", HTTP_GET,  handleSensorAccelerometer);
  server.on("/api/sensors/weather",       HTTP_GET,  handleSensorWeather);
  server.on("/api/weather/mode",          HTTP_POST, handleWeatherMode);
  server.on("/api/status",               HTTP_GET,  handleStatus);
  server.on("/api/grid-test/set",        HTTP_POST, handleGridTest);
  server.onNotFound([]() {
    String path = server.uri();
    if (LittleFS.exists(path)) {
      File file = LittleFS.open(path, "r");
      server.streamFile(file, getContentType(path));
      file.close();
      return;
    }
    if (path.startsWith("/api/")) {
      sendJson(404, "{\"error\":\"Unknown API endpoint. GET / for the full list.\"}");
    } else {
      sendJson(404, "{\"error\":\"File not found: " + path + "\"}");
    }
  });

  server.begin();
  Serial.println("HTTP server started on port 80.");
  Serial.println("Test it: open http://" + WiFi.localIP().toString() + " in your browser.");

  // ── Auto-resume: restore the last display + brightness from NVS ──────────────
  // WiFi is already up (autoConnect blocks above), so clock/calendar NTP works.
  brightness = prefs.getUChar("bri", brightness);
  FastLED.setBrightness(brightness);
  if (prefs.getString("kind", "") == "anim") {
    String body = prefs.getString("animbody", "");
    if (body.length()) {
      Serial.println("Auto-resume: restoring last animation.");
      applyAnimationBody(body);   // defined in api_handlers.ino
    }
  }
}

// ============================================================
// SECTION 12: LOOP — runs continuously
//
// CRITICAL DESIGN RULE: Never use delay() in loop().
// delay() blocks the entire CPU — server.handleClient() can't
// run while we're waiting, so incoming HTTP requests time out.
//
// Instead we use millis() timestamps to check elapsed time:
//   if (now - lastFrameMs >= animationSpeed) { ...update frame... }
// This pattern is called "non-blocking timing" — it lets the loop
// run thousands of times per second, calling handleClient() every
// iteration, and only draws a new frame when enough time has passed.
// ============================================================

void loop() {
  server.handleClient();   // process any pending HTTP requests first

  // WiFi self-heal backstop: if the link is down for a few seconds (and the
  // built-in auto-reconnect hasn't recovered it), force a reconnect. Keeps the
  // board from being stranded offline while it keeps animating.
  static uint32_t lastWifiCheck = 0;
  if (millis() - lastWifiCheck >= 5000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi down — forcing reconnect.");
      WiFi.reconnect();
    }
  }

  uint32_t now = millis();   // snapshot the clock once per loop iteration

  // Rainbow always at 66ms. Frostbite capped at 66ms minimum — shimmer and sine sparkles
  // need at least 15fps to look smooth; faster is fine, slower creates visible stepping.
  uint32_t effectiveRate = (animationName == "rainbow") ? 66u :
                           (animationName == "frostbite") ? min(animationSpeed, (uint32_t)66) :
                           animationSpeed;

  // Animation frame tick: only draw when enough time has elapsed
  if (animationActive && (now - lastFrameMs >= effectiveRate)) {
    lastFrameMs = now;
    if      (animationName == "fire")       stepFireFrame();
    else if (animationName == "rainbow")    runRainbowFrame();
    else if (animationName == "breathe")    runBreatheFrame();
    else if (animationName == "wave")       runWaveFrame();
    else if (animationName == "solid")      runSolidFrame();
    else if (animationName == "liquid")     stepLiquidFrame();
    else if (animationName == "imu")        stepImuFrame();
    else if (animationName == "chiptemp")   stepChipTempFrame();
    else if (animationName == "weather")    stepWeatherFrame();
    else if (animationName == "weather2")   stepWeather2Frame();
    else if (animationName == "timer_fill") stepTimerFillFrame();
    else if (animationName == "timer_snow") stepTimerSnowFrame();
    else if (animationName == "timer_text") stepTimerTextFrame();
    else if (animationName == "clock")      stepClockFrame();
    else if (animationName == "matrix_rain") stepMatrixFrame();
    else if (animationName == "dancefloor") runDanceFloorFrame();
    else if (animationName == "spiral")    runSpiralFrame();
    else if (animationName == "starfield") runStarfieldFrame();
    else if (animationName == "fireworks")  stepFireworksFrame();
    else if (animationName == "fireworks2") stepFireworks2Frame();
    else if (animationName == "comet")      runCometFrame();
    else if (animationName == "sun")       runSunFrame();
    else if (animationName == "frostbite") runFrostbiteFrame();
    else if (animationName == "calendar")  stepCalendarFrame();
    else if (animationName == "sound")     stepSoundFrame();
    FastLED.show();
  }

  // Text scroll tick — same millis() pattern as animation above.
  // The text loops: scroll across, blank for 1 second, then restart.
  if (textActive) {
    if (scrollPausing) {
      // Blank screen pause between loops — wait for the timeout then restart
      if (now >= scrollPauseUntilMs) {
        scrollPausing = false;
        scrollOffset  = 0;
        lastScrollMs  = now;
        renderScrollFrame();
        FastLED.show();
      }
    } else if (now - lastScrollMs >= scrollSpeed) {
      lastScrollMs = now;
      scrollOffset++;
      // Once the full text strip + the width of the screen has scrolled past,
      // the display is clear — start the 1-second pause before looping.
      if (scrollOffset >= MATRIX_W + scrollPixelLen) {
        scrollPausing      = true;
        scrollPauseUntilMs = now + 1000;
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
      } else {
        renderScrollFrame();
        FastLED.show();
      }
    }
  }
}
