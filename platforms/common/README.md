# common — the shared Duta core

[`skrit_device.h`](skrit_device.h) is a **header-only, dependency-free** implementation
of the whole skrit device side: the CMD dispatch, the skrit-mc macro VM (tiers 1–2),
and both transport framings (dual-CDC and [skrit-mux](../../protocol/PROTOCOL.md)).
Every C/C++ platform (`espressif`, `pico`, `zephyr`, `host`) includes it and supplies a
thin [`skrit_hal`](skrit_device.h) vtable — drive a pin, read a UART byte, get a
millisecond tick. One protocol implementation, many MCUs.

```c
static skrit_dev dev;
skrit_dev_init(&dev, &my_hal, /*ctx*/ NULL, /*muxed*/ 1);
// main loop:
skrit_dev_poll(&dev);                 // tee target console -> host
while (cmd_has_byte()) skrit_dev_rx(&dev, cmd_next());
```

A platform sets `hal.caps` (incl. `SKRIT_CAP_MUX` iff it constructed the device muxed),
`hal.macro_tier` (0 = no VM, 2 = full interactive), and wires the callbacks it supports
— a `NULL` callback degrades to a clean `unsupported`/`bad args` reply, never a crash.

## Table-driven IO (Arduino platforms)

The Arduino ports (espressif, pico) don't hand-write the IO callbacks: a board declares
its outputs/inputs as a table of [`duta_io`](duta_io.h) descriptors, and
[`duta_io_arduino.h`](duta_io_arduino.h) *is* the `skrit_hal` IO callbacks
(`out_*`, `pwm_*`, `rgb_*`, `in_*`) driving that table. Adding a relay is one row:

```c
static const duta_io duta_outputs[] = {
  { SKRIT_CTRL_IO,  RELAY1_PIN, "Relay 1", DUTA_ACTIVE_LOW },
  { SKRIT_CTRL_PWM,   LED_PIN,    "Aux LED" },
  { SKRIT_CTRL_RGB,   RGB_PIN,    "RGB LED", 0, RGB_COUNT },
};
```

`main.cpp` then only wires transport/serial/reboot and points the HAL's IO fields at
`duta_io_*`. Zephyr drives IO from its devicetree instead (already a declarative table).

## What's implemented here vs. per-platform

| In the core (free for every port) | Left to the platform HAL |
|-----------------------------------|--------------------------|
| COBS/CRC framing, mux demux/remux | byte in/out on USB/UART/TCP/BLE |
| `PING`/`INFO`/`DEVICE_NAME`/`REBOOT` | GPIO for outputs/inputs |
| `OUTPUT_*` / `INPUT_*` / `OUTPUT_PULSE` | UART config + DTR/RTS/BREAK lines |
| `SERIAL_GET/SET/SIGNAL` plumbing | `millis()` + a `pump()` for waits |
| skrit-mc VM + scratch (`0xFF`) push-and-run | persistent macro storage (optional) |
| ASCII hand-terminal mode (dual links) | board pin map |

Persistent macro slots (ids `0x00..0xFE`) currently reply `STORAGE`; scratch push-and-run
works everywhere. A platform with flash can add a storage hook later (see the roadmap).

## Test

[`test_core.c`](test_core.c) drives the core with a mock HAL — framing (dual + mux),
INFO/outputs, DATA passthrough both ways, a scratch macro run, and CRC rejection. It is
the hardware-free CI check:

```sh
cc -std=c11 -I . -I ../../protocol test_core.c -o test_core && ./test_core
```
