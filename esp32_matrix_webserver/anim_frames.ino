// ============================================================
// SECTION: FRAMES PLAYER — Claude's expression channel
//
// Plays a short frame sequence uploaded via POST /api/display/frames.
// This is the transport behind the MCP server's expression tools: canned
// glyphs (smiley, check, alert…) and Claude-drawn custom animations.
// Spec: docs/superpowers/specs/2026-06-11-claude-expression-display.md
//
// Globals (defined in the main sketch — see PITFALLS on .ino concat order):
//   framesBuf[MAX_PLAY_FRAMES * 64]  pixel data, row-major per frame
//   framesCount / framesIdx          how many frames / which is next
//   framesLoops / framesPlayed       0 = loop forever; N = N passes then HOLD
//
// The dispatch tick in loop() gates on animationSpeed (set to the request's
// frame_ms by handleFrames), so this just advances one frame per call.
// ============================================================

void stepFramesFrame() {
  if (framesCount == 0) return;

  // Finished the requested passes — hold the LAST frame indefinitely.
  // This is how "blink twice, then show the checkmark" works (loop=N).
  if (framesLoops > 0 && framesPlayed >= framesLoops) {
    memcpy(leds, &framesBuf[(framesCount - 1) * 64], sizeof(CRGB) * 64);
    return;
  }

  memcpy(leds, &framesBuf[framesIdx * 64], sizeof(CRGB) * 64);
  framesIdx++;
  if (framesIdx >= framesCount) {
    framesIdx = 0;
    if (framesLoops > 0) framesPlayed++;
  }
}

// Load /frames/<name>.cfr into framesBuf. Format: the studio repo's
// docs/frames-file-format.md (.cfr v1), the canonical contract. Returns false
// on ANY validation failure. File length is validated up front, so a partial
// overwrite of framesBuf mid-read can only happen on a physical FS error.
bool loadCfr(const String& name, uint8_t hueShift,
             uint16_t& outCount, uint16_t& outMs, uint8_t& outLoops) {
  if (!framesBuf) return false;
  if (name.length() == 0 || name.length() > 48) return false;
  for (unsigned int i = 0; i < name.length(); i++) {
    char c = name.charAt(i);
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;   // path-traversal guard: [a-z0-9_-] only
  }
  File f = LittleFS.open("/frames/" + name + ".cfr", "r");
  if (!f) return false;
  uint8_t hdr[12];
  if (f.read(hdr, 12) != 12)                     { f.close(); return false; }
  if (memcmp(hdr, "CFRM", 4) != 0 || hdr[4] != 1){ f.close(); return false; }
  uint8_t  loops   = hdr[5];
  uint16_t fcount  = (uint16_t)hdr[6]  | ((uint16_t)hdr[7]  << 8);
  uint16_t fms     = (uint16_t)hdr[8]  | ((uint16_t)hdr[9]  << 8);
  uint16_t palSize = (uint16_t)hdr[10] | ((uint16_t)hdr[11] << 8);
  if (fcount < 1 || fcount > MAX_PLAY_FRAMES)    { f.close(); return false; }
  if (palSize < 1 || palSize > 256)              { f.close(); return false; }
  if (f.size() != (size_t)12 + 3UL * palSize + 64UL * fcount) { f.close(); return false; }
  CRGB pal[256];
  for (uint16_t p = 0; p < palSize; p++) {
    uint8_t rgb[3];
    if (f.read(rgb, 3) != 3) { f.close(); return false; }
    CRGB c(rgb[0], rgb[1], rgb[2]);
    if (hueShift) {
      CHSV h = rgb2hsv_approximate(c);   // approximation is invisible at 8x8 scale
      h.hue += hueShift;                 // uint8 wrap = color-wheel wrap
      c = h;
    }
    pal[p] = c;
  }
  uint8_t idx[64];
  for (uint16_t fr = 0; fr < fcount; fr++) {
    if (f.read(idx, 64) != 64) { f.close(); return false; }
    for (int p = 0; p < 64; p++) {
      framesBuf[fr * 64 + p] = (idx[p] < palSize) ? pal[idx[p]] : CRGB::Black;
    }
  }
  f.close();
  outCount = fcount; outMs = fms; outLoops = loops;
  return true;
}
