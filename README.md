# Duta

**Duta** is the *messenger* — firmware that turns a cheap MCU into a smart serial
adapter: a transparent UART bridge plus a control channel for relays / LED / GPIO
/ inputs and self-describe. It speaks **[skrit](https://github.com/sutra-console/skrit)**
(vendored in [`protocol/`](protocol/)), so it pairs with the
**[Sutra](https://github.com/sutra-console/sutra)** desktop app over USB / BLE / WebSocket.

> *dūta* (Sanskrit दूत) — *"messenger / envoy"; the one who carries word.*

One firmware family, one protocol, many MCUs — fork it, add a board, inherit the
whole build system (QMK-style).

## Layout

```
platforms/     firmware, one thin HAL per MCU on a shared core (all speak skrit)
  common/        the portable core: dispatch + macro VM + mux + provisioning     ✅
  ch55xduino/    CH55x dual-CDC adapter (arduino-cli + SDCC)                     ✅ working
  espressif/     ESP32 / S3 / C3 / Waveshare S3-Zero (Arduino/PlatformIO)        ✅ working
  pico/          RP2040 / RP2350 — Pico & Pico 2 (arduino-pico/PlatformIO)       ✅ working
  zephyr/        nRF52840 DK + dongle, any Zephyr board (west)                   ✅ working
  host/          native reference over WebSocket                                 ✅ hardware-free CI
protocol/      vendored skrit contract (PROTOCOL.md + protocol.h)
BOARDS.md      the board model: platform → mcu → board → target
targets.yml    QMK-style list of build targets
.github/       build-check CI matrix
```

Every C/C++ port shares [`platforms/common/skrit_device.h`](platforms/common/) — the
protocol dispatch, the skrit-mc macro VM (tiers 1–2), and both transport framings
(dual-CDC and [skrit-mux](protocol/PROTOCOL.md)) live there once. A platform supplies a
small HAL (drive a pin, read a UART byte, get a tick) and a two-line main loop.

| Transport | Boards | DATA + CMD |
|-----------|--------|-----------|
| **usb-dual-cdc** | CH552 | two USB-CDC ports (raw DATA + framed CMD) |
| **usb-mux** | ESP32(-S3/C3), S3-Zero, Pico, Pico 2, nRF52840 | one USB-CDC, multiplexed (skrit-mux) |
| **ble** | nRF52840 *(scaffold)* | dual: two skrit GATT services (DATA console + CMD) |
| **websocket** | host, ESP32 WiFi *(roadmap)* | mux over WS binary frames; auth-gated (`AUTH`) |

## Boards — `platform → mcu → board → target`

Board support is layered (see **[BOARDS.md](BOARDS.md)**): a per-chip **mcu** map owns
the silicon truth (pin inventory, capabilities, strapping/flash hazards), a **vendored
board** header owns the physical facts (breakout, onboard hardware), and a **target**
owns our wiring (which pins are the relays, the DATA UART, the LED). Adding a board is
a one-file PR against facts; making it a Duta is a small target that includes it.

On boards that advertise the **provision** flag (ESP32 family), the IO table is also
**runtime-provisionable**: the app reads the offerable-pin menu (`PIN_CAPS`), writes a
new table (`CONFIG_SET`, validated against the pin map, persisted to NVS), and a reboot
applies it — re-pin a relay without recompiling.

## Build & flash

[`targets.yml`](targets.yml) lists the official targets `(platform, board,
transport)`; [CI](.github/workflows/firmware.yml) builds each on push/PR. Fork,
add your board, inherit the build system. Full instructions (incl. first-flash
bootloader entry) live in each `platforms/<name>/README.md`; the short version:

```sh
# ESP32 family (PlatformIO) — platforms/espressif/
pio run -e esp32s3 -t upload      # envs: esp32s3 · s3zero · esp32c3 · esp32

# Pico / Pico 2 (PlatformIO) — platforms/pico/
pio run -e pico -t upload         # first flash: hold BOOTSEL (UF2)

# nRF52840 (Zephyr) — from a Zephyr workspace
west build -b nrf52840dk/nrf52840 platforms/zephyr && west flash

# CH552 (arduino-cli; core pinned @0.0.25 — see the platform README)
arduino-cli compile --fqbn "CH55xDuino:mcs51:ch552:usb_settings=user266" platforms/ch55xduino

# host reference (CMake) — a runnable WebSocket Duta, no hardware
cmake -B build platforms/host && cmake --build build && ./build/duta-host 9555
```

Once a Duta is flashed, **re-flashing never needs a button**: the app's
*Reboot → bootloader* (skrit `REBOOT`) drops the board into its DFU/UF2/download
mode over the protocol.

## Adding a platform

Include [`platforms/common/skrit_device.h`](platforms/common/), fill a `skrit_hal`
vtable (byte I/O on your transport, GPIO for outputs, a `millis()`), construct the
device dual or muxed, and pump it from your main loop. Add a `targets.yml` entry — and
the Sutra app + MCP server (plus the macro VM and the whole protocol) come for free.
The ESP32 / Pico / Zephyr ports are ~150-line examples. See
[platforms/common/README.md](platforms/common/README.md) and
[platforms/README.md](platforms/README.md). To add a **board** to an existing
platform, see [BOARDS.md](BOARDS.md).

> The `protocol/` here is a vendored copy; the canonical home is the
> [skrit](https://github.com/sutra-console/skrit) repo.
