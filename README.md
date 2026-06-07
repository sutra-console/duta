# Duta — dual-CDC USB-to-TTL adapter + control port (WeAct CH552)

Firmware that turns a WeAct **CH552** into a USB composite device exposing
**two virtual COM ports** over one cable:

```
            ┌─ CDC #1  "Duta DATA"  ── transparent USB↔UART0 bridge, auto-baud
  PC ── USB ┤
            └─ CDC #2  "Duta CMD"   ── line-based control: relays, status, OLED
   (+ optional SSD1306 128x64 OLED mirroring the DATA-port RX)
```

The two ports are independent CDC-ACM interfaces (driverless, `usbser.sys`), so
you can stream serial on **DATA** while toggling a relay on **CMD** at the same
time, from any terminal or script.

## Wiring

| CH552 pin | Role | Connect to |
|-----------|------|------------|
| **P3.1**  | UART TXD (DATA) | target **RX** |
| **P3.0**  | UART RXD (DATA) | target **TX** |
| **P3.4**  | Relay 1 (active-LOW) | relay-module IN1 |
| **P3.3**  | Relay 2 (active-LOW) | relay-module IN2 |
| **P1.7 / P1.6** | OLED SCL / SDA (when enabled) | SSD1306 |
| **GND**   | Ground | common (required) |

> CH552 I/O is 3.3V (5V-tolerant inputs). Don't drive a relay coil directly —
> use a relay **module** with an onboard transistor/opto driver, or add a
> transistor + flyback diode. Pins assignable in [`Duta.ino`](Duta.ino)
> and [`src/config.h`](src/config.h).

## The two COM ports

A composite device creates two `COMx`. Windows won't reliably show which is
which, so **probe**: the CMD port answers `PING` with `PONG`; the DATA port is a
pure passthrough and stays silent.

### CMD port — command protocol (one line, `\n`-terminated, case-insensitive)

| Command | Reply | Action |
|---------|-------|--------|
| `R1 ON` / `R1 OFF` / `R1 TOGGLE` | `OK` | relay 1 (P3.4) |
| `R2 ON` / `R2 OFF` / `R2 TOGGLE` | `OK` | relay 2 (P3.3) |
| `LED ON` / `LED OFF` / `LED TOGGLE` | `OK` | on-board LED (P1.4) |
| `STATUS` | `R1=0 R2=1 LED=0` | current output states |
| `PING`   | `PONG` | identify the CMD port |
| `ID`     | `Duta v2 dual-cdc` | firmware id |
| `OLED <text>` | `OK` / `ERR oled-disabled` | show text on the OLED |
| `HELP`   | command list | |
| *(unknown)* | `ERR` | |

```powershell
# example: pulse relay 1 from PowerShell
$p = New-Object System.IO.Ports.SerialPort 'COM31',9600; $p.DtrEnable=$true; $p.Open()
$p.Write("R1 ON`n");  Start-Sleep 1; $p.Write("R1 OFF`n"); $p.Close()
```

### DATA port — transparent UART bridge

Same behavior as a plain USB-TTL adapter: **auto-baud** (UART0 follows whatever
baud the host opens the port with), DTR-gated upstream. `P3.1`=TX→target RX,
`P3.0`=RX→target TX.

---

## Toolchain setup (ch55xduino)

No build tools are installed yet. Pick **one** of the two paths below.

### Option A — Arduino IDE (simplest)

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. **File → Preferences → Additional Boards Manager URLs**, add:
   ```
   https://raw.githubusercontent.com/DeqingSun/ch55xduino/ch55xduino/package_ch55xduino_mcs51_index.json
   ```
3. **Tools → Board → Boards Manager**, search **CH55xDuino**, install it.
4. **Tools → Board** → `CH552 Board`.
   - **USB Settings → "USER CODE w/ 266B USB ram"** — **required** (the dual-CDC
     stack is user-mode USB and needs the larger USB-RAM window).
5. Open `Duta.ino`, click **Upload**.

### Option B — arduino-cli (scriptable, recommended here)

> ⚠️ **Version pin:** ch55xduino does **not** build with arduino-cli **1.2+**
> (the newer builder breaks the platform's merged-`.cpp` step — you'll see
> `open ...sketch_merged.cpp: The system cannot find the file specified`).
> Use **arduino-cli 1.0.x** (1.0.4 is verified here). The core itself is
> installed once and shared across CLI versions via `%LOCALAPPDATA%\Arduino15`.
> Grab a pinned binary if your `winget`/latest is too new:
> ```powershell
> $u="https://downloads.arduino.cc/arduino-cli/arduino-cli_1.0.4_Windows_64bit.zip"
> Invoke-WebRequest $u -OutFile acli.zip; Expand-Archive acli.zip .\.tools\104 -Force
> ```

```powershell
# 1. Install arduino-cli (pin to 1.0.x — see warning above)
winget install ArduinoSA.CLI    # or use the pinned 1.0.4 zip above

# 2. Add the ch55xduino board index
arduino-cli config init
arduino-cli config add board_manager.additional_urls `
  https://raw.githubusercontent.com/DeqingSun/ch55xduino/ch55xduino/package_ch55xduino_mcs51_index.json

# 3. Install the core
arduino-cli core update-index
arduino-cli core install CH55xDuino:mcs51

# 4. Compile (run from this folder) — note the user266 USB option
arduino-cli compile --fqbn "CH55xDuino:mcs51:ch552:usb_settings=user266" .

