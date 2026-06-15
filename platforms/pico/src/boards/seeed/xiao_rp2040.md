# Seeed Studio XIAO RP2040

A thumb-sized (21 x 17.8 mm) RP2040 board in the XIAO footprint, with native
USB-C and 2 MB flash. Vendored as `xiao_rp2040.h`; the Duta build on it is
`targets/duta_xiao_rp2040.h`.

## Specs

| | |
|---|---|
| Chip | RP2040 (dual-core Cortex-M0+, up to 133 MHz) |
| Flash | 2 MB |
| SRAM | 264 KB |
| USB | USB-C, native USB-CDC |
| Size | 21 x 17.8 mm |

## Pinout (XIAO label to GP)

| Pin | GP | Function |
|-----|----|----------|
| D0 / A0 | GP26 | ADC0 |
| D1 / A1 | GP27 | ADC1 |
| D2 / A2 | GP28 | ADC2 |
| D3 / A3 | GP29 ⚠️[^r-gp29] | ADC3 |
| D4 / SDA | GP6 | I2C0 SDA |
| D5 / SCL | GP7 | I2C0 SCL |
| D6 / TX | GP0 ⚠️[^r-data] | UART0 TX |
| D7 / RX | GP1 ⚠️[^r-data] | UART0 RX |
| D8 / SCK | GP2 | SPI0 SCK |
| D9 / MISO | GP4 | SPI0 RX |
| D10 / MOSI | GP3 | SPI0 TX |

The RP2040 boots from QSPI flash, so none of GP0-29 are strapping pins.

[^r-gp29]: Exposed on this module but modeled as board-internal in the shared Pico-centric `mcu/rp2_pins.h` (on the Pico, GP29 reads VSYS). It works if you drive it directly, but Configure Device will not offer it until it is added to that table.
[^r-data]: Committed to the Duta DATA console bridge (UART0). Reassigning it drops the target console.

## Onboard hardware

- **RGB user LED** (3 discrete channels, **active low**): R = GP17, G = GP16,
  B = GP25.
- **WS2812 NeoPixel** on **GP12**, gated by a **power-enable on GP11** (drive
  GP11 high to light it). In arduino-pico these are `PIN_NEOPIXEL` / `NEOPIXEL_POWER`.
- Charge LED + power LED.
- **BOOT** button and **RESET** ("R") button.

None of these are on the header pads, so all are fixed (never provisionable).

## Flashing

Native USB-C, UF2 drag-and-drop. Hold **BOOT** while plugging in to mount the
`RPI-RP2` drive (`pio run -t upload` does this over the SWD/UF2 path).

## Duta on this board

- **Transport:** `usb-mux` (DATA + CMD over the native USB-CDC via skrit-mux).
- **DATA console:** UART0 on the silkscreened TX/RX, GP0 / GP1 (D6/D7).
- **Default outputs:** the onboard red user LED (GP17, active low) as the aux
  LED. The WS2812 is left unwired by default because it needs GP11 driven high,
  which the generic driver does not do (set `RGB_PIN` to 12 and drive GP11 to
  enable it). Relays go on free pads via Configure Device.
- **Build:** `pio run -d platforms/pico -e xiaorp2040`

## Links

- Wiki: <https://wiki.seeedstudio.com/XIAO-RP2040/>
- RP2040 datasheet: <https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf>
