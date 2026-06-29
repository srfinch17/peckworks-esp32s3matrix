# Repo Split (firmware ↔ studio) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the `peckworks-esp32s3matrix` monorepo into two history-preserving repos — `peckworks-esp32s3matrix` (firmware, existing) and `claude-expression-studio` (new, public) — coupled only by the board's HTTP API.

**Architecture:** Studio is **extracted** to a new repo with `git filter-repo` (remove firmware-only paths from a fresh clone, rewrite history, push to an empty remote). Firmware is the **existing** repo with the studio paths removed in a normal commit (history never force-pushed). Each repo then gets its own root files (`CLAUDE.md`/`README`/`package.json`/`VERSION`/`.gitignore`) and a contract doc. No shared code crosses the boundary.

**Tech Stack:** git, `git-filter-repo` (Python), `gh` CLI, Node (`npm test`, `node --test`), Windows + Git Bash.

## Global Constraints

- **Privacy:** never use the maintainer's real name in code/comments/docs — both repos are distributable; refer to "the user". (Verbatim from existing CLAUDE.md.)
- **History:** the **existing firmware repo is NEVER force-pushed**; studio extraction uses `filter-repo` on a *throwaway clone* only.
- **Empty remote:** the new GitHub repo is created with **no** auto README/license/gitignore (clean first push).
- **Outward-facing/irreversible steps** (`gh repo create`, pushing the new repo, merging the firmware removal PR) require explicit user confirmation before running.
- **Green gates:** a repo is "done" only when its `npm test` (and `npm run check:manifest` for studio) passes and `grep` finds no dangling reference to the other repo's removed paths.
- Studio scripts use `node --test`; the studio suite must stay green after extraction.

## Reference — path assignment

**Firmware-only code/dirs** (removed from studio extraction; kept in firmware):
`esp32_matrix_webserver/`, `install/`, `release/` (likely gitignored).

**Studio code/dirs** (removed from firmware; kept in studio):
`studio/`, `shared/`, `mcp_server/`, `claude-hooks/`, `site/`, `pages-dist/` (gitignored), `.github/workflows/pages.yml`.

**Firmware-only scripts** (kept in firmware; removed from studio):
`build-release.mjs`, `build-release.test.js`.

**Version tooling** (copied in BOTH): `version-bump.js`, `version-check.js`, `version-lib.js`, `version-stamp.js`, `version.test.js` — each repo's `version-stamp.js` is trimmed to its own artifacts.

**All other `scripts/*`** → studio only.

**Firmware-only docs** (kept in firmware; removed from studio): `docs/PITFALLS.md`, `docs/LED_BRIGHTNESS.md`, and these `docs/superpowers/specs/` + matching `plans/`:
`2026-05-22-clock-redesign`, `2026-05-22-five-new-animations`, `2026-05-22-web-ui-polish`, `2026-05-27-weather2`, `2026-06-08-ledsim-preview`, `2026-06-08-liquid-fixes`, `2026-06-08-per-app-brightness`, `2026-06-09-autoresume-and-dst`, `2026-06-09-calendar-app`, `2026-06-09-palette-component`, `2026-06-09-sketch-app`, `2026-06-09-sound-visualizer`, `2026-06-19-board-settings-and-idle-screensaver`, `2026-06-21-led-calibration-battery`, `2026-06-21-calibration-lab-phase1`, `2026-06-21-calibration-phase3-correction-layer`, `2026-06-22-end-user-distribution`, `2026-06-22-ui-revamp-*`, `2026-06-22-ui-polish`/`2026-06-20-ui-polish`.
`2026-06-16-version-certainty` is copied to **both**.
**All other docs** (presence, manifest, studio, hooks, onboarding, expression-display, claude-sweep, logo-identity, awaiting-input, presence-fabric, animation-roster, js-animation-library, board-local-first, emoji-feature-preservation, reapproval, no-board-presence, presence-lifecycle, this split spec/plan) → studio only.

