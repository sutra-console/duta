# Duta on the Flipper Zero

A Flipper Zero **external app** (`.fap`) that turns the Flipper into a Duta skrit
node — so Sutra (or the MCP server) talks to it like any other Duta.

| Role | Flipper hardware |
|------|------------------|
| Host link (skrit-mux: CMD + DATA) | the Flipper's **USB CDC** — Sutra opens it like a serial Duta |
| DATA console bridge | the **GPIO-header USART** — pin **13** TX / pin **14** RX, to a target |
| Outputs | GPIO-header pins **PA7, PA6, PA4, PB3** (digital on/off) + the onboard **RGB LED** (an `OUTPUT_RGB`) |
| Reboot | resets the Flipper |

The screen shows the link state, DATA baud + byte counters, and the live output
states. **Back** exits the app (and restores the Flipper's normal USB/CLI).

## Build

Uses [`ufbt`](https://github.com/flipperdevices/flipperzero-ufbt) (micro Flipper
Build Tool). From this directory:

```sh
pip install ufbt          # or: uv tool install ufbt
ufbt update               # fetch the SDK (once)
ufbt                      # build -> dist/duta.fap
ufbt launch               # build + upload + run on a connected Flipper
```

## Use

1. Install `dist/duta.fap` to the Flipper (`ufbt launch`, or copy to
   `SD:/apps/GPIO/`) and run it (Apps → GPIO → Duta).
2. Plug the Flipper into the PC over USB. It enumerates as a serial port.
3. In Sutra, connect to that serial port — it discovers a `Duta Flipper` and you
   get the console, outputs (the four GPIO pins + RGB LED), and macros.
4. Wire a target's UART to header pins 13/14 (+ GND) to bridge its console.

## Passthrough bridge (USB ⟷ UART)

From the status screen press **▶ (Right)** to enter **Bridge** mode: the Flipper
stops being its own skrit node and wires its USB CDC straight to the GPIO-header
UART (pin 13 TX / 14 RX), byte-for-byte. Sutra (or anything on the serial port)
then talks **straight through** to whatever is on the UART.

The headline use: hang a second Duta off the header UART — e.g. an **nRF52840
running Duta** — and the Flipper becomes a dumb wire to it. Sutra drives the
nRF52's BLE / 802.15.4 (Zigbee/Thread/Matter) radio as if it were plugged in
directly. Works for any UART Duta, not just nRF52.

- **Up/Down** cycle the bridge baud (9600 … 921600; default 115200 — match the
  downstream device). **Back** returns to node mode (and restores the DATA baud).
- The RGB LED glows **blue** while bridging; the screen shows the byte counters
  each way.
- One direction at a time: while bridging, the Flipper's own skrit node + macros
  are paused (it's a wire, not a node). This is the no-protocol-change bridge; a
  *multiplexed* router (Flipper stays a node AND forwards a second device) is
  future work pending a skrit device-addressing extension.

## Planned: Zigbee Router mode

Goal: the Flipper + an attached **nRF52840** (which has the 802.15.4 radio the
Flipper lacks) join a Zigbee network as a **router** — standalone, no PC — using
the network key + model from the SD card. It builds on Bridge mode (the nRF52
hangs off the header UART) and adds a Zigbee brain. **Not built yet**; two
architecture paths, decision pending:

1. **Dumb radio + Flipper brain** (matches the project thesis — see the Sutra
   network model). The nRF52 stays a raw 802.15.4 transceiver (`duta_154_sniff_tx`:
   it just TX/RXes MAC bytes, knows nothing of Zigbee); the **Flipper** runs the
   Zigbee NWK/APS layer — AES-128-CCM* crypto, frame-counter management, route
   relay, join handling — reading key/PAN/channel from
   `apps_data/duta/networks.json` (a copy of Sutra's `.sutra/networks.json`).
   *Pro:* one firmware serves Zigbee/Thread/Matter, evolves with no reflash.
   *Con:* we implement Zigbee routing on the Flipper. **Gated on network-model
   phase B** (the AES-CCM* frame builder — not started).

2. **Smart radio (ZBOSS) on the nRF52.** The nRF52 runs a conformant Zigbee
   router stack (nRF Connect SDK / ZBOSS); the Flipper just pushes the network
   key + role via skrit `CFG` from the SD card, then bridges. *Pro:* a real
   router nearly for free. *Con:* breaks the dumb-radio model; pulls in the nRF
   Connect SDK toolchain.

Either way: the Flipper supplies the keys (from SD) + the UI; the nRF52 supplies
the radio. The host-side crypto plan lives in the Sutra network model.

## Macros (on-device runner)

The Flipper can run **skrit-mc** macro bytecode straight from its SD card — no
Sutra round-trip. From the status screen press **OK** to open the macro list;
**Up/Down** to pick, **OK** to run, **Back** to return.

- Macros live in `SD:/apps_data/duta/` (the app's data dir, `/data` at runtime).
- Each file is a skrit-mc program: `[0x01 ver, …ops…, 0x00 end]` — the same
  bytecode the core VM runs (`SETOUT`/`SETPWM`/`SETRGB`/`EMIT`/`DELAY`…). It
  drives the Flipper's own outputs (GPIO pins + RGB LED) and the DATA UART.
- Three examples ship in [`macros/`](macros/) — copy them to the SD:
  | File | What it does |
  |------|--------------|
  | `blink_led.skm` | blinks the RGB LED green 3× |
  | `pulse_pa7.skm` | drives header pin PA7 high 500 ms, then low |
  | `hello_uart.skm` | emits `hello\r\n` out the DATA UART (pin 13) |

  ```sh
  # with the Flipper mounted via qFlipper, or over ufbt:
  ufbt cli storage mkdir /ext/apps_data/duta
  ufbt cli storage send macros/blink_led.skm /ext/apps_data/duta/blink_led.skm
  ```

  (Sutra can export `.skm` files here later; for now drop them yourself.)

## Notes

- `skrit_device.h` + `protocol.h` are **vendored** here (copied from
  `platforms/common` / `protocol`, like `duta/protocol` mirrors skrit) so the
  `.fap` builds with no extra include paths. Keep them in sync with skrit.
- While the app runs it owns the USB CDC, so the Flipper's normal CLI/qFlipper
  link is paused until you exit (Back).
- Channels not yet bridged: Sub-GHz / NFC / IR as DATA mediums — future work.
