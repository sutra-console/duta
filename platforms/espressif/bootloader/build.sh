#!/usr/bin/env bash
# Build the Duta TinyUF2 bootloader for the ESP32-S3-Zero = TinyUF2 (pinned) +
# our in-tree board variant (boards/duta_s3zero), overlaid onto the closest
# stock board (feather_esp32s3_nopsram: ESP32-S3, 4MB, no PSRAM).
#
# Prereq (NOT vendored): a full ESP-IDF environment with `idf.py` on PATH
# (run IDF's export.sh / export.ps1 first). nrfutil is NOT needed here.
set -euo pipefail

UPSTREAM_URL="https://github.com/adafruit/tinyuf2.git"
UPSTREAM_SHA="34adc54451e9686cab933d84210d889487df5113" # bump deliberately
STOCK_BOARD="adafruit_feather_esp32s3_nopsram"          # 4MB / no-PSRAM S3 base
BOARD="duta_s3zero"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work="${here}/.upstream"
dist="${here}/dist"

if [ ! -d "${work}/.git" ]; then
  echo "==> cloning tinyuf2 @ ${UPSTREAM_SHA}"
  git clone "${UPSTREAM_URL}" "${work}"
fi
git -C "${work}" fetch --depth 1 origin "${UPSTREAM_SHA}" 2>/dev/null || git -C "${work}" fetch origin
git -C "${work}" checkout -q "${UPSTREAM_SHA}"
echo "==> submodules"
git -C "${work}" submodule update --init --depth 1

# Base on a known-good stock board, then overlay only our identity board.h.
boards="${work}/ports/espressif/boards"
echo "==> overlaying boards/${BOARD} (base: ${STOCK_BOARD})"
rm -rf "${boards:?}/${BOARD}"
cp -r "${boards}/${STOCK_BOARD}" "${boards}/${BOARD}"
cp "${here}/boards/${BOARD}/board.h" "${boards}/${BOARD}/board.h"

echo "==> idf.py -DBOARD=${BOARD} build  (needs an exported ESP-IDF)"
( cd "${work}/ports/espressif" && idf.py -DBOARD="${BOARD}" build )

mkdir -p "${dist}"
cp "${work}/ports/espressif/build/tinyuf2.bin" "${dist}/duta_s3zero_tinyuf2.bin"
echo "==> wrote ${dist}/duta_s3zero_tinyuf2.bin"
echo "    flash with esptool (see README); the ROM downloader is the recovery net."
