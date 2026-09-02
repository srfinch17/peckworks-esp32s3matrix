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
- **Porting someone else's code?** Reproduce their license notice verbatim at the top of the
  `.ino` AND add a line to the repo `LICENSE`'s third-party section (`anim_liquid2.ino` is the
  precedent). Commit the LICENSE file; "present on disk" fooled six reviews once.
- **Lock the NAME before the PR.** Once merged, `<name>` lives in every board's NVS
  auto-resume body, the MCP enum, two name-guard mirrors and the manifest: a contract.
  Pre-merge a rename is a grep (fluid2 became liquid2 for free); post-merge it is a
  cross-repo breaking change. If a display name and the id differ, make them match.
- **Settings-dependent cost?** Ship a `micros()` mean/worst Serial line every 5 s while the
  animation runs. It is the only profiler this board has and it settled liquid2's
  "choppy" report in one paste (the page poller, not the solver).

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
   next animation compile first try.) **Since 2026-09-01 you can COMPILE yourself:** run
   the `arduino-cli` gate from the `flash-and-verify` skill (section 0a) before asking for
   any upload. Also measured on this toolchain: `min`/`max` are `std::` templates (both
   args identically typed or it will not compile), every float literal needs `f` (no double
   FPU), pragmas leak to later files, ArduinoJson int defaults silently drop fractional
   values. All in `docs/PITFALLS.md` under 2026-09-01.

2. **`speed` is milliseconds-per-frame, NOT a 1-5 scale.** The firmware reads
   `animationSpeed = constrain(doc["speed"] | 66, 10, 10000)` (ms/frame). The MCP tool
   maps a human 1-5 to ms via `msMap = {1:150, 2:100, 3:66, 4:40, 5:20}`. **Your control
   page MUST do the same mapping before POST**, posting a raw `2` becomes 2ms→clamped
   to 10ms ≈ 100fps (a blizzard). This exact bug has shipped twice. Copy the `MS` table
   into the page's JS and send `speed: MS[sliderValue]`. **If your animation is a
   fixed-timestep simulation** (its speed is physics, not a frame delay), clamp
   `animationSpeed = min(animationSpeed, (uint32_t)<budget>)` in your param block, have the
   page NOT send `speed`, and say in the MCP description that it runs at a fixed rate
   (liquid2 does all three).

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
   **Two-colour pickers use `palette.js` (SINGULAR, `Palette.mount(slot, {count:2, labels})`,
   a plain `<script>`, not `data-auto`)**, distinct from `palettes.js` (plural, the DF grid).
   Following the wrong one ships a page with no picker. **No JS twin for the preview?** Do what
   `calendar.html` does: poll `GET /api/display/framebuffer` at 2 fps into a div grid WITH an
   in-flight guard and the `document.hidden` check (a mirror of the REAL pixels; 16 of 17 pages
   have a preview, do not ship the exception). Every live-apply POST reseeds only if your param
   block makes it so: read the params per frame and reseed only on geometry changes, or every
   slider drag resets the animation (liquid2 shipped that bug once).

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
   **Check the emoji is unused:** grep `animations.html` for your icon first. `🌊` was already
   Wave when liquid2 tried to take it; it shipped as `⛲`. Put related animations adjacent.

8. **Studio mirror set** (all in the separate `claude-expression-studio` repo; this is the only
   cross-repo step, and it is SIX files, not one; liquid2's integration audit found them):
   - `mcp_server/index.ts`: `<name>` in the `matrix_set_animation` `type` **enum** (a closed
     list; without it the tool rejects the type before any HTTP call, no firmware workaround),
     a one-line description bullet, and **any NEW param as a schema property** (Claude only
     sends declared properties; the handler forwards whatever arrives). Prefer reusing the
     generic ones that already exist (`color1`/`color2`, `viscosity`, `speed`) by aliasing them
     in your firmware param block; liquid2 needed only one new property that way.
   - `shared/firmware-names.js` + `shared/firmware-names.test.js` (the size assertion, a drift
     guard, must be bumped).
   - `claude-hooks/matrix_signal.py` AND its live deployed copy at `~/.claude/hooks/` (edit
     both or they drift).
   - `studio/firmware-params.js` (typed editor widgets; defaults must match the FIRMWARE's
     bare-POST defaults, not a sibling animation's).
   - Rebuild `dist/` (`npx tsc`), bump that repo's VERSION, `npm test` + `npm run check`, then
     the user reconnects `/mcp`.
   Remember the MCP layer rescales `speed` 1-5 to ms (trap 2).

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
- **Compile gate first** (`flash-and-verify` 0a): never ask for a Sketch upload of code that
  has not compiled locally. Expect one rainbow boot if you renamed or removed an animation
  (stale NVS `animbody`; it heals on the next launch).
