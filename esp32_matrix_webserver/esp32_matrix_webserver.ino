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
//   - FastLED    by Daniel Garcia   (version 3.6 or later)
//   - ArduinoJson by Benoit Blanchon (version 7.x)
//   - PNGdec     by Larry Bank      (any recent version) — for remote weather icon
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
// SECTION 2: WIFI CREDENTIALS
// ============================================================
// Credentials live in secrets.h — copy secrets.h.example to
// secrets.h and fill in your network name and password.
// secrets.h is gitignored and never committed to the repo.

#include "secrets.h"

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
CRGB     scrollColor2       = CRGB(255, 68, 0);   // second color for gradient mode
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
uint8_t  weatherFrame      = 0;         // frame counter for icon animations
String   weatherDataMode   = "temp";    // which data to show: temp/humidity/uv/pressure/cycle
String   weatherIconSource = "animated";// "animated" (built-in) or "remote" (fetched PNG)
uint32_t weatherPhaseStart = 0;         // millis() when the current phase (data/icon) started
bool     weatherShowIcon   = false;     // true = showing icon, false = showing data
CRGB     weatherIconBuf[64];            // decoded remote PNG icon, scaled to 8×8
bool     weatherHasIcon    = false;     // true once the remote icon has been fetched

// ── Clock state ───────────────────────────────────────────────
CRGB     clockColorHours = CRGB(255,  51,   0);  // hours digit color    (#FF3300)
CRGB     clockColorColon = CRGB(255, 255, 255);  // colon dot color      (#FFFFFF)
CRGB     clockColorMins  = CRGB(  0, 204, 255);  // minutes digit color  (#00CCFF)
int      clockTimezone   = -7;                   // UTC offset in hours (e.g. -7 = Arizona MST)
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

int      fillLitCount      = 0;   // not used directly — settlement count is in snowSettledCount

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
bool    imuReady              = false;         // set to true if IMU is detected at boot
float   liquidHeight[MATRIX_W];               // simulated surface height per column (0-7)
float   liquidVelocity[MATRIX_W];             // vertical velocity per column
float   liquidDamping         = 0.88f;        // energy loss per frame (set from viscosity param)

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

// ── Comet ─────────────────────────────────────────────────────────────────────
CRGB     cometColor1    = CRGB(255, 200,  50);
CRGB     cometColor2    = CRGB(255, 100,   0);
CRGB     cometColor3    = CRGB(150,  30,   0);

// ── Sun ───────────────────────────────────────────────────────────────────────
CRGB     sunColor1      = CRGB(255, 183,   0);
CRGB     sunColor2      = CRGB(255, 102,   0);
CRGB     sunColor3      = CRGB(255,  51,   0);
CRGB     sunColor4      = CRGB(204,  17,   0);

// ── Frostbite ─────────────────────────────────────────────────────────────────
CRGB     fbColor     = CRGB(220, 230, 255);   // cool ice-white default
uint8_t  fbSparkRate = 20;                    // sparkle spawn probability 0-100
uint8_t  fbMistMax   = 80;                    // shimmer brightness ceiling 0-255

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

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32-S3 Matrix Web Server ===");

  initIMU();

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(brightness);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  Serial.println("LEDs initialized.");

  Serial.print("Connecting to: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Connection progress animation: each LED lights blue briefly as
  // we wait for WiFi. The dot counter sweeps left-to-right, top-to-bottom.
  // delay() is safe here because we're still in setup() — the web server
  // isn't running yet so there's nothing to be non-blocking about.
  int dot = 0;
  while (WiFi.status() != WL_CONNECTED) {
    int idx = XY(dot % MATRIX_W, (dot / MATRIX_W) % MATRIX_H);
    if (idx >= 0) {
      leds[idx] = CRGB::Blue;
      FastLED.show();
      delay(80);
      leds[idx] = CRGB::Black;
    }
    dot++;
    if (dot % 16 == 0) Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

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

  uint32_t now = millis();   // snapshot the clock once per loop iteration

  // Animation frame tick: only draw when animationSpeed ms have elapsed
  if (animationActive && (now - lastFrameMs >= animationSpeed)) {
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
    else if (animationName == "fireworks3") stepFireworks3Frame();
    else if (animationName == "fireworks4") stepFireworks4Frame();
    else if (animationName == "comet")      runCometFrame();
    else if (animationName == "sun")       runSunFrame();
    else if (animationName == "frostbite") runFrostbiteFrame();
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
