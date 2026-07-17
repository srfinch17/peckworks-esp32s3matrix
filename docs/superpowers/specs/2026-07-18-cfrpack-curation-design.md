# .cfrpack Archive + Library Curation, Design Spec

**Date:** 2026-07-18
**Status:** Approved (user: "pack it", with a 15-animation drop list)
**Author:** the user + Claude

## Summary

The nemesis panel measured the baked-frames feature shipping LittleFS at 224/224
blocks (0 free): 87 loose files cost 4 KB of flash each. Fix, per the user's
decision: (1) CURATE: drop 15 animations from the bake (user-picked list below);
(2) PACK: ship the remaining 71 as ONE archive file (`library.cfrpack`) plus a
minified `index.json`. Measured math: 71 anims, 136,624 payload bytes; pack +
index land near 37 blocks versus 108 loose, freeing roughly 71 blocks and turning
`scripts/check-fs.mjs` green. Gallery and the `type:"baked"` API do not change.

## Curation (user's drop list, verified against index.json)

cross, ok, wink, yawn, thumbsup, done, jack-o-lantern, working, wait-logo-boot,
wait-logo-breathe, wait-logo-chase, wait-logo-ripple, wait-orbit, bounce,
shooting-star. ("shoothing-star" read as shooting-star; duplicate "ok" deduped.)

The list lives as a COMMITTED exclude file in the studio repo
(`scripts/frames-exclude.json`), read by the exporter, so refreshes can never
resurrect a dropped bake. Drops affect ONLY the baked gallery: the studio's wire
channel (hooks, wait pools, matrix_express) still carries all of these live.

`done` was the only loop!=0 bake; after the drop no shipped play-once file
remains. The hold-last-frame engine path stays covered by the wire channel
(same framesLoops variable, hardware-proven); the .cfr loop-byte handoff is one
statically-reviewed assignment. Accepted.

## .cfrpack v1 (new, canonical definition goes in the STUDIO format doc)

One file, little-endian:

| offset | size | field |
|---:|---:|---|
| 0 | 4 | magic ASCII `CFRP` |
| 4 | 1 | version = 1 |
| 5 | 1 | reserved = 0 |
| 6 | 2 | count u16 |
| 8 | 40 x count | table entries |
| ... | ... | payloads: each a COMPLETE, unmodified `.cfr` blob |

Table entry (40 bytes): `name` 32 bytes, ASCII `[a-z0-9_-]`, zero-padded (spec cap
31 chars + NUL; longest shipped name is 14); `offset` u32 from file start; `length`
u32. Entries sorted by name. Offsets must be ascending, non-overlapping, in-bounds,
and each payload must itself validate as .cfr v1 (the firmware re-uses the existing
per-blob validation).

## Studio changes (local branch, user merges)

- `scripts/export-frames.mjs`: read `scripts/frames-exclude.json` (new, committed,
  the 15 names + a comment field for why); emit `frames-out/library.cfrpack` and a
  MINIFIED `frames-out/index.json` (no pretty-print); keep loose `.cfr` files in
  frames-out for dev inspection (they are not copied to the board anymore). Add
  the panel's guards: fail the export on frames > 160, on names not matching
  `^[a-z0-9_-]{1,31}$` (strict lowercase, no /i), or on any excluded name not
  found (typo protection).
- `docs/frames-file-format.md`: status DRAFT -> SHIPPED; document the firmware's
  160-frame cap; add the `.cfrpack` v1 section above as canonical.
- Tests for pack encode/decode round-trip + exclude + guards; suite + npm run
  check green.
- Size report regenerated (71 entries).

## Firmware changes (this branch)

- `loadCfr(name, ...)` becomes a pack lookup: open `/frames/library.cfrpack`,
  validate magic/version/count, binary-or-linear scan the table for `name`
  (71 entries: linear is fine), validate offset/length in-bounds, then run the
  EXISTING .cfr validation/decode on the blob at that offset (same checks, same
  fail-safe returns, same blank-not-corrupt on mid-read fault). Name guard
  unchanged.
- `data/frames/` shrinks to exactly 2 files: `library.cfrpack` + `index.json`.
- `scripts/check-fs.mjs`: bijection assertion changes to: index.json entries all
  present in the pack table and vice versa (read both locally), plus the existing
  headroom/frames/name checks. Expect GREEN after this change.
- README refresh workflow: copy 2 files instead of the rm+cp glob.
- docs/API.md: assets paragraph updated (2 files, pack format pointer, new size);
  gallery unchanged (still reads index.json).
- Task-5 hardware verification: hold-test swaps to a wire-channel loop=N push;
  add: play the FIRST and LAST table entries (offset math edges), and one name
  present in index but requested with wrong case (rejects).

## Edge cases

- Pack missing or fails validation: every baked play 400s (fail-safe, display
  untouched), gallery still renders from index.json but tiles fail visibly, boot
  fallback (rainbow) already covers the resume path.
- index.json and pack table disagree (partial copy): check-fs catches it at
  refresh time; at runtime the pack table is the authority (a tile listed in
  index but absent from the pack 400s cleanly).
- Duplicate names in the table, zero-length payloads, table overrunning file:
  all rejected at open (count/offset/length validation).

## Non-Goals

- No change to the wire channel, the gallery UX, hue behavior, or the `baked`
  API surface. No partition changes. Loose-file loading is REMOVED, not kept as
  a fallback (one code path, one truth).
