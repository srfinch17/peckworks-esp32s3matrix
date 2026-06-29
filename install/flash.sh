#!/usr/bin/env sh
# Requires esptool: `pip install esptool` (or `pipx install esptool`).
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
MERGED="$(ls "$DIR"/esp32matrix-*-merged.bin 2>/dev/null | head -n1)"
[ -n "$MERGED" ] || { echo "Merged .bin not found next to this script."; exit 1; }
echo "Flashing $MERGED ..."
python3 -m esptool --chip esp32s3 --baud 921600 write_flash 0x0 "$MERGED"
echo "Done. Find the board at http://esp32matrix.local"
