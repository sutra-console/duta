# Seeed Studio XIAO ESP32-C3

A thumb-sized (21 x 17.8 mm) RISC-V board in the XIAO footprint, with native
USB-C and an external u.FL antenna. Vendored as `xiao_esp32c3.h`; the Duta build
on it is `targets/duta_xiao_esp32c3.h`.

## Specs

| | |
|---|---|
| SoC | ESP32-C3 (RISC-V single-core, up to 160 MHz) |
| Flash | 4 MB |
| SRAM | 400 KB |
| USB | USB-C, native USB-CDC (no UART bridge) |
| Antenna | external u.FL |
| Size | 21 x 17.8 mm |

## Pinout (XIAO label to GPIO)

| Pin | GPIO | Function |
|-----|------|----------|
| D0 / A0 | GPIO2 ⚠️[^c-strap] | ADC1_CH2 |
| D1 / A1 | GPIO3 | ADC1_CH3 |
| D2 / A2 | GPIO4 | ADC, JTAG MTMS |
| D3 / A3 | GPIO5 | ADC, JTAG MTDI |
| D4 / SDA | GPIO6 | I2C SDA, JTAG MTCK |
| D5 / SCL | GPIO7 | I2C SCL, JTAG MTDO |
| D6 / TX | GPIO21 ⚠️[^c-data] | UART0 TX |
| D7 / RX | GPIO20 ⚠️[^c-data] | UART0 RX |
| D8 / SCK | GPIO8 ⚠️[^c-strap] | SPI SCK |
| D9 / MISO | GPIO9 ⚠️[^c-boot] | SPI MISO, BOOT button |
| D10 / MOSI | GPIO10 | SPI MOSI |

USB D-/D+ are GPIO18/19 (internal). GPIO11-17 are the SPI flash bus.

[^c-strap]: Boot strapping pin (GPIO2, GPIO8). The ROM samples it at reset, so external wiring that holds it at the wrong level can stop the board from booting. Fine as ordinary IO once running; just avoid hard pull-ups/downs that fight the boot state.
[^c-boot]: GPIO9 is the boot-mode strapping pin and the onboard BOOT button. Held low at reset, the chip enters serial-download mode instead of running your firmware.
[^c-data]: Committed to the Duta DATA console bridge (UART0). Reassigning it drops the target console.

## Onboard hardware

- **No user LED** (only a charge indicator on VCC_3V3, not GPIO-controllable).
- **BOOT button** on GPIO9 (also broken out as D9, so it is a dual-use pad).
- **RESET button** on CHIP_EN.
- No NeoPixel.

Strapping pins to treat with care: GPIO2, GPIO8, GPIO9.

## Flashing

Native USB-C. Hold **BOOT** while plugging in to enter the ROM bootloader
(esptool / `pio run -t upload`).

## Duta on this board

- **Transport:** `usb-mux` (DATA + CMD share the native USB-CDC via skrit-mux).
- **DATA console:** UART0 on the silkscreened TX/RX, GPIO21 / GPIO20 (D6/D7).
- **Default outputs:** none. The C3 has no onboard user-controllable output, so
  the compiled-default output table is empty. Wire relays/LEDs to free pads and
  add them at runtime via the app's Configure Device screen.
- **Build:** `pio run -d platforms/espressif -e xiaoc3`

## Links

- Wiki: <https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/>
- ESP32-C3 datasheet: <https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf>
