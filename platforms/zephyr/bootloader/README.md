# Duta UF2 bootloader (nRF52840)

A thin customization of the **Adafruit nRF52 UF2 bootloader** for the Pro Micro /
nice!nano-class nRF52840 boards Duta runs on. We keep everything that makes UF2
nice (drag-drop flashing, BLE/serial DFU, the `GPREGRET 0x57` app→DFU trigger the
[Zephyr firmware](../README.md#reboot-to-bootloader-flash-with-no-button) already
uses) and add the two things the stock bootloader lacks:

1. **Board identity.** It IDs as a **Duta** — USB VID `0x1209` (the Duta family,
   so Sutra recognizes it) with a distinct bootloader PID, product string
   `Duta nRF52840`, volume label **`DUTA`**, and Board-ID `nRF52840-duta-v1` in
   `INFO_UF2.TXT`. So the board is self-identifying *even sitting in DFU*.
2. **Exit DFU by command.** Open its CDC port at **1200 baud** and it resets back
   into the application — the mirror of the app's 1200-baud / `GPREGRET=0x57`
   enter-DFU touch. No button, no flash required. (Round trip: app→DFU via skrit
   `REBOOT mode=1`; DFU→app via the 1200-baud touch here.)

It is otherwise **binary-layout-identical** to Adafruit's `nice_nano` (same
SoftDevice, MBR, bootloader region, 3V3 REGOUT0, blue LED on P0.15), which is
what makes the UF2 self-update path safe.

## What's in tree (and what isn't)

We vendor only the board variant — everything else comes from a **pinned upstream
checkout** (`build.sh` fetches it; the SHA is pinned there):

```
boards/duta_nrf52840/
  board.h        Duta identity (VID/PID, product/volume strings, Board-ID)
  board.mk       MCU + folds duta_exit.c into the build
  board.cmake    MCU variant (make is the supported build path)
  pinconfig.c    CF2 flash/RAM map (verbatim nice_nano)
  duta_exit.c    the 1200-baud → reset-to-app hook (strong override of a weak
                 TinyUSB callback — no upstream patch needed)
build.sh         clone pinned upstream + overlay this board + make
```

## Build

Needs `arm-none-eabi-gcc` + `make` on PATH and Nordic **`nrfutil`** (for the
self-update package). Then:

```sh
./build.sh         # -> dist/update-duta_nrf52840_bootloader-<ver>_nosd.uf2
```

(`build.sh` clones the pinned Adafruit bootloader into `.upstream/`, overlays
`boards/duta_nrf52840`, and runs `make BOARD=duta_nrf52840 all`.)

## Flash (UF2 self-update — no probe)

1. Drop the board into its **current** bootloader (skrit `REBOOT mode=1`, or
   double-tap) — the `NICENANO` drive mounts.
2. Drag `dist/update-duta_nrf52840_bootloader-*.uf2` onto it. It reflashes the
   bootloader and resets; next time you enter DFU the drive is **`DUTA`**.

**Safety net:** if the self-update is ever refused (board-ID mismatch) or a build
goes wrong, the board's **SWD pads (DIO/CLK)** recover it — flash
`_build/build-duta_nrf52840/duta_nrf52840_bootloader-*_s140_*.hex` with a probe
(`pyocd`/`nrfjprog`/a Pico running `debugprobe`). Layout-identical means the app
in flash is untouched by a bootloader swap.

## Using the exit command

- **Raw**: open the DFU CDC port at 1200 baud and close it (e.g. PySerial
  `Serial(port, 1200)` then `.close()`), or `mode COMx baud=1200` then DTR toggle.
- **Sutra/MCP**: the planned *Exit bootloader* action does the 1200-baud touch.

The whole flash loop is then host-driven, no button: `REBOOT→bootloader` →
(optionally drag a UF2) → `1200-baud touch` → back in the app.

## Notes / gotchas

- Keep it layout-identical to `nice_nano`. If you change the SoftDevice version,
  flash region, or `UICR_REGOUT0`, the UF2 self-update can brick the board — use
  SWD for those.
- `make` is the supported build path (the exit file is wired via `board.mk`).
- `USB_DESC_*` PIDs (`0x5DF0`) are placeholders in the `0x1209` space — register
  proper pid.codes PIDs before shipping.
- Bump `UPSTREAM_SHA` in `build.sh` deliberately to pick up upstream fixes; the
  board variant is decoupled from upstream `main.c`, so rebases are cheap.
