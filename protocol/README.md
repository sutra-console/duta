# Duta protocol

This is the **contract**. A "Duta" is *any* device that speaks this wire
protocol and answers the self-describe commands — regardless of MCU, framework,
or transport. The desktop app ([`../Duta`](../Duta)) and every firmware
in [`../platforms`](../platforms) implement this and nothing app-specific.

- **[PROTOCOL.md](PROTOCOL.md)** — the full spec: COBS/CRC framing, message
  types, self-describe, snippet storage.
- **[protocol.h](protocol.h)** — portable C reference (message IDs, status &
  capability bits, CRC-8/ATM, COBS). Shared by the C/C++ platforms
  (`espressif`, `zephyr`, `host`, and eventually `ch55xduino`).
- `protocol.py` — *(planned)* the same constants for the MicroPython platform.

## Transports

The CMD protocol is transport-independent. USB gives two pipes (DATA console +
CMD); BLE/TCP give one, so those carry **both** console bytes and CMD frames
multiplexed over a single channel. A device declares its transport(s) in its
self-description; the app routes accordingly.

Implementing a new platform = implement this protocol over whatever transport
the hardware has, answer `INFO` / `DEVICE_NAME` / `OUTPUT_DESC`, and you get the
whole desktop app + MCP server for free.
