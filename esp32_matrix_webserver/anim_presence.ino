// ============================================================
// anim_presence.ino — board-native rendering of presence DATA on the 8×8.
//
// Presence v0.5: when animationName == "presence", the board draws the stored
// PresenceMessage's `data` (progress / values / series) in the intent's color.
// Glyph-only presences are still rendered by the MCP server (frame push) — this
// file only handles the data shapes.
//
// parsePresence() parses the stored presenceJson ONCE (when presence mode is
// entered) into the cache below; runPresenceFrame() only draws from the cache
// each frame (no JSON in the render loop — heap discipline).
// ============================================================

enum PresShape : uint8_t { PRES_NONE, PRES_PROGRESS, PRES_VALUES, PRES_SERIES };

static PresShape presShape       = PRES_NONE;
static CRGB      presColor       = CRGB::White;
static float     presProgress    = 0.0f;          // 0..1
static float     presValues[3];                   // up to 3 readout values
static uint8_t   presValueCount  = 0;
static uint8_t   presSeries[8];                   // pre-normalized column heights 1..8
static uint8_t   presSeriesCount = 0;
static uint8_t   presValueIdx    = 0;             // which value is showing (cycle)
static uint32_t  presValueLastMs = 0;
static int       presScrollX     = 0;             // x offset for long-number scroll
static uint32_t  presScrollLastMs = 0;

// Intent → color. Mirrors PRESENCE_VOCAB in data/presence-vocab.js so the 8×8
// and the desktop card agree. COLOR_ORDER is RGB, so CRGB(r,g,b) maps straight.
CRGB presenceColor(const String& intent) {
  if (intent == "working"  || intent == "alert")    return CRGB(0xE0, 0xA0, 0x20); // amber
  if (intent == "thinking" || intent == "question") return CRGB(0x3A, 0x78, 0xD0); // blue
  if (intent == "done"     || intent == "ok")       return CRGB(0x33, 0xC0, 0x6A); // green
  if (intent == "celebrate")                        return CRGB(0xD2, 0x4B, 0xD2); // magenta
  if (intent == "error")                            return CRGB(0xE0, 0x47, 0x3C); // red
  if (intent == "info")                             return CRGB(0x7A, 0x8A, 0xA0); // slate
  if (intent == "idle")                             return CRGB(0x46, 0x50, 0x6A); // dim indigo
  return NEUTRAL_WHITE;                                                             // unknown → neutral white (locked #FFFFE8)
}

// Parse the stored presenceJson into the render cache. Called when presence mode
// is entered (from applyAnimationBody). Sets presShape = PRES_NONE if there's no
// usable data (so runPresenceFrame draws nothing).
void parsePresence() {
  presShape       = PRES_NONE;
  presValueCount  = 0;
  presSeriesCount = 0;
  presValueIdx    = 0;
  presValueLastMs = millis();
  presScrollX     = MATRIX_W;
  presScrollLastMs = millis();

  JsonDocument doc;
  if (deserializeJson(doc, presenceJson) != DeserializationError::Ok) return;
  presColor = presenceColor(String(doc["intent"] | "info"));

  JsonObject data = doc["data"];
  if (data.isNull()) return;

  if (!data["progress"].isNull()) {
    presProgress = constrain((float)(data["progress"] | 0.0f), 0.0f, 1.0f);
    presShape = PRES_PROGRESS;
    return;
  }

  if (data["values"].is<JsonArray>()) {
    for (JsonVariant v : data["values"].as<JsonArray>()) {
      if (presValueCount >= 3) break;
      presValues[presValueCount++] = (float)(v["value"] | 0.0f);
    }
    if (presValueCount > 0) presShape = PRES_VALUES;
    return;
  }

  if (data["series"].is<JsonArray>()) {
    float tmp[32];
    int n = 0;
    for (JsonVariant v : data["series"].as<JsonArray>()) {
      if (n >= 32) break;
      tmp[n++] = v.as<float>();
    }
    int take  = min(n, (int)MATRIX_W);   // last <=8 points
    int start = n - take;
    if (take <= 0) return;
    float mn = tmp[start], mx = tmp[start];
    for (int i = start; i < n; i++) { mn = min(mn, tmp[i]); mx = max(mx, tmp[i]); }
    presSeriesCount = (uint8_t)take;
    for (int i = 0; i < take; i++) {
      uint8_t h;
      if (mx > mn) h = (uint8_t)(1 + lroundf((tmp[start + i] - mn) / (mx - mn) * 7.0f));
      else         h = 4;   // flat series → mid-height line
      presSeries[i] = (uint8_t)constrain((int)h, 1, (int)MATRIX_H);
    }
    presShape = PRES_SERIES;
    return;
  }
}

// Draw an integer big + vertically centered (3×5); scroll it if too wide for 8px.
// Shared by the progress (percent) and values (readout) shapes — numbers read on
// the 8×8 where solid fills don't.
static void drawBigNumber(long n, CRGB color, uint32_t now) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%ld", n);
  int len = (int)strlen(buf);
  // Width = len*3px + (len-1)*1px gap = 4*len-1. Hardcoded (not FONT_CHAR_W/GAP):
  // those macros live in fonts.ino, concatenated AFTER this file, so undefined here.
  int wpx = 4 * len - 1;
  if (wpx <= MATRIX_W) {
    drawStrCentered3x5(buf, 1, color);   // static, vertically centered (rows 1–5)
  } else {
    if (now - presScrollLastMs >= 60) {  // advance the scroll
      presScrollLastMs = now;
      presScrollX -= 1;
      if (presScrollX < -wpx) presScrollX = MATRIX_W;
    }
    drawStr3x5(buf, presScrollX, 1, color);
  }
}

// Draw the cached presence data. Clears the panel; the loop calls FastLED.show().
void runPresenceFrame() {
  FastLED.clear();
  uint32_t now = millis();

  switch (presShape) {
    case PRES_PROGRESS:
      // Show the percent as a number (a solid fill doesn't read as "progress" on 8×8).
      drawBigNumber(lroundf(presProgress * 100.0f), presColor, now);
      break;

    case PRES_SERIES: {
      // Thin-line sparkline: light ONLY the top pixel of each column (the value),
      // not the fill beneath — so a monotonic series reads as a rising LINE, not a
      // solid triangle, and a jagged one reads as a chart.
      int x0 = (MATRIX_W - presSeriesCount) / 2;   // center the columns
      for (int i = 0; i < presSeriesCount; i++)
        setPixel(x0 + i, MATRIX_H - presSeries[i], presColor);   // top pixel = trace point
      break;
    }

    case PRES_VALUES:
      if (presValueCount > 1 && now - presValueLastMs >= 1800) {
        presValueLastMs = now;
        presValueIdx = (presValueIdx + 1) % presValueCount;
        presScrollX = MATRIX_W;   // restart scroll for the newly shown number
      }
      drawBigNumber(lroundf(presValues[presValueIdx]), presColor, now);
      break;

    case PRES_NONE:
    default:
      break;   // nothing to draw
  }
}
