# ESP32-S3 Matrix Firmware

Firmware and onboard web UI for the **Waveshare ESP32-S3-Matrix** (8×8 / 64 WS2812B RGB
LEDs on an ESP32-S3 with a 6-axis IMU). It runs **standalone, no computer required**. The
board self-onboards onto WiFi via a captive portal, serves its own web UI to pick and tune
animations, shows weather and clock modes, includes a calibration lab, and exposes an HTTP
API.

🌐 **[Overview &amp; live demo](https://srfinch17.github.io/peckworks-esp32s3matrix/)** ·
⚡ **[Flash it in one click](https://srfinch17.github.io/peckworks-esp32s3matrix/install/)** ·
📦 **[Downloads / releases](https://github.com/srfinch17/peckworks-esp32s3matrix/releases/latest)**

## Features

- **8×8 animation engine.** Fire, matrix rain, fireworks, dance floor, comet, and an
  IMU-reactive liquid, plus more, each with its own web control page.
- **WiFi captive-portal onboarding.** Joins your network with no hardcoded credentials, and
  is reachable at `http://esp32matrix.local`.
- **Onboard web UI.** Animation selector, brightness, weather and clock modes, settings, and
  a calibration lab, all served from the device.
- **HTTP API.** Full surface in [`docs/API.md`](docs/API.md). Persistent settings and the
  last animation survive reboots (NVS).

## Flash it

No toolchain needed. End users flash one pre-merged binary (app plus web UI in a single
image):

- **One click (Chrome/Edge).** Open the [install page](https://srfinch17.github.io/peckworks-esp32s3matrix/install/),
  plug in the board, and click. It flashes over Web Serial; nothing is uploaded anywhere.
- **Offline (any OS).** Download the [latest release](https://github.com/srfinch17/peckworks-esp32s3matrix/releases/latest)
  and run `flash.bat` (Windows) or `flash.sh` (macOS/Linux).

After flashing, join the `ESP32-Matrix-Setup` hotspot, pick your WiFi, and open
`http://esp32matrix.local`. The distributed firmware ships with **no** baked-in
credentials. You set them on first boot.

Developers build the distributable image with `npm run build:release`. It refuses to build
while `secrets.h` is present, so a shipped `.bin` never carries WiFi credentials (see
[`RELEASING.md`](RELEASING.md)). For the Arduino IDE setup (board settings, libraries,
LittleFS upload), see [`CLAUDE.md`](CLAUDE.md) and [`docs/PITFALLS.md`](docs/PITFALLS.md).

### Refreshing the baked expression library

The 86 `.cfr` files in `esp32_matrix_webserver/data/frames/` are exported from the
`claude-expression-studio` repo. To refresh after the studio library changes:

    cd ../claude-expression-studio && npm run export:frames
    rm ../peckworks-esp32s3matrix/esp32_matrix_webserver/data/frames/*
    cp frames-out/library.cfrpack frames-out/index.json ../peckworks-esp32s3matrix/esp32_matrix_webserver/data/frames/

Then do a LittleFS upload. Commit the changed assets. `npm run check` now verifies the frames
directory matches index.json and that the LittleFS image has block headroom; run it after every
refresh.

## Drive it with Claude

The Claude integration is an MCP server, the Expression Studio, and Claude Code hooks that
turn the panel into Claude's ambient expression channel. It lives in a separate repo,
**[claude-expression-studio](https://github.com/srfinch17/claude-expression-studio)**. That
project talks to this board only over the HTTP API in [`docs/API.md`](docs/API.md), and the
board is optional for it (it has a board-free browser mode).

**This firmware repo contains no MCP server, and the board itself needs none**; the board
just answers HTTP. The one MCP server in the system is the studio's (it registers as
`expression-studio`). To drive the board from Claude, install that repo (`npm run setup`)
and point it here (`--board http://<board-ip>`).
