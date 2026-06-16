# common: the shared Duta core

[`skrit_device.h`](skrit_device.h) is a **header-only, dependency-free** implementation
of the whole skrit device side: the CMD dispatch, the skrit-mc macro VM (tiers 1-2),
and both transport framings (dual-CDC and [skrit-mux](../../protocol/PROTOCOL.md)).
Every C/C++ platform (`espressif`, `pico`, `zephyr`, `host`) includes it and supplies a
thin [`skrit_hal`](skrit_device.h) vtable: drive a pin, read a UART byte, get a
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
A `NULL` callback degrades to a clean `unsupported`/`bad args` reply, never a crash.
New HAL fields are always **appended** (positional initializers zero-fill trailing
fields), so older platform code keeps compiling.

**Buffer sizing (`SKRIT_SEND_CAP`).** The device→host framing buffers live in the
`skrit_dev` struct (`send_buf`/`send_cobs`), **not** on the caller's stack: `skrit__send`
needs `~2·SKRIT_SEND_CAP` bytes, and on an RTOS those locals overflowed a small send-thread
stack (a sniffer build raised `SKRIT_SEND_CAP` to 200 — to fit a 137 B record in one mux
frame — and faulted a 1 KB thread on its first reply, wedging RX). So raising the cap grows
the **dev struct** (static), not any stack; define it before including `skrit_device.h`.
Contract: **sends to one dev must be serialized** (a platform lock or the single-threaded
super-loop), since that scratch is shared per-dev.

## Table-driven IO (Arduino platforms)

The Arduino ports (espressif, pico) don't hand-write the IO callbacks: a board declares
its outputs/inputs as a table of [`duta_io`](duta_io.h) descriptors,
[`duta_board_io.h`](duta_board_io.h) builds the standard table from a target's role
macros, and [`duta_io_arduino.h`](duta_io_arduino.h) *is* the `skrit_hal` IO callbacks
(`out_*`, `pwm_*` incl. `pwm_config_*`, `rgb_*`, `in_*`) driving that table. Adding a
relay is one row:

```c
static const duta_io duta_outputs[] = {
  { SKRIT_CTRL_IO,  RELAY1_PIN, "Relay 1", DUTA_ACTIVE_LOW },
  { SKRIT_CTRL_PWM,   LED_PIN,    "Aux LED" },
  { SKRIT_CTRL_RGB,   RGB_PIN,    "RGB LED", 0, RGB_COUNT },
};
```

`main.cpp` then only wires transport/serial/reboot and points the HAL's IO fields at
`duta_io_*`. Zephyr drives IO from its devicetree instead (already a declarative table).

## Runtime provisioning (`DUTA_PROVISION`)

The driver operates on an *active table* pointer that defaults to the compiled
`duta_outputs[]`. Defining `DUTA_PROVISION` (before including the driver) adds the
runtime layer on top of the [`duta_pincap.h`](duta_pincap.h) vocabulary and the board's
mcu/overlay tables (see [../../BOARDS.md](../../BOARDS.md)):

- `duta_io_pin_caps` resolves the offerable-pin **menu** (`mcu ∩ board`): hides
  forbidden/fixed pins, warns on strapping/dual-use ones.
- `duta_io_config_get` / `duta_io_config_set` read the current table / validate a new
  one against the menu and persist it. A **fixed** pin may be *kept* in its compiled
  role (the Pico's onboard LED stays the LED) but never repurposed.
- `duta_io_load` at boot swaps in the persisted table; else the compiled default.
- Persistence is three hooks (`duta_io_store_load/save/clear`); a platform defines
  `DUTA_HAVE_STORE` + real storage (ESP32 → NVS via `Preferences`). Without it, a RAM
  buffer makes provisioning session-only.

Point the trailing HAL fields (`pin_caps`, `config_get`, `config_set`) at these and the
core advertises `SKRIT_FLAG_PROVISION` + answers `PIN_CAPS`/`CONFIG_GET`/`CONFIG_SET`.

## What's implemented here vs. per-platform

| In the core (free for every port) | Left to the platform HAL |
|-----------------------------------|--------------------------|
| COBS/CRC framing, mux demux/remux | byte in/out on USB/UART/WS/BLE |
| `PING`/`INFO`/`DEVICE_NAME`/`REBOOT`/`DATA_DESC` | GPIO for outputs/inputs |
| `OUTPUT_*` / `INPUT_*` / `OUTPUT_PULSE` / `PWM_CONFIG` | UART config + DTR/RTS/BREAK lines |
| `SERIAL_GET/SET/SIGNAL` plumbing | `millis()` + a `pump()` for waits |
| `AUTH` session gating (network transports) | password storage |
| `PIN_CAPS` / `CONFIG_GET` / `CONFIG_SET` dispatch | the pin menu + table persistence |
| skrit-mc VM + scratch (`0xFF`) push-and-run | persistent macro storage (optional) |
| ASCII hand-terminal mode (dual links) | board pin map |

Persistent macro slots (ids `0x00..0xFE`) currently reply `STORAGE`; scratch push-and-run
works everywhere. A platform with flash can add a storage hook later (see the roadmap).

## Tests (the hardware-free CI checks)

- [`test_core.c`](test_core.c): the core with a mock HAL: framing (dual + mux),
  INFO/outputs, DATA passthrough, PWM + PWM_CONFIG, RGB, DATA_DESC, AUTH gating,
  provisioning dispatch, scratch macro runs, CRC rejection.
- [`test/test_io.cpp`](test/test_io.cpp): the table-driven Arduino IO driver against a
  mock `Arduino.h`.
- [`test/test_provision.cpp`](test/test_provision.cpp): the provisioning resolver,
  validated writes (incl. the fixed-pin keep/repurpose rule), and the persistence
  round-trip.

```sh
cc  -std=c11   -I . -I ../../protocol test_core.c -o test_core && ./test_core
c++ -std=c++17 -I test -I . -I ../../protocol test/test_io.cpp -o test_io && ./test_io
c++ -std=c++17 -I test -I . -I ../../protocol test/test_provision.cpp -o test_prov && ./test_prov
```
