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

## Notes

- `skrit_device.h` + `protocol.h` are **vendored** here (copied from
  `platforms/common` / `protocol`, like `duta/protocol` mirrors skrit) so the
  `.fap` builds with no extra include paths. Keep them in sync with skrit.
- While the app runs it owns the USB CDC, so the Flipper's normal CLI/qFlipper
  link is paused until you exit (Back).
- Channels not yet bridged: Sub-GHz / NFC / IR as DATA mediums — future work.
