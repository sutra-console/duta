# host / native reference

A native build of the [Duta protocol](../../protocol/README.md) that carries the
skrit-mux stream over a **WebSocket**. Built on the shared core
([`../common/skrit_device.h`](../common/skrit_device.h)), so it's a faithful
muxed + auth-gated device. Two jobs:

1. **Hardware-free CI**: compiles everywhere; unit-tests the WebSocket codec
   ([`ws.h`](ws.h)) and exercises the core without a board.
2. **Network-bridge seed**: the reference an ESP32 WiFi "bridge mode" mirrors,
   and the proving ground for the app's WebSocket transport.

It exposes 3 controls and answers the full CMD set. Because it's a **network**
transport it is **auth-gated**: connect, then `AUTH "duta"` (the factory default)
before other commands work; see [Network auth](../../protocol/PROTOCOL.md).

## Build & run

```sh
cmake -B build && cmake --build build
ctest --test-dir build               # WebSocket codec test
./build/duta-host 9555               # ws://127.0.0.1:9555/
```

Connect with any WebSocket client; frames are the skrit-mux stream as binary
messages. Plain C11 + POSIX sockets (Winsock shim for Windows). No dependencies:
the WebSocket handshake (SHA-1 + base64) and framing are in [`ws.h`](ws.h).

> `wss://` (TLS) is out of scope for this minimal reference; terminate TLS at a
> reverse proxy, or use the ESP32/Zephyr bridge's platform TLS.
