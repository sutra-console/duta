# Boards: `platform → mcu → board → target`

Duta's board support is organized in four conceptual layers. The first three only
ever **narrow** what a pin may do (silicon sets the ceiling, the board carves away
from it) and the fourth *chooses* from what's left:

| Layer | Lives in | Owns |
|-------|----------|------|
| **platform** | `platforms/<platform>/` | the toolchain/framework (build reality): `ch55xduino`, `espressif`, `pico`, `zephyr`, `host` |
| **mcu** | `src/mcu/<chip>.h` | **silicon truth**: the full pin inventory, each pin's intrinsic caps, and an immutable hazard status (`FREE`/`CAUTION`/`FORBIDDEN`), written **once per chip** |
| **board** *(vendored)* | `src/boards/<vendor>/<board>.h` | **facts about the physical board, nothing else**: which mcu it carries, which pins are broken out, what's wired onboard (`FIXED`/`DUAL` uses), `BOARD_VENDOR` + `BOARD_MODEL` |
| **target** *(ours)* | `src/targets/duta_<board>.h` | **our choices**: which vendored board we build on, the Duta role pins (DATA UART, relays, LED, RGB, DTR/RTS), `BOARD_NAME`, and the compiled-default `duta_outputs[]` |

The split matters: *"relay on GPIO4" is not a fact about a DevKitC, it's a fact
about our wiring.* Vendored board headers must stay accurate to the actual product
(breakout, onboard LED/WS2812, nothing more), so they're reusable by anyone; the
Duta-specific role map lives in the target that `#include`s one of them. To bring up
your own build, **base a new target off a vendored board** (or vendor a new board
first if yours isn't here).

`board.h` in each platform is a **thin dispatcher**: it maps the `-DBOARD_*` build
flag (set per env in `platformio.ini`) to a target.

## The layers in code

- **`platforms/common/duta_pincap.h`**: the shared vocabulary, capability bits
  (`DUTA_CAP_DIGITAL/ADC/PWM/DAC/I2C/SPI/TOUCH`), the mcu hazard status, and the
  board commitment (`FIXED`/`DUAL`). This is the "menu" runtime provisioning picks from.
- **`mcu/<chip>.h`**: declares `static const duta_pin duta_mcu_pins[]` (+ `DUTA_MCU_NPINS`,
  `DUTA_MCU_NAME`, `BOARD_HAS_NATIVE_USB`). One row per pin: `{pin, caps, status, bus}`.
- **`boards/<vendor>/<board>.h`**: `#include "../../mcu/<chip>.h"`, then the facts:

  ```c
  #define BOARD_VENDOR "Raspberry Pi"
  #define BOARD_MODEL "Pico"
  // breakout: DUTA_BROKEN_OUT_ALL 1 (dev kits) or an explicit pin list
  static const int16_t duta_board_broken_out[] = { 0, 1, /* … */ };
  #define DUTA_BROKEN_OUT_N (sizeof … )
  // onboard hardware:
  #define ONBOARD_LED_PIN 25
  static const duta_pin_use duta_board_uses[] = {
      {25, DUTA_USE_FIXED, "onboard LED"},   // not on a header -> never provisionable
      {48, DUTA_USE_DUAL,  "onboard WS2812"},// on a header too -> offered with a warning
  };
  #define DUTA_USES_N (sizeof … )
  ```

- **`targets/duta_<board>.h`**: `#include "../boards/<vendor>/<board>.h"`, then our
  role pins (`DATA_TX/RX`, `RELAY1/2`, `LED`, `RGB`, `DTR/RTS`) + `BOARD_NAME`, ending
  with `#include "duta_board_io.h"` to build the default `duta_outputs[]` table. A
  target with a non-standard IO set declares `duta_outputs[]` itself instead.

## Provisioning semantics (mcu ∩ board)

A pin is **offerable** iff it's broken out, not `FORBIDDEN`, and not `FIXED`. The
device resolves this and reports it over `PIN_CAPS`, so the app's "Configure device"
picker renders the menu with **zero hardcoded chip knowledge**. Two special rules:

- **dual-use** pins (Arduino-D13-style: on a header *and* driving onboard hardware)
  are offered with a warning naming what they'd take over.
- **fixed** pins may be *kept* in their compiled-default role by a provisioned table
  (the Pico's GP25 stays the LED: "if the LED can't move, it's always the LED"),
  but never repurposed or moved.

## Adding a board / target

1. If the chip is new, write `mcu/<chip>.h` (the silicon truth, once per chip).
2. If the physical board is new, vendor it: `boards/<vendor>/<board>.h` (facts only).
3. Create `targets/duta_<board>.h` basing off the vendored board with your role pins.
4. Add a `#elif defined(BOARD_<X>)` arm to that platform's `board.h` dispatcher and an
   `[env:<x>]` to `platformio.ini` passing `-DBOARD_<X>`.

Steps 1-2 are upstreamable facts anyone can reuse; step 3 is the only Duta opinion.

## Why ch55xduino is its own platform (not PlatformIO)

The platform axis is **build reality**, and the CH55x's build reality is genuinely its
own: an 8051 (MCS-51) part compiled with **SDCC** through the **ch55xduino** Arduino
core, driven by `arduino-cli`, not PlatformIO. PlatformIO's `intel_mcs51` platform has
no maintained ch55xduino framework integration (community recipes hand-roll board JSONs
against it); adopting that would trade a supported upstream toolchain for a fragile
fork we'd maintain ourselves. So it stays a sibling of `espressif`/`pico`/`zephyr`,
equal in the layout, different in toolchain, with the **core version pinned** in CI
(`CH55xDuino:mcs51@<ver>`) so builds are reproducible and upgrades are deliberate.
The boards/targets layering applies inside it the same as everywhere else; it's just
compile-time only (no runtime provisioning, no room).

## Zephyr is native

Zephyr has its own board concept, so the nRF platform stays on Zephyr's mechanism:
`platforms/zephyr/boards/<target>.conf` + `.overlay`, named in Zephyr's canonical
style (e.g. `nrf52840dk_nrf52840`). IO there is driven from the devicetree, not the
`duta_io` table.
