#!/usr/bin/env bash
# Build the Duta UF2 bootloader = Adafruit_nRF52_Bootloader (pinned) + our
# in-tree board variant (boards/duta_nrf52840). Produces the self-update UF2.
#
# Prereqs (NOT vendored): arm-none-eabi-gcc + make on PATH, Nordic `nrfutil`
# (for the UF2 self-update package). We carry only the board variant + the
# exit-to-app file; everything else comes from the pinned upstream checkout.
set -euo pipefail

UPSTREAM_URL="https://github.com/adafruit/Adafruit_nRF52_Bootloader.git"
UPSTREAM_SHA="c67f0bcf0fa8e841426335b1bbde91cda6ca1f50" # master @ 2024; bump deliberately
BOARD="duta_nrf52840"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work="${here}/.upstream"
dist="${here}/dist"

if [ ! -d "${work}/.git" ]; then
  echo "==> cloning Adafruit_nRF52_Bootloader @ ${UPSTREAM_SHA}"
  git clone "${UPSTREAM_URL}" "${work}"
fi
git -C "${work}" fetch --depth 1 origin "${UPSTREAM_SHA}" 2>/dev/null || git -C "${work}" fetch origin
git -C "${work}" checkout -q "${UPSTREAM_SHA}"
echo "==> submodules (tinyusb, nrfx, uf2, tinycrypt, sdk…)"
git -C "${work}" submodule update --init --depth 1

echo "==> overlaying boards/${BOARD}"
cp -r "${here}/boards/${BOARD}" "${work}/src/boards/${BOARD}"

echo "==> make BOARD=${BOARD} all"
make -C "${work}" BOARD="${BOARD}" all

mkdir -p "${dist}"
uf2="$(ls "${work}"/_build/build-"${BOARD}"/update-"${BOARD}"_bootloader-*_nosd.uf2 | head -1)"
cp "${uf2}" "${dist}/"
echo "==> wrote ${dist}/$(basename "${uf2}")"
echo "    drag it onto the current bootloader's drive (SWD pads are the backup)."
