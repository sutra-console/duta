# zephyr (nRF52840 + any Zephyr board)

Duta on **Zephyr** (west + Zephyr SDK), built on the shared core
([`../common/skrit_device.h`](../common/skrit_device.h)). The headline target is the
**nRF52840** (DK and dongle); the same `main.c` runs on any Zephyr board you add an
overlay for. `native_sim` is the hardware-free CI build-check.

```sh
west build -b native_sim platforms/zephyr               # CI build-check
west build -b nrf52840dk/nrf52840 platforms/zephyr      # DK
west build -b nrf52840dongle/nrf52840 platforms/zephyr  # USB dongle
west flash                                              # (DK; dongle uses nrfutil/UF2)
```

(Run inside a Zephyr workspace, or point `ZEPHYR_BASE` at one.)

## Transport

DATA + CMD are **multiplexed over a USB CDC ACM port** ([skrit-mux](../../protocol/PROTOCOL.md)).
The target console is a **hardware UART** (`duta-data` devicetree alias → `uart1`). Bring
your own board by dropping a `boards/<board>.overlay` (a `cdc_acm_uart0` node under
`zephyr_udc0` + a `duta-data` alias) and a `boards/<board>.conf` (USB stack + CDC ACM).

| Board | DATA UART (TX/RX) | Outputs | Reboot-to-DFU |
|-------|-------------------|---------|---------------|
| `nrf52840dk/nrf52840` | P1.01 / P1.02 | LEDs led0–led2 | GPREGRET `0x57` |
| `nrf52840dongle/nrf52840` | P1.10 / P1.13 *(adjust)* | green + RGB LEDs | UF2/Open bootloader |

Optional `duta-dtr` / `duta-rts` GPIO aliases drive a target's reset/boot pins from
`SERIAL_SIGNAL` (enter ESP/AVR bootloaders).

## What it answers

`PING` · `INFO` (caps = mux + serial + reboot, `macro_tier = 2`) · `DEVICE_NAME` ·
`OUTPUT_SET/GET/TOGGLE/DESC/PULSE` · `SERIAL_GET/SET/SIGNAL` · `REBOOT` (app, or
GPREGRET→DFU on nRF52) · scratch (`0xFF`) macro push-and-run via the shared skrit-mc VM.

## Roadmap

- **BLE NUS transport** — carry the same mux stream over a GATT pipe (a natural fit for
  the nRF52840); advertise alongside or instead of USB.
- **Dual CDC ACM** — a second `cdc_acm_uart` instance for a raw, un-muxed DATA port that
  plain terminals can open directly.
- **Persistent macros** — back the `0x00..0xFE` ids with the settings/NVS subsystem.
- **Async input events** — push `EVENT_INPUT` on GPIO edges via `skrit_dev_emit_event`.
- **PWM** — wire the `pwm_set/pwm_get` HAL hooks to `pwm-leds` devicetree nodes
  (the ESP32/Pico ports answer `OUTPUT_PWM` already; here it reports unsupported).
