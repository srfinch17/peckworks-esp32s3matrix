# ESP32-S3 Matrix — Firmware

Firmware + onboard web UI for the **Waveshare ESP32-S3-Matrix** (8×8 / 64 WS2812B RGB
LEDs on an ESP32-S3 with a 6-axis IMU). It runs **standalone — no computer required**:
self-onboards onto WiFi via a captive portal, serves its own web UI to pick and tune
animations, shows weather/clock, includes a calibration lab, and exposes an HTTP API.

## Features

- **8×8 animation engine** — fire, matrix rain, fireworks, dance floor, comet, liquid
  (IMU-reactive), and more, each with its own web control page.
- **WiFi captive-portal onboarding** — joins your network with no hardcoded credentials;
  reachable at `http://esp32matrix.local`.
- **Onboard web UI** — animation selector, brightness, weather/clock modes, settings, and
  a calibration lab — all served from the device.
- **HTTP API** — full surface in [`docs/API.md`](docs/API.md); persistent settings + last
  animation survive reboots (NVS).

## Flash it

End users flash a single pre-merged binary from [`install/`](install/) (ESP Web Tools
browser button, or offline `flash.bat`/`flash.sh`) — no separate web-file upload.
Developers build it with `npm run build:release`.

For the Arduino IDE developer setup (board settings, libraries, LittleFS upload), see
[`CLAUDE.md`](CLAUDE.md) and [`docs/PITFALLS.md`](docs/PITFALLS.md).

## Drive it with Claude

The Claude integration — an MCP server, the Expression Studio, and Claude Code hooks that
turn the panel into Claude's ambient expression channel — lives in a separate repo:
**[claude-expression-studio](https://github.com/srfinch17/claude-expression-studio)**. It
talks to this board only over the HTTP API in [`docs/API.md`](docs/API.md); the board is
optional for that project (it has a board-free browser mode).
