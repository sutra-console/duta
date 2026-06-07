# zephyr

Duta on **Zephyr** (west + Zephyr SDK) — nRF52840, ESP32, RP2040, STM32, …
Currently a skeleton; `native_sim` is the CI build-check target.

```sh
west build -b native_sim platforms/zephyr           # CI build-check
west build -b nrf52840dk_nrf52840 platforms/zephyr  # real board
west flash
```

(Run inside a Zephyr workspace, or point `ZEPHYR_BASE` at one.)

## Roadmap

- **USB transport** — CDC ACM endpoint carrying multiplexed DATA + CMD.
- **BLE transport** — NUS GATT service (great fit for nRF52840).
- Bridge a hardware UART ↔ the DATA console.

Dispatch logic to port over: [`../host/src/main.c`](../host/src/main.c). Protocol
constants/helpers: [`../../protocol/protocol.h`](../../protocol/protocol.h).
