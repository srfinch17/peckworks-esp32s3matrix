# Idle Screensaver: Random Settings + New Rotation, Design Spec

**Date:** 2026-07-17
**Status:** Approved for planning
**Author:** the user + Claude (brainstorm)

## Summary

Two stacked changes to the idle screensaver engine:

1. **New default rotation.** Remove `claudesweep` from the default pool; add
   `fireworks2`, `spiral`, `wave`, `starfield`, and `rainbow`. New default
   (12 apps): `fire, matrix_rain, clock, fireworks, fireworks2, frostbite,
   snow, dancefloor, spiral, wave, starfield, rainbow`. Claudesweep remains
   available as a settings-page checkbox, just not in the default.
2. **Randomize-settings mode.** A new board setting (`idle_random`, default
   on) that makes every screensaver launch roll random parameters: colors,
   themes, speeds, densities, and a random brightness of 6, 7, or 8
   (frostbite: 7 or 8 only). Off restores exactly today's behavior: the
   tuned per-app params in `idleParamsFor()` and the idle brightness slider.

All randomization lives in `idle_engine.ino` (Approach A from the brainstorm).
The animation API, `applyAnimationBody()`, and the animation implementations
are untouched: the idle engine simply builds its launch JSON with random
values instead of hardcoded ones.

## Motivation / Problem

The screensaver launches every app with hardcoded params, so the rotation
always looks the same: fire is always the classic orange palette,
matrix_rain is always `"theme":"classic"` green, and so on. The variety
already exists in the firmware (4 fire palettes, 4 rain themes, 64
dancefloor palettes, arbitrary colors on most apps); the idle engine just
never asks for it. The user wants every screensaver launch to look
different, and wants a richer default pool.

## Goals

- Every random-mode launch picks fresh params so back-to-back appearances
  of the same app look different.
- All rolled colors are visible at brightness 6 to 8 (full-saturation,
  full-value hues only; no dim or pastel rolls that round to black).
- A `Randomize settings` checkbox in the settings page Idle panel. When
  checked, the Idle brightness slider is disabled/grayed (the board rolls
  6 to 8 instead).
- Random off = byte-for-byte today's launch behavior.
- Docs updated in the same change (`docs/API.md` settings keys, CLAUDE.md
  settings line).

## Non-Goals

- No `"random":true` flag on the public animation API (Approach B). Can be
  layered later if random-on-demand is ever wanted.
- No changes to the studio repo. This intentionally breaks the informal
  "screensaver looks the same as `matrix_idle`" alignment with
  `mcp_server/idle.ts`: the screensaver becomes random, the on-demand tool
  stays tuned. The `Keep aligned` comments in `idle_engine.ino` and
  `settings.ino` are updated to say so.
- No per-app enable/disable of randomization. One global switch.

---

## Design

### 1. Setting

| Piece | Value |
|---|---|
| NVS key | `idle_rand` (bool) |
| JSON key | `idle_random` |
| Default | `true` |

Wired through the standard settings plumbing in `settings.ino`:
`loadSettings()` (per-key default pattern), `saveSettings()`,
`settingsToJson()`, `applySettingsJson()`.

`IDLE_APPS_DEFAULT` in `settings.ino` becomes the new 12-app CSV. Existing
boards keep their stored CSV (NVS merge-on-boot only adds missing keys);
after flashing, the user's board gets a one-time `POST /api/settings` with
the new list rather than hand-clicking checkboxes.

### 2. Settings page (`data/settings.html`)

- `APPS` array: the 12 new defaults plus `claudesweep` (13 checkboxes
  total), so claudesweep stays selectable.
- New row in the Idle panel: `Randomize settings` checkbox, id
  `idle_random`, loaded/saved with the other idle keys. Checked by default
  (the setting defaults on, and the page reflects board state).
- When checked, the Idle brightness slider (and its label/output) is
  disabled and visually dimmed. Unchecking re-enables (unlocks) the slider
  live, not just on save.
