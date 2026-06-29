# Repo Split — firmware ↔ studio — Design

**Date:** 2026-06-28
**Status:** approved (design); plan pending

## Goal

Split the current `peckworks-esp32s3matrix` monorepo into two independently
showcaseable repositories that already exist as logically separate things and
communicate only over the board's HTTP API:

- **`peckworks-esp32s3matrix`** (existing repo, becomes **firmware**) — the
  self-contained embedded product: ESP32-S3 firmware + its own onboard web UI /
  animation selector + WiFi captive-portal onboarding + calibration. Runs
  standalone, no computer required.
- **`claude-expression-studio`** (new, **public**) — the renderer-agnostic Claude
  presence & expression system: the Studio web tool, the shared JS render core,
  the MCP server + engine, the Claude Code hooks, and the landing/showcase. The
  board is *one optional renderer*; the studio talks to it only via HTTP.

This is the final step of the `installable-product-target` / `display-emote-northstar`
arc. It is deliberately done LAST, after the feature work merged to master (PR #22).

## Decisions (locked)

1. **Arrangement:** existing repo → firmware; new repo → studio.
2. **History:** preserve per-repo history. New studio repo via `git filter-repo`
   (history rewritten to the studio paths); existing firmware repo via a normal
   removal commit (its history is left untouched — studio files simply stop being
   present going forward; their history lives on in the new repo).
3. **Studio repo name:** `claude-expression-studio`.
4. **Visibility:** public (portfolio showcase + live GitHub Pages demo).

## File partition

| → **`peckworks-esp32s3matrix`** (firmware) | → **`claude-expression-studio`** (studio) |
|---|---|
| `esp32_matrix_webserver/` (.ino + `data/` web UI) | `studio/` · `shared/` · `mcp_server/` · `claude-hooks/` · `site/` |
| `install/` (flasher) · `release/` → gitignore | `pages-dist/` → gitignore (regenerated) |
| **scripts:** `build-release.mjs` (+test) + version tooling\* | **scripts:** `build-pages`, `build-gallery-data`, `setup`, `setup-lib`, `approval`, `check-expression`, `check-manifest`, `copy-shared-runtime`, `rebuild-mcp`, `dump-sim-frames`, `gen-ask-icons`, `gen-wait-logo`, `gen-wait-rainbow`, `render-contact-sheet` (+ their tests) + version tooling\* |
| **docs:** `PITFALLS.md`, `LED_BRIGHTNESS.md`, + firmware specs/plans (animations, clock, weather, web-UI, settings, calibration, distribution) | **docs:** `ROADMAP.md`, `.superpowers/`, + studio specs/plans (expression-display, matrix-idle, presence-*, claude-sweep, logo-identity, awaiting-input, presence-fabric, animation-roster, expression-studio-*, hooks-and-animation-moments, trigger-manifest-*, board-local-first, js-animation-library, studio-editor-*, editor-params-labels, frame-expression-editor, onboarding-installer, pages-showcase, presence-card, reapproval-toggle, no-board-presence, presence-lifecycle, this split spec) |
| Own `CLAUDE.md` · `README.md` · `VERSION` · `package.json` · `.gitignore` | Own `CLAUDE.md` · `README.md` · `VERSION` · `package.json` · `.gitignore` · `.github/workflows/pages.yml` |

\* **Version tooling** (`version-bump.js`, `version-check.js`, `version-lib.js`,
`version-stamp.js`, `version.test.js`) is copied into **both** repos; in each, the
stamp targets only that repo's artifacts — firmware: `version.h` + `data/version.json`;
studio: `mcp_server/package.json` + `shared/manifest.json` (and the `.mcpb` manifest).
The two repos version independently (they already are independently deployed artifacts).

**Mixed dirs (`scripts/`, `docs/`)** sort by *the subsystem each file serves*. The
per-file doc sorting is mechanical and finalized during execution against this
principle; the lists above are the intended assignment.

## The cross-repo seam (HTTP contract, no code dependency)

The firmware **defines** the board's HTTP API; the studio **consumes** a subset of
it over HTTP only. There is **zero shared code** between the repos — the coupling is
a runtime HTTP contract, one-directional and loose.

- **Firmware repo** keeps the full API surface as `docs/API.md` (it implements every
  endpoint). This is lifted from the current `CLAUDE.md` "API surface" section.
- **Studio repo** gets `docs/board-api-contract.md` — only the endpoints the engine
  and hooks call: `/api/display/frames`, `/api/display/animation`, `/api/presence`
  (GET + POST), `/api/display/framebuffer`, `/api/idle/arm`, `/api/brightness`,
  `/api/status` — annotated "implemented by `peckworks-esp32s3matrix`; this is the
  contract the engine/hooks depend on." The two READMEs cross-link.

### CLAUDE.md split

The current single `CLAUDE.md` splits along the same line:
- **Firmware CLAUDE.md:** hardware facts, coordinate system, Arduino IDE setup, WiFi
  portal, firmware layout, add-animation recipe, the API surface (it owns), auto-resume/
  NVS, settings, idle screensaver, calibration, board discovery, firmware versioning.
- **Studio CLAUDE.md:** the shared render core, the Studio surfaces, the engine, the
  MCP server, the trigger manifest, the expression/presence model, the Claude Code
  hooks, onboarding (`npm run setup`), Pages, studio versioning. Reframes "the dev
  loop": for the studio, Claude *can* run/verify things locally; the board is one
  optional renderer reached via the documented HTTP contract.

### Known seam to document (not automate)

`shared/firmware-sims.js` (JS ports of the `anim_*.ino`) lives in the **studio** repo;
the `.ino` source lives in **firmware**. They are independent reimplementations.
Adding a firmware animation and wanting it in the web sim/Gallery requires a manual
JS port in the studio repo. This coupling is accepted and documented in both repos'
CLAUDE.md — it is not generated or enforced across repos.

## Execution approach (history-preserving)

**Studio repo (new):**
1. `gh repo create claude-expression-studio --public` — created **empty** (no auto
   README/license/gitignore, so the history push is clean).
2. Fresh clone of the monorepo → `git filter-repo` keeping only the studio paths
   (incl. studio scripts + studio docs), rewriting history to those files.
3. Add the split-only files on top (firmware-free `CLAUDE.md`, new `README.md`,
   trimmed `package.json`, `docs/board-api-contract.md`, `.gitignore`,
   `.github/workflows/pages.yml`).
4. Push to the new repo. Re-run the test suite + `check:manifest` to confirm the
   extracted repo is self-contained and green.

**Firmware repo (existing):**
1. On a branch, `git rm -r` the studio paths (`studio/ shared/ mcp_server/
   claude-hooks/ site/ pages-dist/` + studio scripts + studio docs).
2. Add the firmware-scoped `CLAUDE.md`, `README.md`, `docs/API.md`, trimmed
   `package.json`/`.gitignore`.
3. Commit → PR → merge. History preserved; no force-push.

Both repos end with honest, full history and a green build. Pages is **not** set up
in this step — it is redone per-repo afterward (studio: the existing showcase;
firmware: optional board web-UI showcase).

## Out of scope (explicitly later)

- Per-repo GitHub Pages (post-split, separate step).
- Assigning the ~40 approved animations to hook events (content; a post-split smoke
  test that the studio repo works standalone).
- The board.html local-first arbitration rework (the studio repo's first feature;
  needs its own design pass).
- Engine orphan-process cleanup; raising the engine framebuffer-proxy timeout.

## Success criteria

- Two repos, each with a green test suite and no dangling reference to the other's
  files.
- Studio repo runs (engine + Studio + hooks) with the board absent and present.
- Firmware repo builds the release binary standalone.
- Each repo's README cross-links the other and documents the HTTP contract boundary.
- The existing firmware repo's history is intact (not force-pushed).
