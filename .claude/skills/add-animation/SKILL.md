---
name: add-animation
description: Add a new animation/visualization mode to the ESP32-S3 matrix firmware. Use whenever creating a new animated display mode (a new anim_*.ino) so every wiring-up step is done consistently and nothing is skipped.
---

# Add a new animation mode

> **⚠️ Two-repo + manifest note (2026-06-28).** This repo (`peckworks-esp32s3matrix`) is
> **firmware-only**. Steps 1-7 below (the `anim_*.ino` + `data/*.html`) are all here. But the
> **MCP/Claude wiring lives in the separate `claude-expression-studio` repo** (`mcp_server/`
> `shared/`, `claude-hooks/`). And the old per-config files this skill used to name
> `mcp_server/wait.ts`, `idle.ts`, `wait-weights.json`, **no longer exist**: wait/idle pools
> are now entries in **`shared/manifest.json`** (read at runtime; no rebuild). So step 8 and the
> "Optional" section now mean *edit the studio repo*. See [[trigger-manifest-design]] / [[repo-split]].

Adding an animation touches **8 places** (was "6", two silent-failure spots were
added after they bit us: `KNOWN_ANIMS` and the MCP enum). Skipping any one is the
usual cause of "I built it but it doesn't show up / the page 404s / the API 400s /
Claude can't launch it." Names below assume a mode called `<name>` (e.g. `comet`
`claudesweep`).

## Before you start
- This skill is the **firmware wiring**. For the *look*, legibility at 64px
  brightness-5 color, silhouette/motion craft, use **`emoting-on-8x8`** alongside it.
- Read `CLAUDE.md` (hardware facts) and `docs/PITFALLS.md` (traps).
- **`COLOR_ORDER` is RGB**, `CRGB(r,g,b)` maps straight through.
- Draw only via `setPixel(x, y, CRGB)` (bounds-checked). `XY(x,y)=y*8+x`, row-major
  (NOT serpentine), origin top-left.
- **Non-blocking**: no `delay()`. Use `millis()` + frame-state like the other
  `anim_*.ino`. The dispatcher rate-limits via `animationSpeed`/`lastFrameMs`.

## ⚠️ Three traps that have bitten this codebase repeatedly, internalize before coding

