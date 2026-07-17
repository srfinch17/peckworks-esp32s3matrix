# Board HTTP API

The full HTTP surface the firmware implements. External clients (notably the
[`claude-expression-studio`](https://github.com/srfinch17/claude-expression-studio)
MCP server + hooks) drive the board **only** through these endpoints, no shared code.

Base URL = `ESP32_URL` (default `http://esp32matrix.local`).

```
GET  /api/status            # fw_version, fw_built (__DATE__ __TIME__), web_version;
                            #   + heap telemetry: free_heap/largest_block/min_free_heap/free_psram
                            #   + mqtt_enabled; when enabled also mqtt_connected, mqtt_state
                            #     (PubSubClient code, 0=connected), mqtt_bad_host (host not a numeric IP),
                            #     mqtt_secs_since_publish (-1 = no successful publish yet; read
                            #     alongside mqtt_connected to tell "never connected" from "connected but stalled")
GET  /api/presence          # current PresenceMessage (semantic status for any renderer)
POST /api/presence          { intent, headline? detail? data? urgency? }   # board stamps ts; PURE STORE (no LED render)
GET  /api/sensors/{temperature,accelerometer,weather}
GET  /api/display/framebuffer   # live 8×8 leds[] as 64 "RRGGBB" (row-major), exact mirror for previews
POST /api/display/clear
POST /api/brightness        { level: 0-255 }
POST /api/display/text      { text, color, color2, gradient, small, tiny, scroll_speed }
POST /api/display/animation { type, transient? ...mode-specific }   # transient:true skips NVS auto-resume;
                            #   clock/calendar accept tz (POSIX TZ, DST) or timezone (int offset)
POST /api/display/matrix    { matrix: [[8×8 hex]] }
POST /api/display/frames    { frames: ["384-hex RRGGBB×64", …≤24], frame_ms, loop }   # loop 0=forever, N=passes then hold last
POST /api/weather/mode      { mode: temp|humidity|uv|pressure|cycle }
GET  /api/settings          # all current board settings (NVS-backed)
POST /api/settings          { partial keys }   # merge-update, only sent keys change
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
- **Presence** is a pure store on `POST /api/presence`, it does NOT change the display.
  Glyph/data rendering for a presence is done separately (the client pushes frames, or the
  board renders `data` natively via `anim_presence.ino`).
- `GET /api/display/framebuffer` is the polling source for a remote mirror; note it can be
  slow under load (multi-second), so size client timeouts accordingly.
- Auto-resume, settings, and the idle screensaver are all NVS-backed and survive reflashes.
- **`/api/settings` keys** (partial-merge; only sent keys change): `idle_enabled`, `idle_apps`,
  `idle_after_secs`, `idle_rotate_secs`, `idle_brightness`, `idle_random` (bool, default true:
  each screensaver launch rolls random params and a random brightness 6-8, frostbite 7-8, and
  the `idle_brightness` value is ignored; false runs tuned or page-default params at
  `idle_brightness`; upgraded boards keep their previously stored idle_apps, POST the new
  default list once to adopt the expanded rotation),
  `default_brightness`,
  `boot_animation`, `timezone`, `calibration_correction`, and the MQTT publisher keys
  `mqtt_enabled` (bool, default false), `mqtt_host` (broker LAN IP string, "" = unconfigured),
  `mqtt_port` (int, default 1883), `mqtt_every_secs` (int 1-3600, default 3).

## MQTT publisher (optional, off by default)

When `mqtt_enabled` is true and `mqtt_host` is set, the firmware (`mqtt_publisher.ino`)
connects to that broker and publishes its own sensor readings every `mqtt_every_secs`
seconds. This is the on-board replacement for a PC-side polling bridge; with it off, the
board behaves exactly as before. Payloads are retained, QoS 0:

```
plantfloor/matrix/temperature    {"celsius":<n.1>,"ts":"<ISO8601 UTC>"}
plantfloor/matrix/accelerometer  {"ax":<n.3>,"ay":<n.3>,"az":<n.3>,"ts":"<ISO8601 UTC>"}
plantfloor/status/matrix         {"online":true}   on connect (retained)
                                 {"online":false}  last will, fired by the broker on an
                                                   ungraceful drop (retained)
```

The status/will topic sits **outside** the `plantfloor/matrix/#` subtree on purpose, so a
consumer subscribed to the data wildcard never ingests it. Turning MQTT off (or changing the
broker) publishes a retained `{"online":false}` before disconnecting, so a graceful shutdown
reports the same status the will reports on a crash (a clean disconnect would otherwise discard
the will and leave a stale `online`). The board only publishes once its clock has synced over
NTP, so no reading is stamped with a pre-sync (1970) time. `celsius` is the ESP32 **chip**
temperature, not room temperature.

The payloads match the bridge's topics, keys, and types but are **not byte-identical**: the
board stamps `ts` at seconds precision and formats numbers with fixed decimals (`25.0`, not the
bridge's JS `25`). Both parse the same and neither subscriber is affected. The broker connection
is **unauthenticated and unencrypted** (a trusted LAN Mosquitto, matching the Night 1 setup);
broker username/password and TLS are a later hardening step, not implemented here.

## Baked frames (.cfr)

The board ships the studio's animation library as static assets in `/frames/`
(86 `.cfr` files + `index.json`, ~171 KB of data; note LittleFS stores files in
4 KB blocks so the on-flash cost is larger), baked by the studio repo's
`npm run export:frames`. The canonical format contract is the studio's
`docs/frames-file-format.md` (.cfr v1).

- `POST /api/display/animation` `{"type":"baked","name":"aurora"}` plays one.
  Optional `hue` (0-255) rotates every palette entry around the color wheel at
  load time. `transient:true` skips auto-resume as usual; otherwise the board
  resumes the baked animation after a power cycle.
- Names are `[a-z0-9_-]` only; a bad name or corrupt file returns 400 and the
  display is untouched (exception: a physical flash read fault mid-load blanks
  the frames buffer, stopping any frames playback in progress, rather than
  leaving mixed pixels). Play-once files (loop
  count N in the file) hold their last frame, matching the frames wire channel.
- `GET /api/status` includes `"baked":"<name>"` while a baked animation is
  active.
- The gallery page (`/gallery.html`) lists the library from `/frames/index.json`.
- Refreshing the assets: in the studio repo run `npm run export:frames`, copy
  `frames-out/` into this repo's `esp32_matrix_webserver/data/frames/`, then do
  a LittleFS upload.
- The board setting boot_animation cannot name a baked animation (it has no
  name field); to pin a baked animation across power cycles, play it once
  without transient:true and auto-resume will restore it on boot.
