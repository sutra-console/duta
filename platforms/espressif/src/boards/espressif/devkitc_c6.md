# Espressif ESP32-C6-DevKitC-1

The reference dev board for the ESP32-C6 (RISC-V, Wi-Fi 6 + BLE 5 + **802.15.4**
for Zigbee/Thread). Vendored as `devkitc_c6.h`; the Duta build on it is
`targets/duta_devkitc_c6.h`. Two USB-C ports and an addressable RGB LED.

## Specs

| | |
|---|---|
| SoC | ESP32-C6 (RISC-V HP core ≤160 MHz + LP core) |
| Radios | Wi-Fi 6 (2.4 GHz), BLE 5, **IEEE 802.15.4** (Zigbee / Thread / Matter) |
| Module | ESP32-C6-WROOM-1 (PCB ant) / -1U (u.FL) |
| Flash | **4 MB** on this unit (GigaDevice GD25Q32, in-package; WROOM-1 also ships 8 MB — read the real size with `esptool flash-id`) |
| PSRAM | none |
| USB | 2× USB-C: native ESP32-C6 USB **and** a USB-to-UART bridge (CH343 on this revision) |

## The two USB ports

| Port | Goes to | GPIO | Duta role |
|------|---------|------|-----------|
| **ESP32-C6 USB** | the C6's native USB-Serial/JTAG | D− = GPIO12, D+ = GPIO13 | the **skrit mux** (CMD + DATA) — what Sutra connects to |
| **USB-to-UART** | the bridge chip ↔ UART0 | TX = GPIO16 (U0TXD), RX = GPIO17 (U0RXD) | the **DATA console** (we wire DATA to U0), so this port is a plain serial console |

GPIO12/13 are committed to native USB (forbidden for IO). The ROM also logs its
boot output on U0 (GPIO16/17) at reset, so expect chip chatter on the UART-bridge
port right after power-up.

## Onboard hardware

- **Addressable RGB LED (WS2812)** on **GPIO8** ⚠️ (also a strapping pin).
- **BOOT button** (GPIO9, the boot strapping pin) and **RESET button** (CHIP_EN).
- 5 V / 3V3 / GND rails + the J1/J3 GPIO headers (every usable GPIO is broken out).

**Strapping pins (sampled at reset — usable as IO, just don't force them at
power-up):** GPIO4 (MTMS), GPIO5 (MTDI), GPIO8, GPIO9, GPIO15. ADC1 is on
GPIO0-6. The in-package SPI flash uses the GPIO24-30 region (not broken out).

## Flashing

Native USB-C (esptool over GPIO12/13). Hold **BOOT**, tap **RESET**, release to
enter the ROM downloader; or the app's *Reboot → bootloader* once Duta is running.
The ROM downloader is in silicon, so the board is unbrickable.

> **Pure-IDF note:** PlatformIO's `framework = espidf` does **not** emit
> `bootloader.bin`, so `pio run -t upload` fails. Flash the three images directly
> with esptool — a chip-matching bootloader comes from the arduino-esp32 libs
> (`framework-arduinoespressif32-libs/esp32c6/bin/bootloader_<mode>_<freq>.elf`,
> `esptool elf2image` it), the partition table + app from `.pio/build/esp32c6/`.
> Use the flash mode/freq the embedded flash supports.

## Duta on this board

- **Transport:** `usb-mux` (DATA + CMD over the native USB-Serial/JTAG via skrit-mux).
- **DATA console:** UART0 on GPIO16/17 → the board's UART-bridge USB port.
- **Default output:** the onboard WS2812 (GPIO8) as the RGB output.
- **Build:** `pio run -d platforms/espressif -e esp32c6` (pure-IDF).
- The 802.15.4 radio makes this a natural Zigbee/Thread node for the network-model work.

## Links

- User guide: <https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html>
- ESP32-C6 datasheet: <https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf>
