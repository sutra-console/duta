# ch55xduino (CH55x)

The original Duta firmware: a WeAct **CH552** USB composite device exposing
**two virtual COM ports** over one cable, built with
[ch55xduino](https://github.com/DeqingSun/ch55xduino) (SDCC).

```
            ┌─ CDC #1  "DATA"  ── transparent USB↔UART0 bridge, auto-baud
  PC ── USB ┤
            └─ CDC #2  "CMD"   ── control: relays, LED, self-describe (+ ASCII)
   (+ optional SSD1306 128x64 OLED mirroring the DATA-port RX)
```

Two independent CDC-ACM interfaces (driverless `usbser.sys`), so you can stream
serial on **DATA** while toggling a relay on **CMD** at the same time. The binary
CMD wire protocol is the shared [skrit protocol](../../protocol/PROTOCOL.md);
a line-based ASCII command set (below) also works for quick manual use.

## Wiring

| CH552 pin | Role | Connect to |
|-----------|------|------------|
| **P3.1**  | UART TXD (DATA) | target **RX** |
| **P3.0**  | UART RXD (DATA) | target **TX** |
| **P3.4**  | Relay 1 (active-LOW) | relay-module IN1 |
| **P3.3**  | Relay 2 (active-LOW) | relay-module IN2 |
| **P1.7 / P1.6** | OLED SCL / SDA (when enabled) | SSD1306 |
| **GND**   | Ground | common (required) |

> CH552 I/O is 3.3V (5V-tolerant inputs). Don't drive a relay coil directly; use
> a relay **module** with an onboard driver. Pins are set by the selected board
> (see **Boards** below).

## CMD port: ASCII commands (one line, `\n`, case-insensitive)

| Command | Reply | Action |
|---------|-------|--------|
| `R1 ON` / `R1 OFF` / `R1 TOGGLE` | `OK` | relay 1 |
| `R2 ON` / `R2 OFF` / `R2 TOGGLE` | `OK` | relay 2 |
| `LED ON` / `LED OFF` / `LED TOGGLE` | `OK` | aux LED |
| `STATUS` | `R1=0 R2=1 LED=0` | output states |
| `PING`   | `PONG` | identify the CMD port |
| `ID`     | `Duta v3 dual-cdc` | firmware id |
| `OLED <text>` | `OK` / `ERR oled-disabled` | show text on the OLED |
| `HELP`   | command list | |

The **DATA** port is a transparent **auto-baud** UART bridge (UART0 follows the
host baud, DTR-gated upstream).

## Build (arduino-cli)

`arduino-cli` is **pinned to 1.0.4**: ≥1.2 breaks the ch55xduino merged-`.cpp`
step (`sketch_merged.cpp not found`). The `usb_settings=user266` option is
**mandatory** (the dual-CDC stack is a user-mode USB stack needing the 266 B
USB-RAM window). Run from the repo root:

```sh
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/DeqingSun/ch55xduino/ch55xduino/package_ch55xduino_mcs51_index.json
arduino-cli core update-index
arduino-cli core install CH55xDuino:mcs51@0.0.25   # pinned, matches CI

arduino-cli compile --fqbn "CH55xDuino:mcs51:ch552:usb_settings=user266" platforms/ch55xduino
```

The sketch entry is `ch55xduino.ino` (Arduino requires the main file name to
match the folder).

## Flash / bootloader

**1200-baud touch (no button):** opening the DATA port at 1200 baud with DTR
deasserted jumps to the ROM bootloader. It self-exits, so start the flasher
*first*, then touch:

```powershell
$acli = ".\.tools\104\arduino-cli.exe"
$job = Start-Job { param($a) & $a upload --fqbn "CH55xDuino:mcs51:ch552:usb_settings=user266" "$using:PWD\platforms\ch55xduino" } -Arg $acli
Start-Sleep -Milliseconds 1200
try { $p=New-Object System.IO.Ports.SerialPort 'COM23',1200; $p.DtrEnable=$false; $p.Open(); $p.Close() } catch {}
Receive-Job -Wait $job   # expect: Verify complete!!! / Reset OK
```

**Manual:** unplug, hold `BOOT`/`DOWNLOAD`, plug in, release after ~1 s. The board
enumerates as the WCH bootloader (VID `4348` / PID `55E0`).

**Windows driver gotcha (once):** the bootloader device loads no driver and the
flasher spins `No CH55x USB Found`. Fix with [Zadig](https://zadig.akeo.ie/):
enter the bootloader, **Options → List All Devices**, pick `4348 55E0`, set
**WinUSB**, **Install Driver**. It then auto-binds on every later flash.

## Boards / porting

The pin map is the only board-specific part; everything else (USB, protocol,
UART0 bridge) is board-agnostic. Pick a board at the top of
[`src/config.h`](src/config.h), default **WeAct CH552**:

```c
//#define BOARD_GENERIC_CH552
//#define BOARD_CUSTOM   // your own, from src/boards/custom.h
```

To add a board, copy [`src/boards/custom.h`](src/boards/custom.h), fill in the
pins (ch55xduino style: `Px.y → x*10+y`, e.g. `P3.4 → 34`), and select
`BOARD_CUSTOM`. Each header defines `BOARD_NAME BOARD_VENDOR RELAY1_PIN
RELAY2_PIN RELAY_ACTIVE_LOW LED_PIN LED_ACTIVE_LOW OLED_SCL_PIN OLED_SDA_PIN
OLED_I2C_ADDR` (+ optional `PARITY_SUPPORT`). Avoid **P3.6/P3.7** (USB) and
**P3.0/P3.1** (UART0). `board.h` `#error`s on a missing pin. The CH55x is
**compile-time only**: no runtime provisioning (no room); see
[BOARDS.md](../../BOARDS.md) for why this platform stays on arduino-cli.

## Layout

```
ch55xduino.ino           DATA bridge + CMD parser + relays + self-describe
src/config.h             board selection + feature flags (ENABLE_OLED, PARITY_SUPPORT)
src/board.h, src/boards/ per-board pin maps
src/usb/                 dual-CDC stack (descriptors, endpoints, COBS/CRC frames)
src/oled/                SSD1306 bit-bang mirror (gated by ENABLE_OLED)
```

## Notes / limitations

- **USB-RAM is tight.** Two CDC data + two notify endpoints just fit the 266 B
  window using **32-byte** packets and aliasing the DATA notify EP onto EP0. The
  CH552 transmits IN packets from a hardware-fixed **+64** offset, so each data
  endpoint uses a 96-byte buffer.
- UART FIFOs are small (16 B); great for console/log use, no HW RTS/CTS.
- `PARITY_SUPPORT` is scaffolded (advertised via INFO) but the UART is still 8N1
  until the 9-bit-mode driver is written.
