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

> **Pure-IDF flashing recipe (hard-won — works):** PlatformIO's
> `framework = espidf` does **not** emit `bootloader.bin`, and the C6 native
> USB-Serial/JTAG can't be reset-to-app by esptool (it lands in download). So:
>
> 1. **Flash over the CH343/UART port** (`COM4`-style, the *second* USB-C), not
>    the native USB — the UART bridge has the classic DTR/RTS auto-reset, so
>    esptool flashes **and** cleanly boots the app.
> 2. **Bootloader:** the pure-IDF build has none; borrow a chip-matching one from
>    the arduino libs but use **DIO @ 40 MHz** (the in-package GD25Q32 hangs on
>    QIO@80M → `TG0_WDT` boot loop):
>    `esptool --chip esp32c6 elf2image framework-arduinoespressif32-libs/esp32c6/bin/bootloader_dio_40m.elf`.
> 3. Flash all three with the conservative params, e.g.:
>    ```sh
>    uvx esptool --chip esp32c6 --port COM4 --before default-reset --after hard-reset \
>      write-flash --flash-mode dio --flash-freq 40m --flash-size 4MB \
>      0x0 bootloader_dio_40m.bin 0x8000 .pio/build/esp32c6/partitions.bin \
>      0x10000 .pio/build/esp32c6/firmware.bin
>    ```
>
> The **boot/console log appears on the CH343/UART port** (U0), and the **skrit
> mux on the native USB** (`303A:1001`). Verified: PING→PONG, DEVICE_NAME→
> "Duta ESP32-C6". (The durable fix is building our own bootloader via an
> esp-idf container, which would drop the arduino-bootloader borrow.)

## Duta on this board

- **Transport:** `usb-mux` (DATA + CMD over the native USB-Serial/JTAG via skrit-mux).
- **DATA console:** UART0 on GPIO16/17 → the board's UART-bridge USB port.
- **Default output:** the onboard WS2812 (GPIO8) as the RGB output.
- **Build:** `pio run -d platforms/espressif -e esp32c6` (pure-IDF).
- The 802.15.4 radio makes this a natural Zigbee/Thread node for the network-model work.

## Links

- User guide: <https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html>
- ESP32-C6 datasheet: <https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf>
