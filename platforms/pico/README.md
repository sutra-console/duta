# pico (Raspberry Pi Pico / Pico 2)

Duta on the RP2040 / RP2350 via **Arduino + PlatformIO** (the
[earlephilhower arduino-pico core](https://github.com/earlephilhower/arduino-pico)),
built on the shared core ([`../common/skrit_device.h`](../common/skrit_device.h)) with
table-driven IO (the [`duta_io`](../common/duta_io_arduino.h) driver). A full adapter:
DATA console bridge, the skrit-mc macro VM (tiers 1–2), serial control, and
reboot-to-bootloader.

Board support follows [BOARDS.md](../../BOARDS.md): [`src/mcu/`](src/mcu) (silicon
truth), [`src/boards/raspberrypi/`](src/boards) (vendored board facts — e.g. GP25 is
the onboard LED and is *not* on a header), [`src/targets/`](src/targets) (our wiring);
[`src/board.h`](src/board.h) dispatches `-DBOARD_*` to a target.

## Build & flash

```sh
# from platforms/pico/
pio run                          # build the default env (pico)
pio run -e pico2                 # Pico 2 / RP2350
pio run -e pico -t upload        # flash
```

**First flash:** hold **BOOTSEL** while plugging in USB — the board mounts as a UF2
drive and `-t upload` (or dragging `.pio/build/<env>/firmware.uf2` onto it) flashes it.
After that the button is optional: `-t upload` resets a running Duta into BOOTSEL
automatically (1200-baud touch), and the app's *Reboot → bootloader* (skrit `REBOOT`
mode 1) does the same over the protocol.

## Transport

DATA + CMD are **multiplexed over the single native USB-CDC** ([skrit-mux](../../protocol/PROTOCOL.md)):
the app opens one port (`Serial`, the Pico's USB CDC) and gets both the console and the
control channel. The target console is a **hardware UART** (`Serial1` = UART0 on
`DATA_TX/DATA_RX`, i.e. GP0/GP1). Both the Pico and the Pico 2 expose the native USB,
so the mux always rides the on-chip USB CDC.

| Board (`-DBOARD_*`) | DATA TX/RX | Relay 1/2 | LED | USB |
|---------------------|------------|-----------|-----|-----|
| `PICO` (default) | 0 / 1 | 2 / 3 | 25 | native CDC |
| `PICO2` | 0 / 1 | 2 / 3 | 25 | native CDC |

Edit the target header ([`src/targets/`](src/targets)) to remap pins, add outputs (one
`duta_io` row per relay/LED/PWM, or wire a WS2812 strip as an RGB row), or set the
optional `DTR/RTS` auto-reset lines (`-1` = unused).

## What it answers

`PING` · `INFO` (caps = mux + serial + reboot + pwm, `macro_tier = 2`) · `DEVICE_NAME` ·
`DATA_DESC` (uart) · `OUTPUT_SET/GET/TOGGLE/DESC/PULSE/PWM` (Aux LED dims 0–1023) ·
`PWM_CONFIG` (frequency + resolution get/set) · `SERIAL_GET/SET/SIGNAL` (set DATA
baud/parity, drive DTR/RTS/BREAK to enter ESP/AVR bootloaders) · `REBOOT` (app, or the
RP2040/RP2350 UF2/BOOTSEL bootloader) · `MACRO_WRITE_*`/`MACRO_RUN` for scratch (`0xFF`)
push-and-run.

## Roadmap

- **Runtime provisioning** — the mcu/board maps are in place; wire `DUTA_PROVISION` +
  LittleFS persistence in `main.cpp` (mirrors the ESP32 port's NVS hooks) to accept IO
  re-provisioning from the app.
- **Persistent macros** — back the `0x00..0xFE` macro ids with LittleFS on the on-board
  flash (the core already has the VM; just needs a storage hook).
- **Dual-CDC mode** — expose a second TinyUSB CDC interface so DATA gets its own raw
  port (no mux), mirroring the CH552 dual-CDC transport.
- **PIO-based extra UARTs** — use the RP2040/RP2350 PIO blocks to bridge additional
  target consoles beyond the single hardware UART0.
