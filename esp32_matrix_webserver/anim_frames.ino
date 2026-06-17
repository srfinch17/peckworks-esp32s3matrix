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