# 5. Upload (enter bootloader first — see Flashing)
arduino-cli upload  --fqbn "CH55xDuino:mcs51:ch552:usb_settings=user266" .
```

The `usb_settings=user266` option is **mandatory** — the dual-CDC firmware is a
user-mode USB stack and won't fit/work under the default CDC option.

---

## Flashing / entering the bootloader

### Easiest: 1200-baud touch (no button)

This firmware (and the stock ch55xduino CDC) jumps to the ROM bootloader when
the **DATA** port is opened at **1200 baud with DTR deasserted**. Because the
bootloader self-exits after a moment, start the flasher *first*, then touch:

```powershell
# start upload polling, then drop into bootloader while it retries (one session)
$acli = ".\.tools\104\arduino-cli.exe"
$job = Start-Job { param($a) & $a upload --fqbn "CH55xDuino:mcs51:ch552:usb_settings=user266" "$using:PWD" } -Arg $acli
Start-Sleep -Milliseconds 1200
try { $p=New-Object System.IO.Ports.SerialPort 'COM23',1200; $p.DtrEnable=$false; $p.Open(); $p.Close() } catch {}
Receive-Job -Wait $job   # expect: Verify complete!!! / Reset OK
```
(`COM23` = the DATA port; find it as the lower-numbered of the two Duta COMs.)

### Manual: BOOT button

1. **Unplug** the board.
2. **Hold `BOOT`/`DOWNLOAD`**, plug in USB, release after ~1 s.

Either way the board enumerates as the WCH bootloader (VID `4348` / PID `55E0`)
and ch55xduino's bundled flasher writes it and resets into the firmware.

> After this firmware is running, the board re-appears as a serial COM port
> (it was previously showing as `VID_1209&PID_C550 "CH55X"` — the ch55xduino
> CDC default). Re-do the button dance any time you want to reflash.

### Windows driver gotcha (do this once)

On a fresh Windows machine the bootloader device (`VID_4348&PID_55E0`) loads
**no driver** and shows up in Device Manager with an `Error` status, so the
flasher just spins `No CH55x USB Found` and the board "kicks itself back out"
of the bootloader. Fix it **once** with [Zadig](https://zadig.akeo.ie/):

1. Enter the bootloader (button dance) and **leave it there** (don't press reset).
2. Zadig → **Options → List All Devices** → pick USB ID **`4348 55E0`**.
3. Set target driver to **WinUSB**, click **Install Driver**, wait for SUCCESS.

WinUSB then stays bound to that hardware ID, so every later flash auto-binds
with no `Error` state and uploads immediately.

---

## Customizing

- Relay pins / polarity, `DEFAULT_BAUD`: top of [`Duta.ino`](Duta.ino).
- OLED enable + I²C pins: [`src/config.h`](src/config.h) (`ENABLE_OLED`).
- Add a command: extend `handleCommand()` in the sketch.
- USB names / VID-PID: [`src/usb/USBconstant.c`](src/usb/USBconstant.c).

## Firmware layout

```
Duta.ino            app: DATA bridge + CMD parser + relays
src/config.h             board selection + feature flags (ENABLE_OLED)
src/board.h              dispatches to the selected board's pin map
src/boards/*.h           per-board pin maps (weact_ch552, generic_ch552, custom)
src/usb/USBconstant.*    dual-CDC descriptors (2× IAD, 4 interfaces)
src/usb/USBhandler.*     EP0 control + EP1..EP4 dispatch + device cfg
src/usb/USBCDC.*         per-port ring/endpoint data path (A=DATA, B=CMD)
src/oled/ssd1306.*       SSD1306 bit-bang I2C text terminal (gated by ENABLE_OLED)
```

## Boards / porting

The pin map is the only board-specific part; everything else (USB, protocol,
UART0 bridge) is board-agnostic. Pick a board near the top of
[`src/config.h`](src/config.h) — default is **WeAct CH552**:

```c
//#define BOARD_GENERIC_CH552
//#define BOARD_CUSTOM   // your own, from src/boards/custom.h
```

To add a board, copy `src/boards/custom.h`, fill in the pins (ch55xduino style:
`Px.y → x*10+y`, e.g. `P3.4 → 34`), and select `BOARD_CUSTOM`. Each board header
defines `RELAY1_PIN RELAY2_PIN RELAY_ACTIVE_LOW LED_PIN LED_ACTIVE_LOW
OLED_SCL_PIN OLED_SDA_PIN OLED_I2C_ADDR`. Avoid **P3.6/P3.7** (USB) and
**P3.0/P3.1** (UART0 bridge). `board.h` `#error`s if a required pin is missing.

## Notes / limitations

- **USB-RAM is tight.** Two CDC-ACM data endpoints + two notification endpoints
  just fit in the CH552's 266-byte user USB window by using **32-byte** bulk
  packets and aliasing the (unused) DATA notify endpoint onto EP0. The CH552
  transmits IN packets from a hardware-fixed **+64** buffer offset, so each data
  endpoint uses a 96-byte buffer (64 OUT slot + 32 IN slot).
- UART FIFOs are small (16 B). Fine for console/log use; not a line-rate
  1 Mb/s adapter. No hardware RTS/CTS flow control.
- The OLED is ACK-probed at boot — if no panel is on the I²C pins, all OLED
  calls become no-ops so the bridge runs at full speed. The bit-bang mirror
  does add per-byte overhead on the DATA RX path; disable `ENABLE_OLED` if you
  need maximum sustained throughput.