1. **Single-translation-unit ordering** (the #1 trap, see `docs/PITFALLS.md`). All
   `.ino` concatenate (main `esp32_matrix_webserver.ino` FIRST, then alphabetical).
   Arduino auto-prototypes **functions** but NOT **globals / `#define`s / structs**.
   So:
   - Any variable your `handleAnimation` branch or `loop()` dispatch must SEE goes in
     the **main ino** (alongside `solidColor`, `cometColor1`). A global defined in your
     later-sorting `anim_<name>.ino` is invisible to the earlier-concatenated
     `api_handlers.ino`/main `loop()` → compile error.
   - Keep mode-internal state as **file-local `static`** in `anim_<name>.ino`.
   - If `handleAnimation` must reset your animation, call a **non-static function**
     (e.g. `void reset<Name>()`), NOT a file-local static, the function is
     auto-prototyped and cross-file visible; the static is not.
   (This cost two compile-fix cycles on the settings/idle work; pre-empting it made the
   next animation compile first try.)

2. **`speed` is milliseconds-per-frame, NOT a 1-5 scale.** The firmware reads
   `animationSpeed = constrain(doc["speed"] | 66, 10, 10000)` (ms/frame). The MCP tool
   maps a human 1-5 to ms via `msMap = {1:150, 2:100, 3:66, 4:40, 5:20}`. **Your control
   page MUST do the same mapping before POST**, posting a raw `2` becomes 2ms→clamped
   to 10ms ≈ 100fps (a blizzard). This exact bug has shipped twice. Copy the `MS` table
   into the page's JS and send `speed: MS[sliderValue]`.

3. **Brightness-5 floor (only if it'll run as a wait/idle indicator).** Ambient
   indicators render at FastLED global brightness 5, which DOUBLE-scales with your
   per-pixel `nscale8`. A dim "baseline"/trail color must keep its **weakest channel**
   above the visibility threshold or it vanishes / shifts hue at bri 5 (e.g. amber's
   green needs a per-pixel value ≳ 63/255 to survive). Verify via
   `GET /api/display/framebuffer` AND your eyes at bri 5, the framebuffer is pre-global-
   scaling, so it can look fine while the panel reads black. See the LED-brightness-
   formula memory.

## The 8 steps

1. **New file `esp32_matrix_webserver/anim_<name>.ino`**, mode state as file-local
   `static` (NOT cross-file globals, see trap 1); a `run<Name>Frame()` /
   `step<Name>Frame()` that renders ONE frame into `leds[]`. Clear what you need each
   frame. Mirror an existing `anim_*.ino`.

2. **Shared globals → main ino.** Any color/param your handler sets that the frame fn
   reads goes in `esp32_matrix_webserver.ino`'s globals block (e.g. `CRGB <name>Color;`).

3. **Dispatch branch** in `esp32_matrix_webserver.ino` `loop()`, grep `animationName ==`;
   add `else if (animationName == "<name>") step<Name>Frame();`.

4. **Register in `KNOWN_ANIMS`** (api_handlers.ino, ~the string array near the top of the
   handlers). **If you skip this, every `<name>` POST returns 400** ("unknown animation
   type"), the #1 silent failure for a new mode.

5. **HTTP handler** in `api_handlers.ino` → `handleAnimation()`/`applyAnimationBody()`
   parse the mode's params from the JSON body, set the main-ino globals, and (if it has
   internal state to reseed) call your `reset<Name>()` function. `animationName`/
   `animationSpeed` are set by the shared path.

6. **Control page `data/<name>.html`**, clone an existing leaf (e.g. **`rainbow.html`**) for the
   **shared design system (v1.1.0 revamp)**. Structure: `.wrap` (set `--accent-page:#hex` inline) →
   colored emoji `h1` → `.panel` → `.layout` (`.preview-frame` with `<canvas class="preview">` +
   `.controls` of `.subcard`/`.subhead`/`.chips`) → `.actions` (`.btn-primary`/`.btn-secondary`/
   `.live-dot`) → `.status`. Link `app.css` (all chrome/tokens). Load `previews.js`
   (`MatrixPreview.start(canvas,'<type>')`, the canvas engine) and `palettes.js` (`DF_PAL` +
   `buildDfPalGrid(gridEl, onPick, activeIdx)`) if it has a preview/palette. **End-of-body drop-in
   scripts (order matters):** page JS, then `backnav.js data-auto data-parent="/animations.html"
   data-label="Animations"` (renders the breadcrumb), `bright.js data-auto` (self-mounts the
   brightness widget, no manual `#brightnessSlot`), `header.js data-auto` (logo card). **Live-apply
   default-on, debounced ~180ms:** `liveApply(){clearTimeout(t);t=setTimeout(applyAnimation,180)}`.
   **Speed = fps slider → ms** (trap 2). Preview renders at FULL brightness, no `ledsim.js` for
   animation previews. Launch POSTs `{ "type":"<name>", ... }`.

7. **Hub card, in `data/animations.html`, NOT the index.** Post-revamp `animations.html` is a **pure
   `.apps` grid of `.card` link-outs** (every animation has its own page now, no more inline
   `.anim-card`s). Add, same `.card` shape as the index:
   ```html
   <a href="/<name>.html" class="card"><span class="icon">…</span>
     <div class="name">…</div><div class="desc">…</div></a>
   ```
   Do NOT add it to `index.html` (cards placed there get moved, see web-ui-structure).
   System/config pages go in `system.html` instead. ⚠️ The new leaf's `<h1>` must NOT duplicate its
   hub's name, `backnav.js` derives the breadcrumb's current crumb from the `<h1>`, so a leaf titled
   "Animations" would read "Animations › Animations". Give it the mode's own name.

8. **MCP enum**, add `<name>` to the `matrix_set_animation` `type` enum array (+ a one-line
   description) in `mcp_server/index.ts`, **which now lives in the separate
   `claude-expression-studio` repo**. Without this, Claude can't launch it by name. This is the
   only cross-repo step in the core 8 (steps 1-7 are all firmware, in this repo).

## Optional: wire it into the busy/idle pools (now manifest-driven, in the STUDIO repo)
The old `wait.ts`/`idle.ts`/`wait-weights.json` are **gone**. Pools now live in
**`shared/manifest.json`** in the `claude-expression-studio` repo, read at RUNTIME (no rebuild):
- **Idle screensaver (firmware side):** add `<name>` to `IDLE_APPS_DEFAULT` in this repo's
  `settings.ino` (existing boards keep their stored `idle_apps` CSV until toggled in settings).
- **Idle screensaver (studio side):** add `<name>` to the `esp32-8x8` renderer's `screensaver`
  binding in `shared/manifest.json`.
- **Busy/wait pool:** add `{"<name>": <weight>}` to the manifest's `working` intent pool. A
  firmware-animation pick fires `POST /api/display/animation {type, transient:true}`, the
  **`transient` flag skips NVS auto-resume** so a busy launch doesn't make the board boot into
  it forever. `shared/firmware-names.js` (mirrored in the Python hook) must list `<name>` so the
  resolver routes it to the animation path, not the frames path.
- **Web sim / Gallery (optional):** to also see it in the browser studio, add a JS port to
  `shared/firmware-sims.js` (a `make<Name>(opts)→{frame_ms,frame()}` + one registry line). This
  is the **manual cross-repo seam**, firmware `anim_*.ino` and the JS port are independent (see
  [[repo-split]]). All of the above are studio-repo edits.

## Finish
- Needs **both** a Sketch upload (firmware) **and** a **LittleFS Data Upload** (because
  `data/` changed); MCP enum/idle/wait edits need a **`/mcp` reconnect**. Bump the version
  if it's a real feature (`npm run bump:minor`) and redeploy all changed artifacts.
- The controller can drive most verification over HTTP (launch it, read
  `/api/display/framebuffer`); the bri-5 *look* and any persistence/power-cycle test are
  the user's eyes. **Restore the board's prior brightness + display after testing.**
- Do not claim it works until confirmed on hardware. If a non-obvious trap bit us, append
  to `docs/PITFALLS.md`.
