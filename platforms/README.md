# Platforms

Each subdirectory is a firmware implementation of the
[Duta protocol](../protocol/README.md) for a different MCU/framework. They
all speak the same wire protocol, so the **same desktop app drives all of them**.

| Platform | Framework / toolchain | Targets | Transports | Status |
|----------|----------------------|---------|------------|--------|
| [`ch55xduino`](ch55xduino) | ch55xduino + arduino-cli (SDCC) | CH551/2/4 | USB dual-CDC | ✅ working |
| [`espressif`](espressif) | Arduino / PlatformIO | ESP32 / S3 / C3 | USB·UART · TCP · BLE | 🚧 scaffold |
| [`zephyr`](zephyr) | west + Zephyr SDK | nRF52840 / ESP32 / RP2040 | USB · BLE | 🚧 scaffold |
| [`host`](host) | CMake (native C) | Linux/macOS/Windows | TCP | 🚧 reference |
| `micropython` | mpy-cross | RP2040 / ESP32 | USB · TCP · BLE | ⬜ planned |

## Build system (QMK-style)

[`../targets.yml`](../targets.yml) is the canonical list of official build
targets `(platform, board, transport)`. CI ([firmware.yml](../.github/workflows/firmware.yml))
builds each on push/PR. **Forks add a board entry and inherit the whole CI** —
just like QMK keyboards.

## Adding a platform

1. `mkdir platforms/<name>` with the framework's project files.
2. Include [`../../protocol/protocol.h`](../protocol/protocol.h) (or mirror it).
3. Wire a transport, run the frame loop, answer `PING`/`INFO`/`DEVICE_NAME`/
   `OUTPUT_DESC` (+ outputs/snippets as the hardware allows).
4. Add target(s) to `targets.yml` and a job to the CI workflow.
