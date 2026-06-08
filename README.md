# Duta

**Duta** is the *messenger* — firmware that turns a cheap MCU into a smart serial
adapter: a transparent UART bridge plus a control channel for relays / LED / GPIO
/ inputs and self-describe. It speaks **[skrit](https://github.com/sutra-console/skrit)**
(vendored in [`protocol/`](protocol/)), so it pairs with the
**[Sutra](https://github.com/sutra-console/sutra)** desktop app over USB / TCP / BLE.

> *dūta* (Sanskrit दूत) — *"messenger / envoy"; the one who carries word.*

One firmware family, one protocol, many MCUs — fork it, add a board, inherit the
whole build system (QMK-style).

## Layout

```
platforms/     firmware, one impl per MCU/framework (all speak skrit)
  ch55xduino/    CH55x dual-CDC adapter        ✅ working
  espressif/     ESP32 (Arduino/PlatformIO)    🚧 scaffold
  zephyr/        nRF52840/ESP32/RP2040 (west)  🚧 scaffold
  host/          native reference over TCP     🚧 (hardware-free CI + TCP bridge)
protocol/      vendored skrit contract (PROTOCOL.md + protocol.h)
targets.yml    QMK-style list of build targets
.github/       build-check CI matrix
```

## Build

[`targets.yml`](targets.yml) lists the official targets `(platform, board,
transport)`; [CI](.github/workflows/firmware.yml) builds each on push/PR. Fork,
add your board, inherit the build system. Per-platform instructions live in each
`platforms/<name>/README.md`.

## Adding a platform

Implement [skrit](protocol/PROTOCOL.md) over your transport, answer
`PING` / `INFO` / `DEVICE_NAME` / `OUTPUT_DESC`, add a `targets.yml` entry — and
the Sutra app + MCP server come for free. See [platforms/README.md](platforms/README.md).

> The `protocol/` here is a vendored copy; the canonical home is the
> [skrit](https://github.com/sutra-console/skrit) repo.
