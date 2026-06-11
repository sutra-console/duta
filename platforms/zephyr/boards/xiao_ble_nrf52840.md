# Seeed Studio XIAO nRF52840

A thumb-sized (21 x 17.8 mm) nRF52840 board in the XIAO footprint, with native
USB-C, BLE/802.15.4 radio, and an external 2 MB QSPI flash. Built on Zephyr's
upstream `xiao_ble` board; the Duta wiring is in `xiao_ble_nrf52840.conf` +
`xiao_ble_nrf52840.overlay`. A Sense variant adds an IMU and PDM microphone.

## Specs

| | |
|---|---|
| SoC | Nordic nRF52840 (Cortex-M4F, 64 MHz) |
| RAM | 256 KB |
| Flash | 1 MB internal + 2 MB QSPI (P25Q16H) |
| Radio | BLE 5, 802.15.4, NFC |
| USB | USB-C, native USB-CDC |
| Size | 21 x 17.8 mm |

## Pinout (XIAO label to nRF port.pin)

| Pin | nRF | Function |
|-----|-----|----------|
| D0 / A0 | P0.02 | AIN0 |
| D1 / A1 | P0.03 | AIN1 |
| D2 / A2 | P0.28 | AIN4 |
| D3 / A3 | P0.29 | AIN5 |
| D4 / SDA | P0.04 | I2C SDA, AIN2 |
| D5 / SCL | P0.05 | I2C SCL, AIN3 |
| D6 / TX | P1.11 ⚠️[^n-data] | UART TX |
| D7 / RX | P1.12 ⚠️[^n-data] | UART RX |
| D8 / SCK | P1.13 | SPI SCK |
| D9 / MISO | P1.14 | SPI MISO |
| D10 / MOSI | P1.15 | SPI MOSI |

NFC pads: P0.09 / P0.10 ⚠️[^n-nfc].

[^n-data]: Committed to the Duta DATA console bridge (uart0). Reassigning it drops the target console.
[^n-nfc]: P0.09/P0.10 power up as NFC antenna pins. Using them as GPIO requires clearing the NFCPINS bit in UICR (a one-time config plus reset); until then they will not drive normally.

## Onboard hardware

- **RGB user LED** (3-in-one, **active low**, common anode): Red = P0.26,
  Green = P0.30, Blue = P0.06. Zephyr's `xiao_ble` board aliases these as
  `led0` / `led1` / `led2`.
- **Charge LED** on P0.17.
- **RESET** on P0.18; battery-read enable on P0.14.
- **Sense variant:** LSM6DS3TR-C IMU (power P1.08, INT P0.11) and a PDM mic
  (data P0.16, clock P1.00).

## Flashing

Adafruit nRF52 **UF2 bootloader**: double-tap **RESET** to mount the UF2 drive,
then drag `build/zephyr/zephyr.uf2` (the build emits UF2 via
`CONFIG_BUILD_OUTPUT_UF2`).

## Duta on this board

- **Transport:** `usb-mux` (DATA + CMD over a USB CDC ACM via skrit-mux; a BLE
  alternative is available with `overlay-ble.conf`).
- **DATA console:** `uart0`, routed by the board to the silkscreened TX/RX
  (P1.11 / P1.12, D6/D7).
- **Default outputs:** the board's own onboard RGB user LED (`led0/led1/led2`).
  No relays are pre-wired; those are external and provisioned onto free pads.
- **ADC input:** the nRF52840's internal VDD reading (mV), needs no pin.
- **Build:** `west build -b xiao_ble/nrf52840 platforms/zephyr`

## Links

- Wiki: <https://wiki.seeedstudio.com/XIAO_BLE/>
- Zephyr board: <https://docs.zephyrproject.org/latest/boards/seeed/xiao_ble/doc/index.html>
- nRF52840 datasheet: <https://docs.nordicsemi.com/bundle/ps_nrf52840/page/keyfeatures_html5.html>