- Claudesweep's checkbox is unchecked by default (it is not in the default
  `idle_apps` CSV; the page checks only apps present in the stored list).

### 3. Idle engine (`idle_engine.ino`)

`idleLaunch(type)` branches on `settings.idleRandom`:

- **Off:** current path, unchanged. `FastLED.setBrightness(settings.idleBri)`
  and `idleParamsFor(type)`. New apps have no entry there and launch with
  the animation API defaults (which match the web pages' defaults).
- **On:** brightness = random 6, 7, or 8 (frostbite: 7 or 8), and the JSON
  body comes from a new `idleRandomParamsFor(type)`.

Color helper: pick a random base hue and convert `CHSV(hue, 255, 255)` to
a `#rrggbb` string. Multi-color apps do not roll independently: the first
color gets the base hue and the rest are offset around the wheel (roughly
thirds for 3-color apps, roughly a triad apart for 2-color apps, with a little
jitter) so rolls are always mutually distinguishable. ESP32 `random()` is
hardware-backed, no seeding needed.

Per-app rolls:

| App | Random roll per launch |
|---|---|
| fire | palette from {classic, blue, green, purple}; intensity 4-10; sparks 0-10; tendrils 0-10; speed 30-90 ms/frame |
| matrix_rain | theme from {classic, blue, red, purple}; speed 40-90 |
| snow | confetti coin-flip; speed 80-140 (non-confetti already rolls its own hue in `anim_snow`) |
| fireworks | 3 spread hues (color1/2/3) |
| fireworks2 | 3 spread hues (color1/2/3) |
| frostbite | 1 hue (color); sparkle 5-40; mist 2-8; brightness 7-8 |
| dancefloor | palette 0-63; hold 4-12 |
| spiral | 2 spread hues |
| wave | 1 hue: crest at full value, trough same hue dimmed (value about 90; 40 went sub-threshold at brightness 6-8) so it still reads as water |
| starfield | 2 spread hues; density 4-12; inward coin-flip |
| rainbow | coin-flip: classic wheel, or `usePalette` with 4 spread hues |
| clock | 3 spread hues (hours/minutes/colon); timezone handling unchanged |
| claudesweep (if user re-enables) | 1 hue |

Exact numeric ranges above are starting values, tunable after the first
on-hardware look.

### 4. Docs (same change, house rule)

- `docs/API.md`: add `idle_random` to the settings keys table.
- `CLAUDE.md`: add `idle_random` to the settings keys line.
- Update the two `Keep aligned with mcp_server/idle.ts` comments to note
  the alignment now only applies to random-off mode.

## Edge cases

- `idlePickType()` caps the pool at 16 entries; the new default is 12, and
  13 with claudesweep re-enabled. Fine, but noted.
- Repeated clock launches re-init NTP (`ntpSynced = false`); already true
  of today's rotation, unchanged by this design.
- Idle launches already skip auto-resume persistence and the brightness
  NVS write; the random path inherits that by using the same
  `applyAnimationBody()` call.
- A user's stored `idle_apps` CSV may contain apps with no random entry
  (e.g. a future app). `idleRandomParamsFor()` returns an empty string for
  unknown types: the app launches with API defaults at the rolled
  brightness. Fail-safe, never fail-closed.

## Verification

1. User compiles + flashes firmware, uploads LittleFS (settings.html
   changed): two upload steps.
2. Push the new `idle_apps` CSV to the board via `POST /api/settings`.
3. Settings page: checkbox present, slider grays out when checked
   (verify via curl of the board-served file plus a live browser check).
4. Force the screensaver quickly: temporarily set `idle_after_secs` low,
   `POST /api/idle/arm`, wait. Confirm rotation starts.
5. Observe several rotations (or temporarily lower `idle_rotate_secs`):
   confirm params differ launch to launch via `/api/status` (animation
   name), the framebuffer endpoint (colors actually differ), and the LEDs.
6. Toggle random off: confirm tuned params return (fire is classic orange
   again) and the slider brightness is honored.
7. Restore real timer values + board state when done (house rule).
