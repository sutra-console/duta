# Platforms

Each subdirectory is a firmware implementation of the
[skrit protocol](../protocol/README.md) for a different MCU/framework. They
all speak the same wire protocol, so the **same desktop app drives all of them**.

| Platform | Framework / toolchain | Targets | Transport | Status |
|----------|----------------------|---------|-----------|--------|
| [`common`](common) | header-only C (the shared core) | all C/C++ ports | — | ✅ |
| [`ch55xduino`](ch55xduino) | ch55xduino + arduino-cli (SDCC) | CH551/2/4 | USB dual-CDC | ✅ working |
| [`espressif`](espressif) | Arduino / PlatformIO | ESP32 / S3 / C3 | USB mux | ✅ working |
| [`pico`](pico) | arduino-pico / PlatformIO | RP2040 / RP2350 | USB mux | ✅ working |
| [`zephyr`](zephyr) | west + Zephyr SDK | nRF52840 (DK + dongle) | USB mux | ✅ working |
| [`host`](host) | CMake (native C) | Linux/macOS/Windows | WebSocket | ✅ reference |
| `micropython` | mpy-cross | RP2040 / ESP32 | USB mux · WebSocket · BLE | ⬜ planned |

Everything except `ch55xduino` (which has cheap composite-USB silicon) and the planned
MicroPython port builds on the [`common`](common) core; new C/C++ ports should too.

## Build system (QMK-style)

[`../targets.yml`](../targets.yml) is the canonical list of official build
targets `(platform, board, transport)`. CI ([firmware.yml](../.github/workflows/firmware.yml))
builds each on push/PR. **Forks add a board entry and inherit the whole CI** —
just like QMK keyboards.

## Adding a platform

1. `mkdir platforms/<name>` with the framework's project files.
2. Include [`../common/skrit_device.h`](common/skrit_device.h) (it pulls in
   [`../../protocol/protocol.h`](../protocol/protocol.h)).
3. Fill a `skrit_hal` vtable (byte I/O on your transport, GPIO for outputs, a
   `millis()`), `skrit_dev_init(..., muxed)`, and pump it from the main loop —
   the dispatch, macro VM, and framing are already done. The
   [`espressif`](espressif) / [`pico`](pico) ports are ~150-line templates.
4. Add target(s) to `targets.yml` and a job to the CI workflow.
