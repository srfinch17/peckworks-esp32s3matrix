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

// Load <name>'s frame data from /frames/library.cfrpack, the studio's baked
// animation archive (one file holding every .cfr blob, looked up by a name
// table). Format: the studio repo's docs/frames-file-format.md (.cfr v1 blob
// layout + .cfrpack v1 archive layout), the canonical contract. Returns false
// on ANY validation failure. Pack/table/entry bounds are validated up front,
// so a partial overwrite of framesBuf mid-read can only happen on a physical
// FS error.
bool loadCfr(const String& name, uint8_t hueShift,
             uint16_t& outCount, uint16_t& outMs, uint8_t& outLoops) {
  if (!framesBuf) return false;
  if (name.length() == 0 || name.length() > 48) return false;
  for (unsigned int i = 0; i < name.length(); i++) {
    char c = name.charAt(i);
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;   // path-traversal guard: [a-z0-9_-] only
  }

  File f = LittleFS.open("/frames/library.cfrpack", "r");
  if (!f) return false;

  // ---- pack header (8 bytes): magic 'CFRP', version, reserved, count u16 ----
  uint8_t phdr[8];
  if (f.read(phdr, 8) != 8)                         { f.close(); return false; }
  if (memcmp(phdr, "CFRP", 4) != 0 || phdr[4] != 1) { f.close(); return false; }
  uint16_t count = (uint16_t)phdr[6] | ((uint16_t)phdr[7] << 8);
  if (count < 1 || count > 1024)                    { f.close(); return false; }
  uint32_t tableBytes = 40UL * count;
  uint32_t fsize = (uint32_t)f.size();
  if (8UL + tableBytes > fsize)                     { f.close(); return false; }

  // ---- linear-scan the table for `name` (up to 1024 entries; the shipped
  // library is ~71, so linear is fine). Each entry: name[32] zero-padded,
  // offset u32, length u32. Name compare is NUL-terminated-string semantics. ----
  uint32_t entryOffset = 0, entryLength = 0;
  bool found = false;
  uint8_t entry[40];
  for (uint16_t i = 0; i < count && !found; i++) {
    if (f.read(entry, 40) != 40) { f.close(); return false; }
    size_t nlen = 0;
    while (nlen < 32 && entry[nlen] != 0) nlen++;
    if (nlen == name.length() && memcmp(entry, name.c_str(), nlen) == 0) {
      entryOffset = (uint32_t)entry[32] | ((uint32_t)entry[33] << 8) |
                    ((uint32_t)entry[34] << 16) | ((uint32_t)entry[35] << 24);
      entryLength = (uint32_t)entry[36] | ((uint32_t)entry[37] << 8) |
                    ((uint32_t)entry[38] << 16) | ((uint32_t)entry[39] << 24);
      found = true;
    }
  }
  if (!found) { f.close(); return false; }

  // ---- validate the entry against the pack file, overflow-safe (32-bit) ----
  if (entryOffset < 8UL + tableBytes)                           { f.close(); return false; }
  if (entryLength < 12UL)                                       { f.close(); return false; }
  if (entryOffset > fsize || entryLength > fsize - entryOffset) { f.close(); return false; }
  if (!f.seek(entryOffset))                                     { f.close(); return false; }

  // ---- from here down: the existing .cfr v1 blob validation/decode,
  // unchanged, except the length check compares against the table's
  // `entryLength` instead of f.size() (the blob is embedded in a larger file). ----
  uint8_t hdr[12];
  if (f.read(hdr, 12) != 12)                     { f.close(); return false; }
  if (memcmp(hdr, "CFRM", 4) != 0 || hdr[4] != 1){ f.close(); return false; }
  uint8_t  loops   = hdr[5];
  uint16_t fcount  = (uint16_t)hdr[6]  | ((uint16_t)hdr[7]  << 8);
  uint16_t fms     = (uint16_t)hdr[8]  | ((uint16_t)hdr[9]  << 8);
  uint16_t palSize = (uint16_t)hdr[10] | ((uint16_t)hdr[11] << 8);
  if (fcount < 1 || fcount > MAX_PLAY_FRAMES)    { f.close(); return false; }
  if (palSize < 1 || palSize > 256)              { f.close(); return false; }
  if (12UL + 3UL * palSize + 64UL * fcount != entryLength) { f.close(); return false; }
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
    if (f.read(idx, 64) != 64) {
      // Physical FS fault mid-expand: frames already written are stale-new mixed.
      // Blank beats corrupt: zero the buffer and stop any current frames playback.
      memset(framesBuf, 0, sizeof(CRGB) * MAX_PLAY_FRAMES * 64);
      framesCount = 0;
      f.close(); return false;
    }
    for (int p = 0; p < 64; p++) {
      framesBuf[fr * 64 + p] = (idx[p] < palSize) ? pal[idx[p]] : CRGB::Black;
    }
  }
  f.close();
  outCount = fcount; outMs = fms; outLoops = loops;
  return true;
}
