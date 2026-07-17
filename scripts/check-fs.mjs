// Guard the LittleFS image: it must actually build, have real free-block
// headroom, and the baked-frames library (data/frames/library.cfrpack +
// index.json) must be internally consistent. Reuses build-release.mjs's
// exact mechanism for locating mklittlefs and parsing the SPIFFS partition
// size.
//
//   node scripts/check-fs.mjs
//
// mklittlefs doesn't print a used/free block summary, so headroom is derived
// by binary-searching the smallest pool size (in 4KB blocks) that still
// builds data/ successfully; free blocks = partition blocks - that minimum.
// Exit code is non-zero on any failed assertion.

import { readFile } from "node:fs/promises";
import { mkdtempSync, rmSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { execFileSync } from "node:child_process";
import { glob } from "node:fs/promises";
import path from "node:path";
import os from "node:os";
import { pickHighestVersionPath } from "./build-release.mjs";

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const DATA_DIR = path.join(REPO_ROOT, "esp32_matrix_webserver", "data");
const FRAMES_DIR = path.join(DATA_DIR, "frames");
const PACK_PATH = path.join(FRAMES_DIR, "library.cfrpack");
const PKG_ESP32 = path.join(os.homedir(), "AppData", "Local", "Arduino15", "packages", "esp32");

const BLOCK_SIZE = 4096;
const MIN_FREE_BLOCKS = 8;
const MAX_FRAMES = 160;
const NAME_RE = /^[a-z0-9_-]{1,31}$/;

async function globOne(root, pattern) {
  const hits = [];
  for await (const f of glob(pattern, { cwd: root })) hits.push(path.join(root, f));
  if (!hits.length) throw new Error(`Not found under ${root}: ${pattern}`);
  return pickHighestVersionPath(hits);
}

function readSpiffs(csv) {
  // huge_app.csv: "spiffs, data, spiffs, 0x310000,0xE0000,"
  const line = csv.split("\n").find((l) => /^\s*spiffs\s*,/.test(l));
  if (!line) throw new Error("no spiffs row in partition CSV");
  const cols = line.split(",").map((s) => s.trim());
  return { offset: cols[3], size: cols[4] };
}

/** true if a LittleFS image of `blocks` 4KB blocks can hold data/. */
function fits(mklittlefs, tmpBin, blocks) {
  try {
    execFileSync(mklittlefs, ["-c", DATA_DIR, "-p", "256", "-b", String(BLOCK_SIZE), "-s", String(blocks * BLOCK_SIZE), tmpBin], { stdio: "ignore" });
    return true;
  } catch {
    return false;
  }
}

async function main() {
  let failed = false;
  const assert = (ok, msg) => {
    console.log(`${ok ? "PASS" : "FAIL"}: ${msg}`);
    if (!ok) failed = true;
  };

  const toolsRoot = process.env.ESP32_TOOLS_DIR || PKG_ESP32;
  const mklittlefs = await globOne(toolsRoot, "tools/mklittlefs/**/mklittlefs.exe");
  const csvPath = await globOne(toolsRoot, "hardware/esp32/**/tools/partitions/huge_app.csv");
  const { size: fsSizeHex } = readSpiffs(await readFile(csvPath, "utf8"));
  const fsSize = Number(fsSizeHex);
  const totalBlocks = fsSize / BLOCK_SIZE;
  if (!Number.isInteger(totalBlocks)) {
    throw new Error(`spiffs size ${fsSizeHex} is not a multiple of the ${BLOCK_SIZE}-byte block size`);
  }

  const tmpDir = mkdtempSync(path.join(os.tmpdir(), "check-fs-"));
  const tmpBin = path.join(tmpDir, "littlefs.bin");
  try {
    // (a) the image builds at the real partition size.
    const builds = fits(mklittlefs, tmpBin, totalBlocks);
    assert(builds, `LittleFS image builds from data/ at the partition size (${fsSize} bytes = ${totalBlocks} x 4KB blocks)`);

    // (b) headroom. Shrink the pool (binary search) until it no longer fits;
    // the gap between that minimum and the real partition is free blocks.
    if (builds) {
      let lo = 1, hi = totalBlocks;
      while (lo < hi) {
        const mid = Math.floor((lo + hi) / 2);
        if (fits(mklittlefs, tmpBin, mid)) hi = mid; else lo = mid + 1;
      }
      const requiredBlocks = lo;
      const freeBlocks = totalBlocks - requiredBlocks;
      assert(freeBlocks >= MIN_FREE_BLOCKS,
        `at least ${MIN_FREE_BLOCKS} free 4KB blocks of headroom (measured ${freeBlocks} free, ${requiredBlocks} of ${totalBlocks} blocks used)`);
    } else {
      assert(false, `at least ${MIN_FREE_BLOCKS} free 4KB blocks of headroom (skipped: image does not build at all)`);
    }
  } finally {
    rmSync(tmpDir, { recursive: true, force: true });
  }

  // (c) library.cfrpack table <-> index.json parity, plus table bounds.
  const index = JSON.parse(await readFile(path.join(FRAMES_DIR, "index.json"), "utf8"));
  let packBuf = null;
  try {
    packBuf = await readFile(PACK_PATH);
  } catch {
    packBuf = null; // pre-swap state: loose .cfr files still present, pack not written yet
  }

  if (!packBuf) {
    assert(false, "pack not present yet (pre-swap state)");
  } else {
    let magic = "", version = -1, count = 0, tableBytes = 0, tableValid = false;
    if (packBuf.length >= 8) {
      magic = packBuf.toString("ascii", 0, 4);
      version = packBuf.readUInt8(4);
      count = packBuf.readUInt16LE(6);
      tableBytes = 40 * count;
      tableValid = magic === "CFRP" && version === 1 && count >= 1 && count <= 1024 &&
        8 + tableBytes <= packBuf.length;
    }
    assert(tableValid,
      `library.cfrpack header valid (magic CFRP, version 1, count 1..1024, table in-bounds; ` +
      `got magic=${JSON.stringify(magic)} version=${version} count=${count})`);

    if (tableValid) {
      const entries = [];
      for (let i = 0; i < count; i++) {
        const base = 8 + i * 40;
        const nameBuf = packBuf.subarray(base, base + 32);
        const nul = nameBuf.indexOf(0);
        entries.push({
          name: nameBuf.toString("ascii", 0, nul === -1 ? 32 : nul),
          offset: packBuf.readUInt32LE(base + 32),
          length: packBuf.readUInt32LE(base + 36),
        });
      }

      const packNames = new Set(entries.map((e) => e.name));
      const indexNames = new Set(index.animations.map((a) => a.name));
      const orphans = [...packNames].filter((n) => !indexNames.has(n));
      const missing = [...indexNames].filter((n) => !packNames.has(n));
      assert(orphans.length === 0 && missing.length === 0,
        `library.cfrpack table matches index.json 1:1 (${packNames.size} in pack, ${indexNames.size} indexed` +
        (orphans.length ? `, orphans in pack: ${orphans.join(", ")}` : "") +
        (missing.length ? `, missing from pack: ${missing.join(", ")}` : "") + ")");

      const sorted = [...entries].sort((a, b) => a.offset - b.offset);
      let boundsOk = true;
      let badDetail = "";
      for (let i = 0; i < sorted.length && boundsOk; i++) {
        const e = sorted[i];
        if (e.offset < 8 + tableBytes || e.length < 12 || e.offset + e.length > packBuf.length) {
          boundsOk = false;
          badDetail = `"${e.name}" out of bounds (offset ${e.offset}, length ${e.length}, file ${packBuf.length})`;
        } else if (i > 0 && e.offset < sorted[i - 1].offset + sorted[i - 1].length) {
          boundsOk = false;
          badDetail = `"${sorted[i - 1].name}" and "${e.name}" overlap`;
        }
      }
      assert(boundsOk,
        `library.cfrpack entries are in-bounds and non-overlapping (${entries.length} entries checked` +
        (badDetail ? `, bad: ${badDetail}` : "") + ")");
    }
  }

  // (d) each index entry's shape: frame count and name pattern.
  const badEntries = index.animations.filter((a) => a.frames > MAX_FRAMES || !NAME_RE.test(a.name));
  assert(badEntries.length === 0,
    `every index entry has frames <= ${MAX_FRAMES} and name matching ${NAME_RE} (${index.animations.length} entries checked` +
    (badEntries.length ? `, bad: ${badEntries.map((a) => a.name).join(", ")}` : "") + ")");

  process.exit(failed ? 1 : 0);
}

main().catch((e) => { console.error("check-fs failed:", e.message); process.exit(1); });
