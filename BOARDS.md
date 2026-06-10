# Boards — `platform → mcu → board`

Duta's board support is organized in three conceptual layers, because each layer
only ever **narrows** what a pin may do — silicon sets the ceiling, the board
carves away from it, and (with runtime provisioning) the user picks from what's
left.

| Layer | Lives in | Owns |
|-------|----------|------|
| **platform** | `platforms/<platform>/` | the toolchain/framework (build reality): `ch55xduino`, `espressif`, `pico`, `zephyr`, `host` |
| **mcu** | `platforms/<platform>/src/mcu/<chip>.h` | **silicon truth**: the full pin inventory, each pin's intrinsic caps, and an immutable hazard status — written **once per chip**, reused by every board on it |
| **board** | `platforms/<platform>/src/boards/<vendor>_<board>.h` | the physical **overlay**: vendor, the role pins, what's broken out / committed, and the compiled-default `duta_outputs[]` table |

`board.h` in each platform is a **thin dispatcher**: it maps the `-DBOARD_*` build
flag (set per env in `platformio.ini`) to the matching leaf header.

Vendor is a **field** (`BOARD_VENDOR`) and a **filename prefix**, not a directory
level (PlatformIO's convention). Promote to `boards/<vendor>/…` only if one vendor
ever accumulates many boards.

## The layers in code

- **`platforms/common/duta_pincap.h`** — the shared vocabulary: capability bits
  (`DUTA_CAP_DIGITAL/ADC/PWM/DAC/I2C/SPI/TOUCH`), the mcu hazard status
  (`FREE`/`CAUTION`/`FORBIDDEN`), and the board commitment (`FIXED`/`DUAL`). This
  is the "menu" runtime provisioning picks from.
- **`mcu/<chip>.h`** — declares `static const duta_pin duta_mcu_pins[]` (+ `DUTA_MCU_NPINS`,
  `DUTA_MCU_NAME`, `BOARD_HAS_NATIVE_USB`). One row per pin: `{pin, caps, status, bus}`.
- **`boards/<vendor>_<board>.h`** — `#include "../mcu/<chip>.h"`, then declares
  `BOARD_NAME`, `BOARD_VENDOR`, the role pins, the board overlay (below), and pulls
  in `duta_board_io.h` to build the default output table.
- **`platforms/common/duta_board_io.h`** — builds the standard `duta_outputs[]`
  (two IO relays + a PWM aux LED + an optional RGB row) from the role macros, so a
  board header declares pins, not table boilerplate. A board with a non-standard IO
  set can declare `duta_outputs[]` itself instead.

### The board overlay (what's physically true on *this* product)

```c
// every usable header pin is exposed (dev kits): the resolver still filters
// FORBIDDEN (flash/USB) and FIXED uses out of the menu.
#define DUTA_BROKEN_OUT_ALL 1
// …or an explicit list for a product board:
static const int16_t duta_board_broken_out[] = { 0, 1, 2, /* … */ };
#define DUTA_BROKEN_OUT_N  (sizeof duta_board_broken_out / sizeof duta_board_broken_out[0])

// pins wired to onboard hardware:
static const duta_pin_use duta_board_uses[] = {
    {25, DUTA_USE_FIXED, "onboard LED"},   // not broken out -> LED is a constant, never offered
    {48, DUTA_USE_DUAL,  "onboard WS2812"},// broken out AND onboard -> offered, but warned
};
#define DUTA_USES_N (sizeof duta_board_uses / sizeof duta_board_uses[0])
```

A pin is provisionable iff **`broken_out && committed != FIXED`**. The device
resolves `mcu ∩ board` and reports it over `PIN_CAPS`, so the app's "Configure
device" picker renders the menu with **zero hardcoded chip knowledge**.

## Adding a board

1. If the chip is new, write `mcu/<chip>.h` (the silicon truth — do it once).
2. Copy a sibling `boards/<vendor>_<board>.h`, set `BOARD_NAME` / `BOARD_VENDOR`,
   the role pins, and the broken-out/committed overlay.
3. Add a `#elif defined(BOARD_<X>)` arm to that platform's `board.h` dispatcher.
4. Add an `[env:<x>]` to `platformio.ini` passing `-DBOARD_<X>`.

That's the whole PR — one leaf header (and a one-time mcu map for a new chip).

## Zephyr is native

Zephyr has its own board concept, so the nRF platform stays on Zephyr's mechanism:
`platforms/zephyr/boards/<target>.conf` + `.overlay`, named in Zephyr's canonical
`<vendor>_<board>` style (e.g. `nrf52840dk_nrf52840`). IO there is driven from the
devicetree, not the `duta_io` table.