---

### Task 1: Safety net + tooling prerequisites

**Files:** none (git operations on the current repo).

- [ ] **Step 1: Confirm clean tree on the split branch**

Run: `git -C "$REPO" status -sb && git branch --show-current`
Expected: branch `chore/repo-split`, no unexpected staged changes (the split spec/plan may be present/untracked).

- [ ] **Step 2: Tag a pre-split rollback point and push it**

```bash
REPO="C:/Users/srfin/Dropbox/Dev/repos/peckworks-esp32s3matrix"
git -C "$REPO" tag pre-split master
git -C "$REPO" push origin pre-split
```
Expected: `* [new tag] pre-split -> pre-split`. This is the recovery anchor for the firmware repo.

- [ ] **Step 3: Verify `git-filter-repo` is available; install if missing**

Run: `git filter-repo --version`
Expected: a version string. If "not a git command" / not found:
```bash
python -m pip install --user git-filter-repo
git filter-repo --version
```
Expected after install: a version string.

- [ ] **Step 4: Verify `gh` auth**

Run: `gh auth status`
Expected: logged in to github.com as `srfinch17`.

- [ ] **Step 5: Commit (plan only, if untracked)**

```bash
git -C "$REPO" add docs/superpowers/plans/2026-06-28-repo-split.md
git -C "$REPO" commit -m "docs(plan): repo split implementation plan"
```

---

### Task 2: Extract the studio repo (filter-repo on a throwaway clone)

**Files:** new clone under the scratchpad; no change to the working repo.

**Interfaces:**
- Produces: `STUDIO_DIR` — a local git repo containing only studio history, ready for root-file rewrite (Task 3).

- [ ] **Step 1: Fresh clone into the scratchpad (throwaway)**

```bash
SCRATCH="C:/Users/srfin/AppData/Local/Temp/claude/C--Users-srfin-Dropbox-Dev-repos-peckworks-esp32s3matrix/64cd7e9b-dada-42ee-8199-799635cb29dd/scratchpad"
STUDIO_DIR="$SCRATCH/claude-expression-studio"
rm -rf "$STUDIO_DIR"
git clone "C:/Users/srfin/Dropbox/Dev/repos/peckworks-esp32s3matrix" "$STUDIO_DIR"
cd "$STUDIO_DIR" && git checkout master
```
Expected: clone completes; `git log --oneline -1` shows the master merge commit.

- [ ] **Step 2: Remove firmware-only code paths from ALL history**

```bash
cd "$STUDIO_DIR"
git filter-repo --force --invert-paths \
  --path esp32_matrix_webserver/ \
  --path install/ \
  --path release/
```
Expected: filter-repo runs to completion ("Parsed N commits ... New history written").

- [ ] **Step 3: Verify firmware code is gone but studio code + history remain**

Run: `ls esp32_matrix_webserver install 2>/dev/null; ls -d studio shared mcp_server claude-hooks site; git log --oneline | wc -l`
Expected: no `esp32_matrix_webserver`/`install` dirs; all five studio dirs present; commit count is large (history preserved).

- [ ] **Step 4: Run the studio suite in the extracted repo (pre-trim sanity)**

```bash
cd "$STUDIO_DIR/mcp_server" && npm ci --silent 2>/dev/null; cd "$STUDIO_DIR"
npm ci --silent 2>/dev/null || true
npm test 2>&1 | tail -15
```
Expected: tests run; note any failures referencing removed firmware files (fixed in Task 3 by trimming firmware scripts).

---

### Task 3: Studio repo — trim firmware leftovers, write root files, create remote, push, green gate

**Files:**
- Delete in `$STUDIO_DIR`: `scripts/build-release.mjs`, `scripts/build-release.test.js`, firmware docs (Reference list).
- Create: `CLAUDE.md`, `README.md`, `docs/board-api-contract.md`, `.gitignore`.
- Modify: `package.json` (drop `build:release`; keep studio scripts), `scripts/version-stamp.js` (studio artifacts only).

