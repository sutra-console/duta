# espressif (ESP32 / ESP32-S3 / ESP32-C3 / Waveshare S3-Zero)

Duta on ESP32 via **Arduino + PlatformIO**, built on the shared core
([`../common/skrit_device.h`](../common/skrit_device.h)) with table-driven IO and
**runtime provisioning**. A full adapter: DATA console bridge, the skrit-mc macro VM
(tiers 1–2), serial control, PWM frequency/resolution, addressable RGB, and
reboot-to-bootloader.

## Build & flash

```sh
# from platforms/espressif/
pio run                          # build the default env (esp32s3)
pio run -e s3zero                # or: esp32s3 / esp32c3 / esp32
pio run -e s3zero -t upload      # flash (auto-detects the port)
pio device monitor               # optional: watch the CDC output
```

**First flash on native-USB boards (S3 / C3 / S3-Zero):** these have no USB-UART
bridge, so a board that isn't already running Arduino firmware won't auto-enter the
bootloader — **hold BOOT while plugging in USB** (the ROM downloader enumerates), then
`-t upload`. After the first flash you never touch the button again: the app's
*Reboot → bootloader* (skrit `REBOOT` mode 1) re-enters the downloader over USB.

**Classic ESP32 DevKit:** the onboard CP2102 + auto-reset circuit handle bootloader
entry; plain `-t upload` always works.

## Board layout — `mcu/ → boards/<vendor>/ → targets/`

Board support follows [BOARDS.md](../../BOARDS.md): [`src/mcu/`](src/mcu) holds the
per-chip silicon truth (pin caps + strapping/flash hazards), [`src/boards/`](src/boards)
holds **vendored board facts** (breakout, onboard hardware — no role choices), and
[`src/targets/`](src/targets) holds **our wiring** (which pins are the relays, the DATA
UART, the LED). [`src/board.h`](src/board.h) dispatches `-DBOARD_*` to a target. To
bring up your own build, base a new target off a vendored board.

## Transport

DATA + CMD are **multiplexed over the single USB-CDC** ([skrit-mux](../../protocol/PROTOCOL.md)):
the app opens one port and gets both the console and the control channel. The target
console is a **hardware UART** (`Serial1` on `DATA_TX/DATA_RX`). On native-USB chips
(S3/C3/S3-Zero) the mux rides the USB-CDC; on the classic ESP32 it rides UART0 (the
on-board bridge).

| Target (`-DBOARD_*`) | Vendored board | DATA TX/RX | Relay 1/2 | LED | RGB | USB |
|----------------------|----------------|-----------|-----------|-----|-----|-----|
| `ESP32S3` (default) | Espressif DevKitC-1 | 17 / 18 | 4 / 5 | 2 | 48 (onboard) | native CDC |
| `S3_ZERO` | Waveshare ESP32-S3-Zero | 43 / 44 (the TX/RX pins) | 4 / 5 | 6 | 21 (onboard, fixed) | native CDC |
| `ESP32C3` | Espressif DevKitM-1 | 21 / 20 | 4 / 5 | 6 | 8 (onboard) | native CDC |
| `ESP32` | classic DevKit | 17 / 16 | 25 / 26 | 2 (onboard) | — | UART0 |

Remap pins in the target header (or provision at runtime, below); the optional
`DTR/RTS` auto-reset lines are per-target (`-1` = unused).

## Runtime provisioning

These targets advertise `FLAG_PROVISION`: the IO table can be **re-provisioned from the
app without reflashing**. The device resolves its offerable-pin menu from the mcu map ∩
the board overlay (forbidden/fixed pins hidden; strapping/dual-use pins warned), validates
every `CONFIG_SET` row against it, and persists the table to **NVS** (`Preferences`); a
reboot applies it. `CONFIG_SET n=0xFF` reverts to the compiled default.

## What it answers

`PING` · `INFO` (caps = mux + serial + reboot + pwm; flags = provision, `macro_tier = 2`) ·
`DEVICE_NAME` · `DATA_DESC` (uart) ·
`OUTPUT_SET/GET/TOGGLE/DESC/PULSE/PWM/RGB` (Aux LED dims 0–1023; the onboard WS2812 is an
addressable RGB output via FastLED — per-pixel, raise `RGB_COUNT` for a strip) ·
`PWM_CONFIG` (frequency + resolution get/set; per-pin on arduino-esp32 core 3.x) ·
`PIN_CAPS` / `CONFIG_GET` / `CONFIG_SET` (provisioning) ·
`SERIAL_GET/SET/SIGNAL` (set DATA baud/parity, drive DTR/RTS/BREAK to enter ESP/AVR
bootloaders) · `REBOOT` (app, or download/DFU on native-USB chips) ·
`MACRO_WRITE_*`/`MACRO_RUN` for scratch (`0xFF`) push-and-run.

## Toolchain notes

- **FastLED is pinned to 3.9.13** — 3.10+ ships an audio module that doesn't compile
  against the arduino-esp32 3.x IDF headers.
- Include paths in `platformio.ini` use `${platformio.include_dir}` (parse-time
  absolute), **not** `${PROJECT_DIR}` — recent espressif32 builders mangle the latter.
- arduino-esp32 **3.x** made `analogWriteFrequency/Resolution` per-pin; the shared
  driver handles both 2.x and 3.x.

## Roadmap

- **Persistent macros** — back the `0x00..0xFE` macro ids with NVS/LittleFS (the core
  already has the VM; just needs a storage hook).
- **WiFi WebSocket "bridge mode"** — run a WS server (esp_http_server) feeding a muxed `skrit_dev`,
  so Sutra can reach the target over the network (mirrors [`../host`](../host)).
- **BLE** — the dual skrit GATT services (DATA + CMD; mirrors the Zephyr scaffold).
- **Analog inputs** — populate `n_inputs` + the `in_*` HAL callbacks (e.g. an LDR for
  `WAITIO`).
