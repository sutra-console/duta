# pico (Raspberry Pi Pico / Pico 2)

Duta on the RP2040 / RP2350 via **Arduino + PlatformIO** (the
[earlephilhower arduino-pico core](https://github.com/earlephilhower/arduino-pico)),
built on the shared core ([`../common/skrit_device.h`](../common/skrit_device.h)). A
full adapter, not a skeleton: DATA console bridge, the skrit-mc macro VM (tiers 1–2),
serial control, and reboot-to-bootloader.

```sh
pio run                   # default env (pico)
pio run -e pico2
pio run -e pico2 -t upload
```

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

Edit [`src/board.h`](src/board.h) to remap pins or wire the optional `DTR/RTS`
auto-reset lines (`-1` = unused).

## What it answers

`PING` · `INFO` (caps = mux + serial + reboot + pwm, `macro_tier = 2`) · `DEVICE_NAME` ·
`OUTPUT_SET/GET/TOGGLE/DESC/PULSE/PWM` (Aux LED dims 0–1023) · `SERIAL_GET/SET/SIGNAL` (set DATA baud/parity,
drive DTR/RTS/BREAK to enter ESP/AVR bootloaders) · `REBOOT` (app, or the RP2040/RP2350
UF2/BOOTSEL bootloader) · `MACRO_WRITE_*`/`MACRO_RUN` for scratch (`0xFF`) push-and-run.

## Roadmap

- **Persistent macros** — back the `0x00..0xFE` macro ids with LittleFS on the on-board
  flash (the core already has the VM; just needs a storage hook).
- **Dual-CDC mode** — expose a second TinyUSB CDC interface so DATA gets its own raw
  port (no mux), mirroring the CH552 dual-CDC transport.
- **PIO-based extra UARTs** — use the RP2040/RP2350 PIO blocks to bridge additional
  target consoles beyond the single hardware UART0.
