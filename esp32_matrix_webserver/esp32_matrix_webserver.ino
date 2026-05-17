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
// ============================================================

CRGB     leds[NUM_LEDS];
WebServer server(80);

uint8_t  brightness      = 40;
bool     animationActive = false;
String   animationName   = "";
uint32_t animationSpeed  = 66;
uint32_t lastFrameMs     = 0;
CRGB     solidColor      = CRGB(0, 100, 255);

bool     textActive         = false;
String   scrollText         = "";
CRGB     scrollColor        = CRGB::White;
CRGB     scrollColor2       = CRGB(255, 68, 0);
bool     scrollGradient     = false;
bool     scrollSmall        = false;
bool     scrollTiny         = false;
bool     scrollPausing      = false;
uint32_t scrollPauseUntilMs = 0;
uint32_t scrollSpeed      = 100;
uint32_t lastScrollMs     = 0;
int      scrollOffset     = 0;
int      scrollPixelLen   = 0;

#define CHAR_W          5
#define CHAR_GAP        1
#define CHAR_TOTAL      (CHAR_W + CHAR_GAP)

#define SMALL_CHAR_W    3
#define SMALL_CHAR_TOTAL 4   // 3 wide + 1 gap
#define TINY_CHAR_TOTAL  4   // 3×3 font: same stride as small

String   chipTempUnit      = "F";

String   weatherZip        = "85013";
String   weatherUnit       = "F";
int      weatherTempVal    = 0;
int      weatherCode       = 113;
int      weatherHumidity   = 0;
int      weatherUvIndex    = 0;
int      weatherPressure   = 0;
uint32_t lastWeatherFetch  = 0;
uint8_t  weatherFrame      = 0;
String   weatherDataMode   = "temp";
String   weatherIconSource = "animated";
uint32_t weatherPhaseStart = 0;
bool     weatherShowIcon   = false;
CRGB     weatherIconBuf[64];
bool     weatherHasIcon    = false;

CRGB     clockBgColor      = CRGB(0, 0, 64);
int      clockTimezone     = -7;
bool     ntpSynced         = false;
int      clockPrevHour     = -1;
int      clockPrevMin      = -1;

uint32_t timerEndMs        = 0;
uint32_t timerTotalMs      = 0;
CRGB     timerColor1       = CRGB(255, 200, 0);
CRGB     timerColor2       = CRGB(255,   0, 0);
CRGB     timerColorColon   = CRGB::White;
int      timerExpiredState = 0;
uint32_t timerExpiredMs    = 0;

int      fillLitCount      = 0;

struct SnowCell { uint8_t col; uint8_t row; }; // row: 0=bottom, 7=top
SnowCell snowPos[64];           // shuffled settle order (bottom rows first)
int      snowSettledCount   = 0;
bool     snowFallActive     = false;
int      snowFallCol        = 0;
int      snowFallTargetY    = 7;
uint32_t snowFallStartMs    = 0;
uint32_t snowFallDurationMs = 500;

bool    imuReady              = false;
float   liquidHeight[MATRIX_W];
float   liquidVelocity[MATRIX_W];
float   liquidDamping         = 0.88f;

// ============================================================
// SECTION 4: COORDINATE MAPPING
//
// Simple grid wiring — all rows left → right.
// x=0 is left, x=7 is right. y=0 is top, y=7 is bottom.
// ============================================================

int XY(int x, int y) {
  if (x < 0 || x >= MATRIX_W || y < 0 || y >= MATRIX_H) return -1;
  return y * MATRIX_W + x;
}

void setPixel(int x, int y, CRGB color) {
  int idx = XY(x, y);
  if (idx >= 0) leds[idx] = color;
}

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
// Use millis()-based timing so the web server stays responsive.
// ============================================================

void loop() {
  server.handleClient();

  uint32_t now = millis();

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
    FastLED.show();
  }

  if (textActive) {
    if (scrollPausing) {
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