- [ ] **Step 1: Delete firmware-only scripts + docs from the studio repo**

```bash
cd "$STUDIO_DIR"
git rm scripts/build-release.mjs scripts/build-release.test.js
git rm docs/PITFALLS.md docs/LED_BRIGHTNESS.md
# firmware specs/plans (Reference list) — repeat per file:
git rm docs/superpowers/specs/2026-05-22-clock-redesign-design.md docs/superpowers/plans/2026-05-22-clock-redesign.md
# ...(remove each firmware doc from the Reference list; keep 2026-06-16-version-certainty in both)
```
Expected: each `git rm` reports the removed path. (Use `git rm --ignore-unmatch` for any plan file without a matching spec name.)

- [ ] **Step 2: Trim `package.json` test/build scripts to studio-only**

Edit `package.json` `scripts`: remove `"build:release"`. Keep `setup`, `stamp`, `check`, `check:manifest`, `test`, `build:mcpb`, `build:gallery`, `build:pages`, `bump:*`. Set `"test"` so it runs the studio node tests (the existing `test` script already globs `scripts` + `mcp_server` + `studio` + `shared`; remove any firmware path from it).

- [ ] **Step 3: Trim `scripts/version-stamp.js` to studio artifacts**

In `version-stamp.js`, remove the firmware stamping (writing `version.h` and `data/version.json`); keep stamping `mcp_server/package.json`, `shared/manifest.json`, and the `.mcpb` manifest. Update `version.test.js` expectations to match.

- [ ] **Step 4: Write the studio `CLAUDE.md`**

Create `CLAUDE.md` from the current one, keeping ONLY: the shared render core, Studio surfaces, the engine, MCP server, trigger manifest, expression/presence model, Claude Code hooks, onboarding (`npm run setup`), Pages, studio versioning. Replace the "dev loop" preamble with: Claude can run/verify the studio locally; **the board is one optional renderer reached only via the HTTP contract in `docs/board-api-contract.md` (implemented by the `peckworks-esp32s3matrix` repo)**. Keep the Privacy note. Remove all hardware-facts/Arduino/firmware-layout/calibration sections.

- [ ] **Step 5: Write `docs/board-api-contract.md`**

List only the consumed endpoints with method + body shape + meaning: `GET /api/status`, `POST /api/brightness`, `POST /api/display/frames`, `POST /api/display/animation` (incl. `transient`), `GET/POST /api/presence`, `GET /api/display/framebuffer`, `POST /api/idle/arm`. Header note: "Implemented by `peckworks-esp32s3matrix`. The engine (`mcp_server/`) and hooks (`claude-hooks/`) depend on this contract; no code is shared."

- [ ] **Step 6: Write `README.md` + `.gitignore`**

`README.md`: the Claude expression system — what it is (board-optional), `npm run setup` for Claude Code, the Studio, the live Pages demo, link to the firmware repo for hardware. `.gitignore`: `node_modules/`, `pages-dist/`, `mcp_server/dist/`, `mcp_server/shared-runtime/`, `*.mcpb`, OS junk.

- [ ] **Step 7: Green gate — studio suite + manifest**

```bash
cd "$STUDIO_DIR" && npm test 2>&1 | tail -8 && npm run check:manifest 2>&1 | tail -3
```
Expected: all tests pass; `manifest OK`.

- [ ] **Step 8: No dangling firmware references**

Run: `grep -rIl --exclude-dir=.git -e "esp32_matrix_webserver" -e "build:release" -e "\\.\\./install" . | grep -v board-api-contract.md || echo CLEAN`
Expected: `CLEAN` (or only intentional contract/doc mentions). Fix any code hit.

- [ ] **Step 9: Commit the split-only changes**

```bash
git add -A && git commit -m "chore: studio repo split — trim firmware, add root files + API contract"
```

