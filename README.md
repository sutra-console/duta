# Duta

**Duta** is the *messenger* — firmware that turns a cheap MCU into a smart serial
adapter: a transparent UART bridge plus a control channel for relays / LED / GPIO
/ inputs and self-describe. It speaks **[skrit](https://github.com/sutra-console/skrit)**
(vendored in [`protocol/`](protocol/)), so it pairs with the
**[Sutra](https://github.com/sutra-console/sutra)** desktop app over USB / TCP / BLE.

> *dūta* (Sanskrit दूत) — *"messenger / envoy"; the one who carries word.*

One firmware family, one protocol, many MCUs — fork it, add a board, inherit the
whole build system (QMK-style).

## Layout

```
platforms/     firmware, one thin HAL per MCU on a shared core (all speak skrit)
  common/        the portable core: dispatch + macro VM + mux (header-only) ✅
  ch55xduino/    CH55x dual-CDC adapter                                     ✅ working
  espressif/     ESP32 / ESP32-S3 / ESP32-C3 (Arduino/PlatformIO)           ✅ working
  pico/          RP2040 / RP2350 — Pico & Pico 2 (arduino-pico/PlatformIO)  ✅ working
  zephyr/        nRF52840 DK + dongle, any Zephyr board (west)              ✅ working
  host/          native reference over TCP                                  ✅ hardware-free CI
protocol/      vendored skrit contract (PROTOCOL.md + protocol.h)
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
| **usb-mux** | ESP32(-S3/C3), Pico, Pico 2, nRF52840 | one USB-CDC, multiplexed (skrit-mux) |
| **ble** | nRF52840 *(scaffold)* | Nordic UART Service GATT pipe, multiplexed |
| **tcp** | host, native_sim | one socket, multiplexed |

## Build

[`targets.yml`](targets.yml) lists the official targets `(platform, board,
transport)`; [CI](.github/workflows/firmware.yml) builds each on push/PR. Fork,
add your board, inherit the build system. Per-platform instructions live in each
`platforms/<name>/README.md`.

## Adding a platform

Include [`platforms/common/skrit_device.h`](platforms/common/), fill a `skrit_hal`
vtable (byte I/O on your transport, GPIO for outputs, a `millis()`), construct the
device dual or muxed, and pump it from your main loop. Add a `targets.yml` entry — and
the Sutra app + MCP server (plus the macro VM and the whole protocol) come for free.
The ESP32 / Pico / Zephyr ports are ~150-line examples. See
[platforms/common/README.md](platforms/common/README.md) and
[platforms/README.md](platforms/README.md).

> The `protocol/` here is a vendored copy; the canonical home is the
> [skrit](https://github.com/sutra-console/skrit) repo.
