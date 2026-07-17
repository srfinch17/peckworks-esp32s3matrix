# Baked Frames Player + Expression Gallery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Play the studio's 86 baked `.cfr` animations from LittleFS via `POST /api/display/animation {"type":"baked","name":...}` plus a gallery web page, reusing the existing frames playback engine.

**Architecture:** The board's `stepFramesFrame()` already plays a CRGB buffer with .cfr's exact loop semantics. New code is a validating file decoder (`loadCfr` in `anim_frames.ino`) that fills that buffer, a `baked` branch in `applyAnimationBody()`, and one static gallery page. The frame buffer grows 24 to 96 frames and moves to PSRAM (freeing 4.6 KB internal DRAM); the wire channel keeps its 24-frame request cap via a new separate define.

**Tech Stack:** Arduino C++ (single translation unit), LittleFS, FastLED (`rgb2hsv_approximate`), vanilla JS page on the shared design system.

**Spec:** `docs/superpowers/specs/2026-07-17-baked-frames-player-design.md`
**Format contract:** `claude-expression-studio/docs/frames-file-format.md` (.cfr v1, merged there as PR #22)

## Global Constraints

- **No em-dashes or en-dashes** in any prose, comment, doc, or commit message (house hard rule). The middle-dot and plain hyphens are fine.
- **Claude/agents cannot compile or flash.** Verification inside tasks is careful self-review; the hardware gate is Task 5. Never claim it works before the user confirms.
- **Single translation unit:** all `.ino` files concatenate alphabetically after the main sketch. `loadCfr` is DEFINED in `anim_frames.ino` (concatenates before `api_handlers.ino`, its only caller), so no prototype is needed. No C++ default arguments on `.ino` free functions.
- **.cfr v1 header (little-endian):** offset 0: magic `CFRM` (4 bytes); 4: version `1` (1 byte); 5: loop count, 0 = loop forever, N = play N times then hold last frame (1 byte); 6: frame_count u16; 8: frame_ms u16; 10: palette_size u16; then 3 x palette_size RGB bytes; then 64 index bytes per frame, row-major y*8+x, NOT serpentine.
- **index.json shape:** `{ "format": {...}, "totalBytes": N, "animations": [ { "name", "source", "file", "frames", "frame_ms", "loop", "palette_size", "distinct_colors", "quantized", "bytes", "loop_note" } ] }`, animations sorted by name.
- **Name guard:** `[a-z0-9_-]` only, length 1-48; anything else rejected (path traversal).
- **Wire contract unchanged:** `POST /api/display/frames` keeps its 24-frame request cap.
- **Privacy:** never the maintainer's real name.
- Largest current bake is 84 frames (claudesweep); buffer capacity is 96.

---

### Task 1: Assets import + buffer to PSRAM + wire-cap split

**Files:**
- Create: `esp32_matrix_webserver/data/frames/` (86 `.cfr` + `index.json`, copied from the studio exporter)
- Modify: `esp32_matrix_webserver/esp32_matrix_webserver.ino:328-333` (defines + buffer + globals) and `setup()` (allocation)
- Modify: `esp32_matrix_webserver/api_handlers.ino:521,542-543` (wire cap uses new define)

**Interfaces:**
- Produces: `MAX_PLAY_FRAMES` = 96 (buffer capacity), `MAX_WIRE_FRAMES` = 24 (wire request cap), `CRGB* framesBuf` (PSRAM-allocated in setup, may be null only if both allocs fail), `String bakedName` global. Task 2 writes `framesBuf`/`bakedName`; Task 3 fetches `/frames/index.json`.

- [ ] **Step 1: Export and copy the assets**

```bash
cd /c/Users/srfin/Dropbox/Dev/repos/claude-expression-studio && npm run export:frames
mkdir -p /c/Users/srfin/Dropbox/Dev/repos/peckworks-esp32s3matrix/esp32_matrix_webserver/data/frames
cp frames-out/*.cfr frames-out/index.json /c/Users/srfin/Dropbox/Dev/repos/peckworks-esp32s3matrix/esp32_matrix_webserver/data/frames/
ls /c/Users/srfin/Dropbox/Dev/repos/peckworks-esp32s3matrix/esp32_matrix_webserver/data/frames | wc -l
du -sb /c/Users/srfin/Dropbox/Dev/repos/peckworks-esp32s3matrix/esp32_matrix_webserver/data/frames
```

Expected: exporter prints `exported 86 animations`; file count 87 (86 .cfr + index.json); size roughly 150000 bytes. If the exporter fails or counts differ, STOP and report BLOCKED.

- [ ] **Step 2: Buffer defines and globals in the main ino**

Replace lines 328-333:

```cpp
#define MAX_PLAY_FRAMES 24
CRGB     framesBuf[MAX_PLAY_FRAMES * 64];   // 24 frames × 64 px ≈ 4.6KB static
uint8_t  framesCount  = 0;    // frames loaded
uint16_t framesLoops  = 0;    // 0 = loop forever; N = play N passes then HOLD the last frame
uint16_t framesPlayed = 0;    // completed passes
uint8_t  framesIdx    = 0;    // next frame to show
```

with:

```cpp
#define MAX_PLAY_FRAMES 96    // buffer capacity: covers the largest .cfr bake (84) with headroom
#define MAX_WIRE_FRAMES 24    // POST /api/display/frames request cap (public contract, unchanged)
CRGB*    framesBuf = nullptr; // 96 frames × 64 px ≈ 18KB, allocated from PSRAM in setup()
uint8_t  framesCount  = 0;    // frames loaded
uint16_t framesLoops  = 0;    // 0 = loop forever; N = play N passes then HOLD the last frame
uint16_t framesPlayed = 0;    // completed passes
uint8_t  framesIdx    = 0;    // next frame to show
String   bakedName    = "";   // name of the active baked .cfr ("" = none); reported by /api/status
```

- [ ] **Step 3: Allocate the buffer in setup()**

In `setup()`, immediately AFTER the `LittleFS.begin(true)` if/else block (ends near line 774), insert:

```cpp
  // Frames playback buffer lives in PSRAM (2MB, mostly idle) instead of the
  // contended internal DRAM. Fallback keeps a misconfigured build booting.
  framesBuf = (CRGB*)ps_malloc(sizeof(CRGB) * MAX_PLAY_FRAMES * 64);
  if (!framesBuf) {
    framesBuf = (CRGB*)malloc(sizeof(CRGB) * MAX_PLAY_FRAMES * 64);
    Serial.println("WARNING: PSRAM alloc failed for framesBuf; using internal heap.");
  }
  if (!framesBuf) Serial.println("ERROR: framesBuf alloc failed; frames/baked playback disabled.");
```

This runs before the boot auto-resume block (line ~859), which may replay a baked animation.

- [ ] **Step 4: Wire cap uses the new define**

In `api_handlers.ino` `handleFrames()`: the comment at line 521 and the check at lines 542-543 reference `MAX_PLAY_FRAMES`. Change BOTH to `MAX_WIRE_FRAMES` (the error message string too), and add a null-buffer guard as the function's first statement:

```cpp
  if (!framesBuf) { sendJson(503, "{\"error\":\"frames buffer unavailable\"}"); return; }
```

- [ ] **Step 5: Self-review the diff**

Check: every `framesBuf` use still compiles as pointer indexing (grep `framesBuf` across the sketch; `sizeof(framesBuf)` would now be pointer size, so verify NO remaining `sizeof(framesBuf)` exists anywhere; `stepFramesFrame` at `anim_frames.ino:24` uses `sizeof(CRGB) * 64`, which is fine); `MAX_WIRE_FRAMES` appears exactly where the wire cap lived; no em-dashes; assets are exactly 87 files.

- [ ] **Step 6: Commit**

```bash
git add esp32_matrix_webserver/data/frames esp32_matrix_webserver/esp32_matrix_webserver.ino esp32_matrix_webserver/api_handlers.ino
git commit -m "feat(frames): ship 86 baked .cfr assets; framesBuf to PSRAM at 96-frame capacity"
```

---

### Task 2: loadCfr decoder + baked animation type + status field

**Files:**
- Modify: `esp32_matrix_webserver/anim_frames.ino` (add `loadCfr` after `stepFramesFrame`)
- Modify: `esp32_matrix_webserver/api_handlers.ino` (KNOWN_ANIMS ~line 135, `applyAnimationBody` ~lines 159/186, `handleFrames` success path, handleStatus line ~762)

**Interfaces:**
- Consumes: `framesBuf`, `MAX_PLAY_FRAMES`, `bakedName` (Task 1); `escapeJson` (api_handlers.ino:20, same TU); FastLED `rgb2hsv_approximate`.
- Produces: `bool loadCfr(const String& name, uint8_t hueShift, uint16_t& outCount, uint16_t& outMs, uint8_t& outLoops)`; animation type `"baked"` accepted by the API; `"baked":"<name>"` in `/api/status`. Task 3's gallery POSTs `{"type":"baked","name","hue"}`.

- [ ] **Step 1: Add loadCfr to anim_frames.ino (after stepFramesFrame)**

```cpp
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
```

- [ ] **Step 2: Accept the type**

In `api_handlers.ino`, add `"baked"` to the `KNOWN_ANIMS` array (line ~135), keeping the existing formatting.

- [ ] **Step 3: Validate + decode BEFORE stopAll**

In `applyAnimationBody()`, directly after the `if (!known) return false;` line (~159), insert:

```cpp
  // Baked frames (.cfr): validate + decode BEFORE stopAll so a bad name or a
  // corrupt/stale file 400s and leaves the board showing what it was.
  uint16_t bakedCount = 0, bakedMs = 0; uint8_t bakedLoops = 0;
  if (reqType == "baked") {
    if (!loadCfr(String(doc["name"] | ""),
                 (uint8_t)constrain((int)(doc["hue"] | 0), 0, 255),
                 bakedCount, bakedMs, bakedLoops)) return false;
  }
```

- [ ] **Step 4: Hand the decode to the frames engine**

Directly after the shared-state reset block (the line `initSparks();`, ~186), insert:

```cpp
  // Baked frames: hand the decoded file to the existing frames playback engine.
  if (animationName == "baked") {
    bakedName      = String(doc["name"] | "");
    animationName  = "frames";        // dispatches to stepFramesFrame()
    framesCount    = (uint8_t)bakedCount;
    framesLoops    = bakedLoops;
    framesIdx      = 0;
    framesPlayed   = 0;
    animationSpeed = (uint32_t)constrain((int)bakedMs, 10, 10000);
  } else {
    bakedName = "";   // any other launch ends the "which baked file" answer
  }
```

(Placement note: this converts `animationName` to `"frames"` before the per-type `if` blocks below it; none of those blocks match `"frames"`, so nothing else fires. Do NOT place it before the reset block: the reset must not run for a rejected load, and it must still run for an accepted one.)

- [ ] **Step 5: Wire channel clears bakedName; status reports it**

In `handleFrames()` success path (right before its final `sendJson(200, ...)`), add:

```cpp
  bakedName = "";   // wire-pushed frames replace whatever baked file was playing
```

In the status builder (api_handlers.ino line ~762), after:

```cpp
    json += ",\"animation\":\"" + animationName + "\"";
```

add:

```cpp
    if (bakedName.length()) json += ",\"baked\":\"" + escapeJson(bakedName) + "\"";
```

- [ ] **Step 6: Self-review the diff**

Check: `loadCfr` defined in `anim_frames.ino` (alphabetically before its caller, no prototype needed); reference params match the call site exactly (`uint16_t&, uint16_t&, uint8_t&`); every early return closes the file; header offsets match the Global Constraints table byte for byte; little-endian assembly is `low | (high << 8)`; `framesCount` cast to `uint8_t` is safe because `fcount <= 96`; `bakedName` is set only after a successful load; no em-dashes.

- [ ] **Step 7: Commit**

```bash
git add esp32_matrix_webserver/anim_frames.ino esp32_matrix_webserver/api_handlers.ino
git commit -m "feat(frames): baked .cfr loader, type baked with hue shift, status reports baked name"
```

---

### Task 3: Gallery page + hub card

**Files:**
- Create: `esp32_matrix_webserver/data/gallery.html`
- Modify: `esp32_matrix_webserver/data/animations.html` (one new card in the grid)

**Interfaces:**
- Consumes: `GET /frames/index.json` (static, served by the existing onNotFound LittleFS handler), `POST /api/display/animation` with `{"type":"baked","name","hue"}` (Task 2).

- [ ] **Step 1: Create gallery.html**

First read `data/settings.html` lines 1-15 and its closing script tags to copy the exact page scaffold (doctype, head, stylesheet link, wrap/container markup, and the `backnav.js`/`header.js` data-auto script lines). Keep the CONTENT below verbatim inside that scaffold; if the scaffold shown here differs from the repo's, the repo's wins:

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Expression Gallery</title>
  <link rel="stylesheet" href="app.css">
  <style>
    #tiles { display:grid; grid-template-columns:repeat(auto-fill,minmax(140px,1fr)); gap:8px; }
    .tile { text-align:left; padding:10px 12px; cursor:pointer; }
    .tile .nm { font-weight:600; }
    .tile .meta { font-size:0.75rem; color:var(--text-dim); }
    .tile.playing { outline:2px solid var(--accent-page); }
  </style>
</head>
<body>
  <main class="wrap">
    <div class="panel">
      <div class="panel-title">Expression Gallery</div>
      <div class="subcard">
        <div class="row"><label>Hue shift <output id="hue_o">0</output></label></div>
        <div class="row"><input type="range" id="hue" min="0" max="255" value="0"></div>
      </div>
      <div class="subcard">
        <div class="row"><span id="status_line">Loading library...</span></div>
        <div id="tiles"></div>
      </div>
    </div>
  </main>
  <script>
const $=id=>document.getElementById(id);
function bindOut(id,oid){const e=$(id),o=$(oid);const u=()=>o.value=e.value;e.addEventListener("input",u);u();}
async function load(){
  try{
    const idx=await (await fetch("/frames/index.json")).json();
    const anims=idx.animations||[];
    if(!anims.length) throw new Error("empty index");
    $("status_line").textContent=anims.length+" baked animations ("+Math.round(idx.totalBytes/1024)+" KB)";
    $("tiles").innerHTML=anims.map(a=>
      `<button type="button" class="tile" data-name="${a.name}"><div class="nm">${a.name}</div>`+
      `<div class="meta">${a.frames}f${a.loop?" / plays "+a.loop+"x":""}</div></button>`).join("");
    $("tiles").addEventListener("click",async ev=>{
      const t=ev.target.closest(".tile"); if(!t) return;
      const name=t.dataset.name;
      const r=await fetch("/api/display/animation",{method:"POST",
        headers:{"Content-Type":"application/json"},
        body:JSON.stringify({type:"baked",name:name,hue:+$("hue").value})});
      document.querySelectorAll(".tile.playing").forEach(x=>x.classList.remove("playing"));
      if(r.ok){ t.classList.add("playing"); $("status_line").textContent="Playing: "+name; }
      else    { $("status_line").textContent="Failed to play "+name; }
    });
  }catch(e){
    $("status_line").textContent="No baked animations installed (missing /frames/index.json).";
  }
}
bindOut("hue","hue_o");
load();
  </script>
  <script src="backnav.js" data-auto data-parent="/animations.html" data-label="Animations"></script>
  <script src="header.js" data-auto></script>
</body>
</html>
```

- [ ] **Step 2: Hub card**

In `data/animations.html`, the grid is one `<a class="card">` per line (e.g. line 28 is the liquid card). Add after the last card in that grid, matching indentation:

```html
    <a href="/gallery.html" class="card"><span class="icon">🖼️</span><div class="name">Expression Gallery</div><div class="desc">86 baked studio animations</div></a>
```

- [ ] **Step 3: Local pre-upload check (no board needed)**

Serve `data/` locally and drive the page with Playwright (the /api and /frames fetches 404 locally, so assert the FAILURE path text appears, tiles container exists, hue slider binds, no other console errors):

```bash
cd esp32_matrix_webserver/data && python -c "from http.server import ThreadingHTTPServer,SimpleHTTPRequestHandler; ThreadingHTTPServer(('127.0.0.1',8124),SimpleHTTPRequestHandler).serve_forever()"
```

Wait: `data/frames/index.json` EXISTS after Task 1, so locally the fetch SUCCEEDS: assert 86 tiles render and `#status_line` shows "86 baked animations". The animation POST will 404 locally; clicking one tile must show "Failed to play <name>" without a thrown error. Kill the server afterward.

- [ ] **Step 4: Commit**

```bash
git add esp32_matrix_webserver/data/gallery.html esp32_matrix_webserver/data/animations.html
git commit -m "feat(gallery): expression gallery page + hub card"
```

---

### Task 4: Docs + version bump

**Files:**
- Modify: `docs/API.md` (new section after the MQTT publisher section)
- Modify: `CLAUDE.md` (firmware layout line + one bullet in the API/settings section)
- Modify: `README.md` (asset refresh command)
- Version: `npm run bump:minor` (0.13.0 to 0.14.0; the script stamps and commits itself)

**Interfaces:**
- Consumes: names fixed in Tasks 1-3 (`baked`, `hue`, `/frames/`, `gallery.html`, status `baked` field).

- [ ] **Step 1: API.md section**

Add a new `##` section immediately after the MQTT publisher section's content ends (before whatever section follows it; read the file to find the boundary):

```markdown
## Baked frames (.cfr)

The board ships the studio's animation library as static assets in `/frames/`
(86 `.cfr` files + `index.json`, ~146 KB), baked by the studio repo's
`npm run export:frames`. The canonical format contract is the studio's
`docs/frames-file-format.md` (.cfr v1).

- `POST /api/display/animation` `{"type":"baked","name":"aurora"}` plays one.
  Optional `hue` (0-255) rotates every palette entry around the color wheel at
  load time. `transient:true` skips auto-resume as usual; otherwise the board
  resumes the baked animation after a power cycle.
- Names are `[a-z0-9_-]` only; a bad name or corrupt file returns 400 and the
  display is untouched. Play-once files (loop count N in the file) hold their
  last frame, matching the frames wire channel.
- `GET /api/status` includes `"baked":"<name>"` while a baked animation is
  active.
- The gallery page (`/gallery.html`) lists the library from `/frames/index.json`.
- Refreshing the assets: in the studio repo run `npm run export:frames`, copy
  `frames-out/` into this repo's `esp32_matrix_webserver/data/frames/`, then do
  a LittleFS upload.
```

- [ ] **Step 2: CLAUDE.md, two edits**

Edit 1, firmware layout paragraph: change

```markdown
`data/*.html` + the shared web design system (`app.css`, `backnav.js`
`header.js`, `bright.js`, `previews.js`, `palettes.js`, all `data-auto` self-injecting).
```

to

```markdown
`data/*.html` + the shared web design system (`app.css`, `backnav.js`
`header.js`, `bright.js`, `previews.js`, `palettes.js`, all `data-auto` self-injecting) ·
`data/frames/` (86 baked `.cfr` studio expressions + `index.json`; gallery at `data/gallery.html`).
```

Edit 2, in the "API, settings, NVS, calibration" section, add a bullet directly after the `- **API:**` bullet:

```markdown
- **Baked frames:** the studio animation library ships on the board (`data/frames/`,
  .cfr v1): `POST /api/animation {"type":"baked","name":...,"hue":0-255}`, gallery
  page `gallery.html`. Contract + refresh workflow in `docs/API.md`.
```

- [ ] **Step 3: README refresh command**

Read `README.md`, locate the development/build section that mentions the LittleFS data upload, and add this subsection at its end (adjust the heading level to match neighbors):

```markdown
### Refreshing the baked expression library

The 86 `.cfr` files in `esp32_matrix_webserver/data/frames/` are exported from the
`claude-expression-studio` repo. To refresh after the studio library changes:

    cd ../claude-expression-studio && npm run export:frames
    cp frames-out/*.cfr frames-out/index.json ../peckworks-esp32s3matrix/esp32_matrix_webserver/data/frames/

Then do a LittleFS upload. Commit the changed assets.
```

- [ ] **Step 4: Commit docs, then bump**

```bash
git add docs/API.md CLAUDE.md README.md
git commit -m "docs: baked frames player, gallery, asset refresh workflow"
npm run bump:minor
npm run check
```

Expected: bump makes its own `chore: bump v0.14.0` commit; check shows repo artifacts at 0.14.0 with the live board still at 0.13.0 (expected drift until Task 5).

---

### Task 5: Hardware verification checkpoint (user in the loop)

**Files:** none (HTTP only; use the flash-and-verify skill).

- [ ] **Step 1: User uploads BOTH artifacts** (firmware changed AND data/ changed): Sketch -> Upload, then LittleFS upload (Serial Monitor closed). The LittleFS upload is ~150 KB bigger than before; it may take noticeably longer.

- [ ] **Step 2: Version + assets live**

```bash
curl http://esp32matrix.local/api/status
curl -s http://esp32matrix.local/frames/index.json | head -c 200
npm run check
```

Expected: fw and web 0.14.0; index.json served; check fully green.

- [ ] **Step 3: Baked play + status + framebuffer**

```bash
curl -X POST http://esp32matrix.local/api/display/animation -H "Content-Type: application/json" -d "{\"type\":\"baked\",\"name\":\"aurora\",\"transient\":true}"
curl http://esp32matrix.local/api/status
curl -s http://esp32matrix.local/api/display/framebuffer | head -c 300
```

Expected: 200 with `"animation":"frames"`; status shows `"baked":"aurora"`; framebuffer non-black.

- [ ] **Step 4: Hue shift changes colors, not geometry**: play aurora with `"hue":128,"transient":true`, framebuffer differs from Step 3, same lit-pixel positions on the matching frame phase (eyeball-level check: colors clearly shifted).

- [ ] **Step 5: Play-once holds**: `{"type":"baked","name":"done","transient":true}` then after ~5s two framebuffer reads 3s apart are identical (held last frame).

- [ ] **Step 6: Rejections leave the display untouched**: POST names `"nope"` (missing) and `"../secrets"` (traversal): both 400, status animation unchanged.

- [ ] **Step 7: Auto-resume**: play aurora WITHOUT transient, wait ~10s (debounced NVS save), user power-cycles the board; after boot, status shows `"baked":"aurora"` again.

- [ ] **Step 8: Gallery + wire regression**: load `/gallery.html` in a browser (86 tiles, tap plays, hub card present); then fire a studio expression (`matrix_express` or a hooks push) and confirm wire frames still render (buffer move regression) and status no longer reports `baked`.

- [ ] **Step 9: Heap check**: `/api/status` free_heap and largest_block at least as good as 0.13.0's baseline (~149.4k / 139.2k; expect slightly BETTER internal DRAM since 4.6 KB of static buffer moved to PSRAM).

- [ ] **Step 10: Restore board state, then PR** to master (body notes the buffer move to PSRAM, the wire cap split, and the asset refresh workflow).
