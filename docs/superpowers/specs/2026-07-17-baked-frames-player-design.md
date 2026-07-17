# Baked Frames Player (.cfr) + Expression Gallery, Design Spec

**Date:** 2026-07-17
**Status:** Approved for planning
**Author:** the user + Claude (brainstorm)

## Summary

Put the studio's whole animation library (86 baked animations, 142 KB) on the board
itself, so a standalone gift board carries every expression with no computer, no
studio, and no network. Firmware side of the two-repo feature: the studio's exporter
(merged there as PR #22) bakes each animation to an indexed-color `.cfr` file; this
spec adds the firmware LOADER plus a web gallery page. Approach A from the
brainstorm: the board already has a frames playback engine (`anim_frames.ino`,
`stepFramesFrame()`) with loop semantics identical to `.cfr` (0 = loop forever,
N = play N times then hold the last frame), so the new code is a file decoder that
fills the existing machinery, not a new player.

Surface decision (user): gallery page + API only. No idle-rotation integration and
no shuffle mode in this change (both parked as future options).

## Contract consumed

`claude-expression-studio/docs/frames-file-format.md` (.cfr v1, merged): 12-byte
little-endian header (magic `CFRM`, version 1, loop count, u16 frame_count, u16
frame_ms, u16 palette_size), then 3 x palette_size RGB bytes, then one palette
index byte per pixel, 64 per frame, row-major y*8+x, NOT serpentine. Colors are
logical RGB with the same semantics as `POST /api/display/frames`. Sidecar
`index.json` lists every animation with metadata.

## Goals

- Every baked animation playable on demand: `POST /api/animation`
  `{"type":"baked","name":"aurora"}`, plus an optional `hue` shift (0-255).
- A gallery page listing all 86 from `index.json`, tap to play, on the animations
  hub as ONE new card.
- Survives standalone life: auto-resume replays the last baked animation after a
  power cycle; play-once files (e.g. `done`) hold their last frame, matching the
  wire channel's behavior.
- A bad name or corrupt file returns 400 and leaves the current display untouched
  (same `applyAnimationBody` contract as every other type).
- Docs in the same change; version bump to 0.14.0.

## Non-Goals (parked)

- Baked animations in the idle screensaver rotation, and a whole-library shuffle
  mode. Both are natural follow-ups; neither ships here.
- Palette-swap presets beyond the single `hue` shift.
- Any exporter/studio changes (the exporter is already merged; this repo only
  consumes its output).
- Regenerating bakes on the board. Files are static assets.

---

## Design

### 1. Assets: `data/frames/`

The 86 `.cfr` files plus `index.json` are COMMITTED to this repo under
`esp32_matrix_webserver/data/frames/` (146 KB). They ride the normal LittleFS
upload and the merged release binary automatically. LittleFS totals ~470 KB of the
1 MB region after this change.

Refresh workflow is manual and documented (README): run `npm run export:frames` in
the studio repo, then copy `frames-out/` over `data/frames/` (one command, exact
incantation in the README), then LittleFS upload. No build automation.

### 2. Playback buffer: grow and move to PSRAM

`framesBuf` (main ino, currently `CRGB framesBuf[24 * 64]`, 4.6 KB of internal
DRAM) becomes a pointer allocated once in `setup()` via `ps_malloc` (30.7 KB of
PSRAM; `ps_malloc` rather than `EXT_RAM_ATTR` because the Arduino core does not
reliably enable the PSRAM-BSS segment, and a failed alloc can fall back to
internal heap with a logged warning instead of failing to link or boot):
`MAX_PLAY_FRAMES` 24 -> 160, covering the largest bake (fire, a 150-frame
6-second RNG window capture) with headroom. This FREES 4.6 KB of the contended internal DRAM. PSRAM Enabled is
already a hard requirement of this firmware (CLAUDE.md board settings). The wire
channel `POST /api/display/frames` keeps its existing 24-frame REQUEST cap: its
public contract does not change; 160 is buffer capacity, not a new wire limit.

### 3. Loader: `type:"baked"` in `applyAnimationBody`

New branch parsed like every other animation type:

- Params: `name` (required), `hue` (optional int 0-255, default 0), plus the
  standard `transient` flag.
- Name is sanitized to `[a-z0-9_-]` only and rejected otherwise (path traversal
  guard); file path is `/frames/<name>.cfr`.
- Load sequence: open file, validate magic `CFRM`, version 1, frame_count 1..160,
  palette_size 1..256, and exact expected file length; read palette into a local
  `CRGB[256]`; if `hue` is nonzero, rotate each palette entry's hue by that amount
  (FastLED `rgb2hsv_approximate` -> add -> `CHSV` back; approximation is fine at
  panel scale); expand index bytes into `framesBuf`; set `framesCount`,
  `framesLoops` (the file's loop byte), `framesIdx = framesPlayed = 0`,
  `animationSpeed = frame_ms` (existing 10..10000 clamp), `animationName =
  "frames"`. Playback proceeds through the untouched `stepFramesFrame()` path:
  brightness, calibration correction, and COLOR_ORDER apply as usual.
- Any validation failure returns false BEFORE `stopAll()`, so the request 400s and
  the board keeps showing what it was.
- `KNOWN_ANIMS` gains `"baked"`.
- A new `bakedName` global (set on successful load, cleared by other launches)
  is reported by `GET /api/status` as `"baked":"<name>"` while a baked animation
  is the active content, so scripts and the gallery can see what is playing.
- Auto-resume needs no new code: `handleAnimation` already persists the request
  body, and boot resume replays it through the same loader. LittleFS is mounted
  before auto-resume in `setup()` (verify during planning; reorder if not).

### 4. Gallery page: `data/gallery.html`

Shared design system (app.css, backnav to the animations hub, header). Fetches
`/frames/index.json`, renders a grid of all 86 names with a small badge (frame
count, and "plays once" when loop != 0). Tap a tile: POST
`{"type":"baked","name":<name>,"hue":<slider>}`. One hue slider (0-255, default 0,
left = no shift per the slider rule) applies to plays started from the page. A
"now playing" line polls nothing: it updates from the POST response only (keep the
page dumb). The animations hub (`data/animations.html`) gets ONE new card,
"Expression Gallery".

### 5. Docs and version (same change, house rule)

- `docs/API.md`: new "Baked frames (.cfr)" section: the `baked` type, params,
  status field, the `/frames/` assets, the refresh workflow, and a pointer to the
  studio's format doc as the canonical contract.
- `CLAUDE.md`: firmware layout line (data/frames assets + gallery.html) and a
  one-line mention under the API section.
- `README.md`: the refresh-the-bakes copy command.
- `npm run bump:minor` -> 0.14.0.

## Edge cases

- `frame_count` > 160 or file length mismatch: reject before touching the display.
  (No current bake exceeds 150; the exporter would have to change for this to
  trigger, and rejection is the correct response to a stale/foreign file.)
- `frame_ms` extremes: clamped by the existing `animationSpeed` constrain
  (10..10000 ms).
- Missing `index.json` or empty `/frames/`: gallery page shows a plain "no baked
  animations installed" message (fetch failure path); the API still 400s cleanly
  per-name. Degraded state is visible, not silent.
- `"baked"` with no/invalid `name`: 400, board untouched.
- Wire-channel interplay: a `POST /api/display/frames` after a baked play simply
  overwrites the buffer through its existing path (last write wins, as today).
- Idle screensaver: `baked` is deliberately NOT in the settings APPS list; if a
  user hand-adds it to `idle_apps`, the loader fails on the missing name and the
  idle engine skips the slot (the launch guard from the idle-random branch).

## Verification (hardware gate)

1. Copy assets, flash firmware + LittleFS upload (both artifacts change).
2. `curl` a baked play (e.g. aurora): 200, `/api/status` shows
   `"animation":"frames"` and `"baked":"aurora"`; framebuffer non-black.
3. Play `done` (loop=1): confirm it holds its last frame.
4. `hue` shift: play the same animation with hue 0 and hue 128, framebuffers
   differ, geometry identical.
5. Bad name / traversal attempt (`../secrets`): 400, display unchanged.
6. Gallery page: loads all 86 tiles, tap plays, hub card present (curl the served
   file + a live browser check).
7. Power-cycle with a baked animation active: auto-resume brings it back.
8. Wire regression: `matrix_express` (studio) still renders normally after the
   buffer move to PSRAM.
9. Heap check via `/api/status` before/after several loads (expect internal DRAM
   to be BETTER than 0.13.0 by ~4.6 KB).
10. Restore board state.