- [ ] **Step 10: Create the empty public remote (CONFIRM with user first)**

```bash
gh repo create claude-expression-studio --public --description "Claude's renderer-agnostic presence & expression studio (ESP32 board optional)"
```
Expected: repo created at `srfinch17/claude-expression-studio`. (No `--add-readme` / no auto init.)

- [ ] **Step 11: Point origin at the new remote and push (CONFIRM)**

```bash
git remote remove origin 2>/dev/null; git remote add origin https://github.com/srfinch17/claude-expression-studio.git
git push -u origin master
git push origin --tags 2>/dev/null || true
```
Expected: `master -> master` on the new repo; browsing it shows full studio history.

---

### Task 4: Firmware repo — remove studio paths, write firmware root files, PR + merge

**Files (in the working repo on `chore/repo-split`):**
- Delete: `studio/`, `shared/`, `mcp_server/`, `claude-hooks/`, `site/`, `pages-dist/`, studio scripts, studio docs, `.github/workflows/pages.yml`.
- Create: `docs/API.md`.
- Modify: `CLAUDE.md` (firmware-only), `README.md` (firmware-only), `package.json` (firmware scripts), `.gitignore`.

- [ ] **Step 1: Remove studio code dirs**

```bash
cd "$REPO"
git rm -r studio shared mcp_server claude-hooks site
git rm -r pages-dist 2>/dev/null || true
git rm .github/workflows/pages.yml 2>/dev/null || true
```
Expected: bulk removals reported.

- [ ] **Step 2: Remove studio-only scripts (keep build-release + version tooling)**

```bash
cd "$REPO/scripts"
git rm approval.mjs approval.test.js build-gallery-data.mjs build-gallery-data.test.js \
  build-pages.mjs build-pages.test.js check-expression.mjs check-expression.test.js \
  check-manifest.mjs check-manifest.test.js copy-shared-runtime.mjs copy-shared-runtime.test.js \
  dump-sim-frames.mjs dump-sim-frames.test.js gen-ask-icons.py gen-wait-logo.py gen-wait-rainbow.py \
  rebuild-mcp.mjs render-contact-sheet.py setup-lib.mjs setup-lib.test.js setup.mjs setup.test.js
```
Expected: each removed.

- [ ] **Step 3: Remove studio docs (keep firmware docs from the Reference list)**

Delete every `docs/superpowers/specs|plans/*` and top-level doc NOT in the firmware Reference list, plus `.superpowers/`. Keep `2026-06-16-version-certainty` (copied to both). Example:
```bash
cd "$REPO"
git rm -r .superpowers 2>/dev/null || true
git rm docs/superpowers/specs/2026-06-25-expression-trigger-manifest-design.md docs/superpowers/plans/2026-06-25-trigger-manifest-plan1-protocol-core.md
# ...(remove each studio doc; keep the firmware Reference list)
```

- [ ] **Step 4: Trim `package.json` to firmware scripts**

Keep `bump:*`, `stamp`, `check`, `build:release`, and a `test` that runs only the firmware node tests (`scripts/build-release.test.js`, `scripts/version.test.js`). Remove `setup`, `check:manifest`, `build:mcpb`, `build:gallery`, `build:pages`.

- [ ] **Step 5: Trim `scripts/version-stamp.js` to firmware artifacts**

Keep stamping `version.h` + `data/version.json`; remove the `mcp_server`/`manifest` stamping. Update `version.test.js` expectations.

- [ ] **Step 6: Write firmware `CLAUDE.md` + `docs/API.md`**

`CLAUDE.md`: keep hardware facts, coordinate system, Arduino setup, WiFi portal, firmware layout, add-animation recipe, auto-resume/NVS, settings, idle screensaver, calibration, board discovery, firmware versioning, Privacy note. Remove all studio/engine/manifest/presence/hooks/onboarding sections. Add a line: "Studio/Claude integration lives in `claude-expression-studio`; this repo exposes the HTTP API in `docs/API.md`." `docs/API.md`: the full API surface (lifted from the old CLAUDE.md "API surface" section).

