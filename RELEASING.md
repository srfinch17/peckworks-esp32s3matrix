# Releasing the firmware

How to cut a downloadable release that end users can flash — from the
[install page](https://srfinch17.github.io/peckworks-esp32s3matrix/install/) (one-click,
browser) or the GitHub release (offline `flash.bat`/`flash.sh`).

> ⚠️ **The golden rule: a distributed `.bin` must NEVER contain WiFi credentials.**
> `secrets.h` bakes your SSID/password into the compiled firmware. `build-release` refuses
> to run while `secrets.h` is present for exactly this reason — don't override it for a
> release. Step 4 below is a hard verification gate, not a formality.

## 1. Set the version

```bash
npm run bump:patch     # or bump:minor / bump:major
npm run stamp          # propagate VERSION → version.h + data/version.json
npm run check          # confirm nothing drifted
```

## 2. Compile a clean binary (no secrets)

Claude can't compile — this is a human + Arduino IDE step.

1. Move your personal `secrets.h` out of the way:
   ```bash
   mv esp32_matrix_webserver/secrets.h esp32_matrix_webserver/secrets.h.bak
   ```
2. In the Arduino IDE: **Sketch → Export Compiled Binary** (writes the `.bin`s into
   `esp32_matrix_webserver/build/...`).
3. If `data/` changed, also re-upload LittleFS so `littlefs.bin` is current.

## 3. Build the merged release bundle

```bash
npm run build:release
```

Produces, in `release/`: `esp32matrix-<version>-merged.bin` (flash at `0x0`),
`littlefs.bin`, `esptool.exe`, `flash.bat`, `flash.sh`, `index.html`, `manifest.json`.
(`build-release` will **halt** if `secrets.h` is still present — good.)

## 4. Verify the binary carries no credentials (REQUIRED)

Even with the gate, confirm directly. With `secrets.h.bak` still on disk, scan the merged
`.bin` for those exact strings:

```bash
node -e '
const fs=require("fs");
const sec=fs.readFileSync("esp32_matrix_webserver/secrets.h.bak","utf8");
const ssid=(sec.match(/WIFI_SSID\s+"([^"]*)"/)||[])[1];
const pass=(sec.match(/WIFI_PASSWORD\s+"([^"]*)"/)||[])[1];
const v=fs.readFileSync("VERSION","utf8").trim();
const bin=fs.readFileSync(`release/esp32matrix-${v}-merged.bin`);
const tainted=(ssid&&bin.includes(Buffer.from(ssid)))||(pass&&bin.includes(Buffer.from(pass)));
console.log(tainted ? "🚨 TAINTED — do NOT publish; rebuild without secrets.h" : "✅ clean — safe to publish");
'
```

Only proceed on `✅ clean`. Restore your `secrets.h` afterward:
`mv esp32_matrix_webserver/secrets.h.bak esp32_matrix_webserver/secrets.h`.

## 5. Host the binary for the one-click flasher

The Pages install page flashes from a same-origin copy. Put the clean `.bin` beside the
manifest and bump the manifest's path/version if the version changed:

```bash
cp release/esp32matrix-$(cat VERSION)-merged.bin site/install/
# edit site/install/manifest.json → "version" and parts[0].path to the new filename
```

Commit `site/install/` (the `.bin` is intentionally committed here — it's the artifact
the browser flasher fetches).

## 6. Publish the GitHub release

Zip the offline installer, then create the release with the bin + zip attached:

```bash
V=$(cat VERSION)
( cd release && zip -j "esp32matrix-$V-installer.zip" \
    "esp32matrix-$V-merged.bin" esptool.exe flash.bat flash.sh )
gh release create "v$V" \
  "release/esp32matrix-$V-merged.bin" \
  "release/esp32matrix-$V-installer.zip" \
  --title "v$V" \
  --notes "Flash via the one-click installer (link in README) or unzip the installer and run flash.bat / flash.sh."
```

`/releases/latest` (linked from the site + README) now resolves to it.
