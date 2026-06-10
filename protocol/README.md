# skrit (vendored)

This is the **contract** — a vendored copy of the
[skrit](https://github.com/sutra-console/skrit) repo; CI checks it stays in sync.
A "skrit" device is *any* device that speaks this wire protocol and answers the
self-describe commands — regardless of MCU, framework, or transport. The
[Sutra](https://github.com/sutra-console/sutra) desktop app and every firmware
in [`../platforms`](../platforms) implement this and nothing app-specific.

- **[PROTOCOL.md](PROTOCOL.md)** — the full spec: COBS/CRC framing, message
  types, self-describe, typed DATA streams, serial control, PWM config, runtime
  IO provisioning (`PIN_CAPS`/`CONFIG_*`), skrit-mux, network auth, async
  events, and the skrit-mc macro bytecode.
- **[protocol.h](protocol.h)** — portable C reference (message IDs, status /
  capability / flag / pin-capability bits, CRC-8/ATM, COBS). Shared by all the
  C/C++ platforms (`ch55xduino`, `espressif`, `pico`, `zephyr`, `host`).
- `protocol.py` — *(planned)* the same constants for the MicroPython platform.

## Transports

The CMD protocol is transport-independent. Dual-CDC USB (CH552) gives two pipes
(raw DATA console + framed CMD); a single channel (one USB-CDC, WebSocket)
carries **both** via skrit-mux; BLE is dual by GATT design (NUS console + a
skrit CMD service). Network transports are auth-gated (`AUTH`). See
[PROTOCOL.md → Transports](PROTOCOL.md).

Implementing a new platform = implement this protocol over whatever transport
the hardware has, answer `INFO` / `DEVICE_NAME` / `OUTPUT_DESC`, and you get the
whole desktop app + MCP server for free.
