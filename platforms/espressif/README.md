# espressif (ESP32)

Duta on ESP32 / S3 / C3 via **Arduino + PlatformIO**. Currently a skeleton:
the CMD protocol over `Serial` (USB-CDC on S3/C3, UART0 otherwise) with 3 demo
controls.

```sh
pio run                 # default env (esp32c3)
pio run -e esp32s3
pio run -t upload -e esp32c3
```

## Roadmap

- DATA-console passthrough (bridge a hardware UART ↔ the host console).
- **TCP transport** — WiFi "bridge mode": listen on a socket, multiplex DATA +
  CMD over it (mirrors [`../host`](../host)).
- **BLE transport** — NUS-style GATT service carrying the same multiplexed stream.

The protocol bits (IDs, CRC-8, COBS) come from
[`../../protocol/protocol.h`](../../protocol/protocol.h) — wired via `build_flags`.
