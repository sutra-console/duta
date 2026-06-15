# Seeed Studio XIAO ESP32-S3

A thumb-sized (21 x 17.8 mm) ESP32-S3 board in the XIAO footprint, with native
USB-C, 8 MB flash, and 8 MB PSRAM. Vendored as `xiao_esp32s3.h`; the Duta build
on it is `targets/duta_xiao_esp32s3.h`. A Sense variant adds a camera, PDM mic,
and SD slot on the bottom pads (not modeled here).

## Specs

| | |
|---|---|
| SoC | ESP32-S3R8 (Xtensa LX7 dual-core, up to 240 MHz) |
| Flash | 8 MB (16 MB on the Plus) |
| PSRAM | 8 MB |
| USB | USB-C, native USB-CDC (no UART bridge) |
| Antenna | external u.FL |
| Size | 21 x 17.8 mm |

## Pinout (XIAO label to GPIO)

| Pin | GPIO | Function |
|-----|------|----------|
| D0 | GPIO1 | ADC, Touch1 |
| D1 | GPIO2 | ADC, Touch2 |
| D2 | GPIO3 ⚠️[^s-strap] | ADC, Touch3 |
| D3 | GPIO4 | ADC, Touch4 |
| D4 / SDA | GPIO5 | I2C SDA, ADC |
| D5 / SCL | GPIO6 | I2C SCL, ADC |
| D6 / TX | GPIO43 ⚠️[^s-data] | UART0 TX |
| D7 / RX | GPIO44 ⚠️[^s-data] | UART0 RX |
| D8 / SCK | GPIO7 | SPI SCK, ADC |
| D9 / MISO | GPIO8 | SPI MISO, ADC |
| D10 / MOSI | GPIO9 | SPI MOSI, ADC |

USB D-/D+ are GPIO19/20 (internal). The Sense variant uses GPIO10-18, GPIO38-40,
GPIO47 for the camera and GPIO41/42 for the PDM mic.

[^s-strap]: GPIO3 is a strapping pin (it selects the JTAG signal source) and is sampled at reset. Usable as ordinary IO, but avoid forcing it to a fixed level at power-up.
[^s-data]: Committed to the Duta DATA console bridge (UART0). The ROM also prints its boot log on GPIO43 at reset, so expect a burst of chip chatter on D6 right after power-up; reassigning these drops the console.

## Onboard hardware

- **User LED** on **GPIO21**, **active low** (drive low to light).
- **Charge LED** (battery status).
- **BOOT button** on GPIO0; **RESET** on CHIP_PU.
- No NeoPixel.

Strapping pins: GPIO0, GPIO45, GPIO46.

## Flashing

Native USB-C. Hold **BOOT**, plug in, release (esptool / `pio run -t upload`).

## Duta on this board

- **Transport:** `usb-mux` (DATA + CMD over the native USB-CDC via skrit-mux).
- **DATA console:** UART0 on the silkscreened TX/RX, GPIO43 / GPIO44 (D6/D7).
- **Default outputs:** the onboard user LED (GPIO21, active low) as the aux LED.
  Everything else is wired to free pads and added at runtime via Configure Device.
- **Build:** `pio run -d platforms/espressif -e xiaos3`

## Links

- Wiki: <https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/>
- ESP32-S3 datasheet: <https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf>
