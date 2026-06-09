# host / native reference

A native build of the [Duta protocol](../../protocol/README.md) that serves
it over a **TCP socket**. Two jobs:

1. **Hardware-free CI** — compiles everywhere; a place to unit-test the framing
   and command handlers without a board.
2. **TCP-bridge seed** — the reference an ESP32 "bridge mode" (or a Linux serial
   server) mirrors, and the proving ground for the app's upcoming TCP transport.

It exposes 3 virtual controls (Relay 1/2, Aux LED) and answers `PING`, `INFO`,
`DEVICE_NAME`, and `OUTPUT_*`.

## Build & run

```sh
cmake -B build && cmake --build build
./build/duta-host 9555        # listens on tcp/9555
```

Plain C11 + POSIX sockets (Winsock shim for Windows). No dependencies.