- [ ] **Step 7: Write firmware `README.md` + `.gitignore`**

`README.md`: the hardware product — flash it (`install/`), the onboard web UI, WiFi captive-portal setup, runs standalone; link to `claude-expression-studio` for the Claude layer. `.gitignore`: add `release/`, keep existing entries.

- [ ] **Step 8: Green gate — firmware tests + no studio references**

```bash
cd "$REPO" && npm test 2>&1 | tail -8
grep -rIl --exclude-dir=.git -e "mcp_server" -e "shared/render" -e "studio/" -e "claude-hooks" . | grep -vE "README|CLAUDE|API.md" || echo CLEAN
```
Expected: firmware tests pass; `CLEAN` (or only intentional doc cross-links).

- [ ] **Step 9: Commit, push, PR (CONFIRM merge with user)**

```bash
cd "$REPO"
git add -A && git commit -m "chore: split out studio into claude-expression-studio; firmware-only repo"
git push -u origin chore/repo-split
gh pr create --base master --head chore/repo-split \
  --title "chore: split — firmware-only repo (studio moved to claude-expression-studio)" \
  --body "Removes the studio/engine/hooks/site code (now at srfinch17/claude-expression-studio, history preserved via filter-repo) and re-scopes CLAUDE.md/README/package.json to firmware. History intact (no force-push). Rollback tag: pre-split."
```
Then, on user confirmation: `gh pr merge <n> --merge`.

---

### Task 5: Cross-link, verify both repos, finalize

**Files:** `README.md` in both repos (cross-link); memory + tasks.

- [ ] **Step 1: Cross-link READMEs**

In each repo's `README.md`, add a one-line link to the other ("Hardware/firmware: …" ↔ "Claude expression layer: …"). Commit + push each.

- [ ] **Step 2: Independent-build verification — studio**

```bash
cd "$STUDIO_DIR" && npm test 2>&1 | tail -5 && npm run check:manifest 2>&1 | tail -2 && npm run build:pages 2>&1 | tail -3
```
Expected: tests pass, `manifest OK`, `pages bundle written`.

- [ ] **Step 3: Independent-build verification — firmware**

```bash
cd "$REPO" && git checkout master && git pull && npm test 2>&1 | tail -5
node scripts/build-release.mjs --help 2>&1 | tail -3 || true
```
Expected: firmware tests pass; build-release script loads (full merge needs Arduino — out of scope here).

- [ ] **Step 4: Final dangling-reference sweep (both repos)**

Run the Task 3 Step 8 and Task 4 Step 8 greps once more in each repo.
Expected: `CLEAN` in both.

- [ ] **Step 5: Update auto-memory + close tasks**

Update `installable-product-target` / `display-emote-northstar` memories: split DONE, two repos live (URLs), pre-split tag recorded. Note the post-split follow-ups (per-repo Pages, animation approval, board.html local-first rework) now live in their respective repos.

## Self-Review

- **Spec coverage:** arrangement (T2/T4), history-preserving filter-repo (T2) + non-force firmware removal (T4), name + public remote (T3 S10), file partition (Reference + T3/T4), HTTP-contract seam (T3 S5 + T6 firmware API.md), CLAUDE.md split (T3 S4 / T4 S6), known firmware-sims seam (documented in both CLAUDE.md — fold into T3 S4 / T4 S6), success criteria (T5). Covered.
- **Placeholder scan:** doc-removal steps use "repeat per file" against an explicit Reference list rather than a vague TODO — acceptable (the list is concrete and finite); all command steps are literal.
- **Type consistency:** n/a (no code interfaces); path names match the Reference section and the spec table.
- **Gap fixed:** added the firmware-sims-seam documentation into the CLAUDE.md-writing steps.
