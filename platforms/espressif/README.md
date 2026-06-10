# espressif (ESP32 / ESP32-S3 / ESP32-C3)

Duta on ESP32 via **Arduino + PlatformIO**, built on the shared core
([`../common/skrit_device.h`](../common/skrit_device.h)). A full adapter, not a
skeleton: DATA console bridge, the skrit-mc macro VM (tiers 1–2), serial control,
and reboot-to-bootloader.

```sh
pio run                   # default env (esp32s3)
pio run -e esp32c3
pio run -e esp32 -t upload
```

## Transport

DATA + CMD are **multiplexed over the single USB-CDC** ([skrit-mux](../../protocol/PROTOCOL.md)):
the app opens one port and gets both the console and the control channel. The target
console is a **hardware UART** (`Serial1` on `DATA_TX/DATA_RX`). On the S3/C3 the mux
rides the native USB-CDC; on the classic ESP32 it rides UART0 (the on-board bridge).

| Board (`-DBOARD_*`) | DATA TX/RX | Relay 1/2 | LED | USB |
|---------------------|-----------|-----------|-----|-----|
| `ESP32S3` (default) | 17 / 18 | 4 / 5 | 2 | native CDC |
| `ESP32C3` | 21 / 20 | 4 / 5 | 8 | native CDC |
| `ESP32` | 17 / 16 | 25 / 26 | 2 | UART0 |

Edit [`src/board.h`](src/board.h) to remap pins or wire the optional `DTR/RTS`
auto-reset lines (`-1` = unused).

## What it answers

`PING` · `INFO` (caps = mux + serial + reboot + pwm, `macro_tier = 2`) · `DEVICE_NAME` ·
`OUTPUT_SET/GET/TOGGLE/DESC/PULSE/PWM` (Aux LED dims 0–1023) · `SERIAL_GET/SET/SIGNAL` (set DATA baud/parity,
drive DTR/RTS/BREAK to enter ESP/AVR bootloaders) · `REBOOT` (app, or download/DFU on
S3/C3) · `MACRO_WRITE_*`/`MACRO_RUN` for scratch (`0xFF`) push-and-run.

## Roadmap

- **Persistent macros** — back the `0x00..0xFE` macro ids with NVS/LittleFS (the core
  already has the VM; just needs a storage hook).
- **WiFi-TCP "bridge mode"** — accept a TCP client and feed a second muxed `skrit_dev`,
  so Sutra can reach the target over the network (mirrors [`../host`](../host)).
- **BLE NUS** — the same mux stream over a GATT pipe.
- **Analog inputs** — populate `n_inputs` + the `in_*` HAL callbacks (e.g. an LDR for
  `WAITIO`).
