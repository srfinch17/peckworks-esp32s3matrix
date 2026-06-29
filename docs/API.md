# Board HTTP API

The full HTTP surface the firmware implements. External clients (notably the
[`claude-expression-studio`](https://github.com/srfinch17/claude-expression-studio)
MCP server + hooks) drive the board **only** through these endpoints — no shared code.

Base URL = `ESP32_URL` (default `http://esp32matrix.local`).

```
GET  /api/status            # fw_version, fw_built (__DATE__ __TIME__), web_version;
                            #   + heap telemetry: free_heap/largest_block/min_free_heap/free_psram
GET  /api/presence          # current PresenceMessage (semantic status for any renderer)
POST /api/presence          { intent, headline?, detail?, data?, urgency? }   # board stamps ts; PURE STORE (no LED render)
GET  /api/sensors/{temperature,accelerometer,weather}
GET  /api/display/framebuffer   # live 8×8 leds[] as 64 "RRGGBB" (row-major) — exact mirror for previews
POST /api/display/clear
POST /api/brightness        { level: 0-255 }
POST /api/display/text      { text, color, color2, gradient, small, tiny, scroll_speed }
POST /api/display/animation { type, transient?, ...mode-specific }   # transient:true skips NVS auto-resume;
                            #   clock/calendar accept tz (POSIX TZ, DST) or timezone (int offset)
POST /api/display/matrix    { matrix: [[8×8 hex]] }
POST /api/display/frames    { frames: ["384-hex RRGGBB×64", …≤24], frame_ms, loop }   # loop 0=forever, N=passes then hold last
POST /api/weather/mode      { mode: temp|humidity|uv|pressure|cycle }
GET  /api/settings          # all current board settings (NVS-backed)
POST /api/settings          { partial keys }   # merge-update — only sent keys change
POST /api/idle/arm          # arm the idle screensaver countdown
GET  /api/calibration       # the measured LED calibration profile (calibration.json) or identity defaults
POST /api/calibration       # overwrite calibration.json (validated) AND live-reload the correction (no reflash)
POST /api/grid-test/set     { mode, brightness, ... }   # Calibration Lab patterns: ramp/sweep/patch/gamma/pixel
```

## Notes for clients

- **Frame-expression channel** = `POST /api/display/frames` (24-frame max, each frame 64
  `RRGGBB`). **Firmware-animation channel** = `POST /api/display/animation` (named built-in
  animations; pass `transient:true` so a busy-spinner doesn't overwrite the user's last
  animation in NVS auto-resume).
- **Presence** is a pure store on `POST /api/presence` — it does NOT change the display.
  Glyph/data rendering for a presence is done separately (the client pushes frames, or the
  board renders `data` natively via `anim_presence.ino`).
- `GET /api/display/framebuffer` is the polling source for a remote mirror; note it can be
  slow under load (multi-second), so size client timeouts accordingly.
- Auto-resume, settings, and the idle screensaver are all NVS-backed and survive reflashes.
