# zephyr (nRF52840 + any Zephyr board)

Duta on **Zephyr** (west + Zephyr SDK), built on the shared core
([`../common/skrit_device.h`](../common/skrit_device.h)). The headline target is the
**nRF52840** (DK and dongle); the same `main.c` runs on any Zephyr board you add an
overlay for. `native_sim` is the hardware-free CI build-check.

```sh
west build -b native_sim platforms/zephyr                  # CI build-check
west build -b nrf52840dk/nrf52840 platforms/zephyr         # DK (USB CDC)
west build -b nrf52840dongle/nrf52840 platforms/zephyr     # USB dongle
west build -b promicro_nrf52840/nrf52840 platforms/zephyr  # nice!nano v2 / SuperMini clone
west flash                                                 # (DK; UF2 boards: see below)

# BLE transport instead of USB (two skrit GATT services):
west build -b nrf52840dk/nrf52840 platforms/zephyr -- -DEXTRA_CONF_FILE=overlay-ble.conf
```

### Pro Micro nRF52840 (nice!nano v2 compatible)

The `promicro_nrf52840/nrf52840` board is the upstream Zephyr target for the
**nice!nano v2** and the SuperMini / "nRF52840 dev board compatible with Nice!Nano"
clones — a Pro-Micro-footprint nRF52840 with a USB-C port, LiPo charger, an onboard
blue LED (P0.15), and the **Adafruit UF2 bootloader**. It has no debug header, so
flash over **UF2**, not `west flash`:

1. `west build -b promicro_nrf52840/nrf52840 platforms/zephyr`
2. **Double-tap reset** — a `NICENANO` (or `xxxBOOT`) USB drive appears.
3. Drag `build/zephyr/zephyr.uf2` onto it; the board reboots running Duta.

After the first flash you never touch the button again: the app's *Reboot →
bootloader* (skrit `REBOOT`, GPREGRET `0x57`) drops it back into the UF2 drive.

BLE works the same as on the DK — add `-DEXTRA_CONF_FILE=overlay-ble.conf`; the
nice!nano's whole appeal is the untethered BLE link to Sutra.

### BLE advertising sniffer variant

The nRF52840 is also a capable BLE sniffer. Build with `overlay-sniff.conf` and
the DATA channel becomes captured advertising packets (`DATA_DESC = ble-sniff`)
instead of a UART console:

```sh
west build -b promicro_nrf52840/nrf52840 platforms/zephyr -- -DEXTRA_CONF_FILE=overlay-sniff.conf
```

It drives the radio directly (so it's mutually exclusive with the BLE
*transport* — `CONFIG_BT` must stay off), listens promiscuously on advertising
channels 37/38/39, and emits each captured PDU as one DATA record
(`ts · channel · rssi · access-address · pdu`; bad-CRC drops). Hardware-verified
on the nice!nano — ~70 packets/s of real nearby advertising, decodable down to
the manufacturer-data company IDs. v1 is advertising-only; connection-following
is future work.

(Run inside a Zephyr workspace, or point `ZEPHYR_BASE` at one.)

## Transport

DATA + CMD are **multiplexed over one channel** ([skrit-mux](../../protocol/PROTOCOL.md)),
selected at build time:

- **USB CDC ACM** (default): a `cdc_acm_uart0` node under `zephyr_udc0`.
- **BLE, dual-channel** (`overlay-ble.conf`, sets `CONFIG_BT`): two **skrit GATT
  services**. DATA carries the raw console (its UUID is NUS-compatible, so plain
  BLE-UART terminals read it) and the sibling CMD service (`6E41…`) carries the framed
  CMD protocol. Not muxed. The device advertises the CMD UUID + a `Duta`-prefixed name.
  **Scaffold**: build-checked in CI, not yet hardware-validated.

The target console is a **hardware UART** (`duta-data` devicetree alias → `uart1`) in
both. Bring your own board by dropping a `boards/<board>.overlay` (a `cdc_acm_uart0` node
under `zephyr_udc0` + a `duta-data` alias) and a `boards/<board>.conf` (USB stack + CDC ACM).

| Board | DATA UART (TX/RX) | Outputs | Reboot-to-DFU |
|-------|-------------------|---------|---------------|
| `nrf52840dk/nrf52840` | P1.01 / P1.02 | LEDs led0-led2 | GPREGRET `0x57` |
| `nrf52840dongle/nrf52840` | P1.10 / P1.13 *(adjust)* | green + RGB LEDs | UF2/Open bootloader |
| `promicro_nrf52840/nrf52840` | P0.09 / P0.10 (TX/RX pads) | blue LED (P0.15) + relays P0.06/P0.08 | GPREGRET `0x57` → UF2 |

Optional `duta-dtr` / `duta-rts` GPIO aliases drive a target's reset/boot pins from
`SERIAL_SIGNAL` (enter ESP/AVR bootloaders).

## What it answers

`PING` · `INFO` (caps = mux + serial + reboot, `macro_tier = 2`) · `DEVICE_NAME` ·
`OUTPUT_SET/GET/TOGGLE/DESC/PULSE` · `SERIAL_GET/SET/SIGNAL` · `REBOOT` (app, or
GPREGRET→DFU on nRF52) · scratch (`0xFF`) macro push-and-run via the shared skrit-mc VM.

## Roadmap

- BLE transport: *scaffolded* (`overlay-ble.conf`); the Sutra app has a BLE
  central (Scan ▸ connect). Needs on-hardware validation: notify flow-control, MTU
  negotiation, pairing/bonding policy.
- Dual CDC ACM: a second `cdc_acm_uart` instance for a raw, un-muxed DATA port that
  plain terminals can open directly.
- Persistent macros: back the `0x00..0xFE` ids with the settings/NVS subsystem.
- Async input events: push `EVENT_INPUT` on GPIO edges via `skrit_dev_emit_event`.
- PWM: wire the `pwm_set/pwm_get` HAL hooks to `pwm-leds` devicetree nodes
  (the ESP32/Pico ports answer `OUTPUT_PWM` already; here it reports unsupported).
