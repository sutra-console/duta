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

### Reboot to bootloader (flash with no button)

The firmware honors skrit `REBOOT` with `mode = 1` (**bootloader/DFU**) by writing
`0x57` — the Adafruit/UF2 bootloader's "enter DFU" magic — to the nRF52
`GPREGRET` retained register, then doing a warm reset. The bootloader sees the
magic on boot and stays in DFU, mounting its `NICENANO`/`xxxBOOT` USB drive. So
**after the first UF2 flash you never press the button again** — the host drops
the board into the bootloader on demand.

- **Where**: `hal_reboot()` in `src/main.c`, gated on `CONFIG_SOC_SERIES_NRF52`
  (+ `CONFIG_REBOOT` for the reset). `mode = 0` is a plain app reset. Advertised
  via `SKRIT_CAP_REBOOT` in `INFO`.
- **From Sutra / MCP**: *Reboot → bootloader* (MCP `reboot_device bootloader=true`).
- **Headless flash flow** (what the bench scripts do):
  1. send `REBOOT mode=1` on the CMD channel → the `NICENANO` drive mounts,
  2. copy `build/zephyr/zephyr.uf2` onto it,
  3. the bootloader flashes + resets; the board re-enumerates running the new build.

  No physical access required — the whole build→flash→run loop is scriptable over
  the USB serial. (The DK/dongle answer `REBOOT` too; the DK's J-Link makes
  `west flash` the usual path there.)

A **custom Duta UF2 bootloader** ([`bootloader/`](bootloader/)) extends this
further: it IDs the board as a Duta (volume label `DUTA`, VID `0x1209`) and adds
the reverse trigger — a **1200-baud touch on its CDC exits DFU back to the app**,
so the bootloader↔app round trip is fully host-driven, no button.

### Demo INVOKE commands

The nRF build ships three example **INVOKE** commands (user-defined device
commands — see [`PROTOCOL.md`](../../protocol/PROTOCOL.md) → *INVOKE*) so the
framework's extension point can be exercised on hardware:

| id | command | effect |
|----|---------|--------|
| `0x0001` | `set_position(x:u16, y:u16)` | blips the status LED as an ack |
| `0x8001` | `blink(times:u8)` | blinks the status LED `times` (visible proof) |
| `0x8002` | `echo(bytes) -> reply` | echoes the payload back (the reply path) |

List them with `INVOKE_DESC`; call them with `INVOKE` (or Sutra's
`list_invocables` / `invoke`). `blink` runs synchronously (`k_msleep`), so its
reply lands after the blinks finish — a real handler would be non-blocking.

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

### Unified runtime-switchable sniffer (BLE + 802.15.4)

The nRF radio does both BLE and 802.15.4, so one image can carry both backends
and switch the live PHY at runtime — no reflash to move between protocols:

```sh
west build -b promicro_nrf52840/nrf52840 platforms/zephyr -- -DEXTRA_CONF_FILE=overlay-sniff-multi.conf
```

Both `duta_ble_sniff.c` and `duta_154_sniff.c` are compiled in and dispatched
through a vtable (`duta_sniffer_multi.c`). The host switches with **`CFG_SET` key
`0x14`** (`SKRIT_CFG_DATA_KIND`): `4` = BLE advertising (ch 37/38/39), `7` = IEEE
802.15.4 (ch 11–26), and **`0` = UART bridge** — parks the radio and forwards the
hardware DATA UART as the console (the same bridge the non-sniffer build runs), so
one image is a UART bridge *and* both sniffers. A switch stops the old mode,
re-inits the radio for the target (or parks it for UART), and resumes; `DATA_DESC`
then reports the active kind so the Wireshark extcap picks the matching DLT
(uart → console pcap). The Controls panel's radio toggle covers BLE↔802.15.4;
UART is reached via the CFG key. **Zigbee, Thread, and Matter-over-Thread
are all kind `7`** — the same 802.15.4 capture, told apart only by the Wireshark
dissector (Matter-over-Wi-Fi needs a Wi-Fi radio, not the nRF). Boots in BLE mode.
The single-PHY `overlay-sniff.conf` / `overlay-sniff154.conf` builds remain as
lighter options.

### Sniffer backends are pluggable

Both sniffers sit behind a portable contract in
[`platforms/common/duta_sniffer.h`](../common/duta_sniffer.h): `main.c` calls only
`duta_sniffer_init/start/stop/is_on/take` (+ `_tx`/`_set_channel`/`_get_channel`
when `DUTA_SNIFF_HAS_TX`/`DUTA_SNIFF_HAS_CHANNEL` are set) and reads
`DUTA_SNIFF_KIND` — it never names a concrete backend. A new radio (an ESP32
802.15.4 PHY, a CC2531, an nRF24) becomes a sniffer by implementing that handful
of functions and adding one selection arm to the facade; the app loop is unchanged.
The nRF backends here (`duta_154_sniff.c`, `duta_ble_sniff.c`) are the reference
implementations.

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
GPREGRET→DFU on nRF52, see *Reboot to bootloader*) · `INVOKE_DESC`/`INVOKE` (the
demo user-defined commands above) · scratch (`0xFF`) macro push-and-run via the
shared skrit-mc VM (incl. the `INVOKE` opcode).

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
