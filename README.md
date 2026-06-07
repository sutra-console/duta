# Duta

A serial-console buddy: **firmware** that turns a cheap MCU into a smart USB-to-
TTL adapter (transparent UART bridge + a control channel for relays/LED/GPIO and
self-describe), and a **desktop app** that drives it — console, snippet/macro
automation, and an embedded **MCP server** so an LLM can read the console and run
commands.

The **[protocol](protocol/)** is the contract: any device that speaks it — over
USB, TCP, or BLE — works with the app, regardless of MCU. So the same app drives
a CH552, an ESP32, or an nRF52840.

## Repo map

```
protocol/        the wire contract — PROTOCOL.md + portable protocol.h
platforms/       firmware, one impl per MCU/framework (all speak the protocol)
  ch55xduino/      CH55x dual-CDC adapter  ✅ working
  espressif/       ESP32 (Arduino/PlatformIO)  🚧 scaffold
  zephyr/          nRF52840/ESP32/RP2040 (west)  🚧 scaffold
  host/            native reference over TCP  🚧 (hardware-free CI + TCP bridge)
Duta/        desktop app (Tauri + React) — console, macros, MCP server
targets.yml      QMK-style list of CI build targets
.github/         build-check CI matrix
```

## Quick start

- **Flash a CH552:** see [platforms/ch55xduino](platforms/ch55xduino/README.md).
- **Run the app:** `cd Duta && bun install && bun run tauri dev`.
- **Let an LLM drive it:** start the MCP server from the app's Settings; point an
  MCP client at `http://127.0.0.1:<port>/mcp`. See [Duta/MCP.md](Duta/MCP.md).

## The app

- **Universal serial console** (ghostty-web) — connect a Duta *or* any COM
  port; baud/parity/stop, named connection profiles.
- **Snippets → macros** — line-based Bash Bunny/DuckyScript plus an expect engine
  (`WAITFOR`, `RUN` with exit-code capture, `IF OK…ELSE…END`). Secret snippets are
  never readable by the LLM, and their echoes are redacted from console reads.
- **Device-driven controls** — the UI renders the relays/LED the device
  *self-describes*, by name.
- **MCP server** — per-tool toggles in Settings; console R/W, outputs, snippets
  (run-by-name only), and connection control.

## Building firmware

[`targets.yml`](targets.yml) lists the official build targets `(platform, board,
transport)`; [CI](.github/workflows/firmware.yml) builds each on push/PR. Fork,
add your board, and you inherit the whole build system — QMK-style. Per-platform
build instructions live in each `platforms/<name>/README.md`.

## Adding a platform

Implement the [protocol](protocol/README.md) over your transport, answer
`PING`/`INFO`/`DEVICE_NAME`/`OUTPUT_DESC`, add a `targets.yml` entry — and the
desktop app + MCP server come for free. See [platforms/README.md](platforms/README.md).
