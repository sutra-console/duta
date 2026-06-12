# Duta TinyUF2 bootloader (ESP32-S3)

The ESP-IDF counterpart of the [nRF Duta UF2 bootloader](../../zephyr/bootloader/) —
a UF2 mass-storage drive that IDs as a Duta and is entered/exited by command,
built with ESP-IDF on **[TinyUF2](https://github.com/adafruit/tinyuf2)** (which
has a first-class ESP32-S2/S3 port). Same drag-drop UX, same family.

Like the nRF one, we vendor **only the board variant**; `build.sh` clones TinyUF2
at a pinned SHA and overlays `boards/duta_s3zero` onto a stock 4MB/no-PSRAM S3
board (`adafruit_feather_esp32s3_nopsram`), changing just the identity + pins.

## Read this first — ESP32 is not the nRF

Three differences that change what a custom bootloader buys you:

1. **Command-into-bootloader already works**, no TinyUF2 needed. The app's
   `hal_reboot` ([`../src/main_idf.c`](../src/main_idf.c)) honors skrit
   `REBOOT mode=1` by setting `RTC_CNTL_FORCE_DOWNLOAD_BOOT` + restarting → the
   chip enters the **ROM serial/JTAG downloader**, and esptool flashes it. No
   button. You have the "no-button flash" half today.
2. **The ROM downloader is immutable** — it lives in silicon, enumerates as
   Espressif `303A:1001`, and you cannot rebrand it. So you can't "ID the board"
   *there*. TinyUF2 is the layer you customize (its USB descriptors + UF2 drive).
3. **You can't brick it.** Because the ROM downloader is always in silicon
   (reachable via the BOOT button + esptool), flashing/replacing TinyUF2 is
   risk-free — the ROM is the permanent recovery net. No SWD, no jig. This is the
   one place ESP32 is *easier* than the nRF.

So: TinyUF2 here adds **Duta identity + a UF2 drag-drop drive + auto-exit-to-app**,
on top of the command-entry you already have.

## What TinyUF2 adds

- **Identity**: USB VID `0x1209` (Duta family) + a distinct bootloader PID,
  product `Duta S3-Zero`, volume label **`DUTA`**, Board-ID `ESP32-S3-duta-s3zero`.
- **Enter by command**: the app requests UF2 via the ESP reset-reason hint
  `0x11F2` (RTC store6, survives a soft reset — the analog of the nRF
  `GPREGRET=0x57`):
  ```c
  static void reboot_to_uf2(void) {
    enum { APP_REQUEST_UF2_RESET_HINT = 0x11F2 };
    (void) esp_reset_reason();                       // links the setter
    esp_reset_reason_set_hint(APP_REQUEST_UF2_RESET_HINT);
    esp_restart();
  }
  ```
  To wire it to skrit, point `REBOOT mode=1` at `reboot_to_uf2()` **instead of**
  the ROM-download path — when you build the firmware to pair with TinyUF2. (They
  are mutually exclusive: ROM-download bypasses TinyUF2 into the ROM downloader;
  the hint enters TinyUF2's UF2 drive. Pick per build.)
- **Exit**: auto-resets to the app after a UF2 is written; double-tap reset or
  holding BOOT during the indicator forces UF2 manually.

## Build

Needs a full **ESP-IDF** with `idf.py` on PATH (run IDF's `export.sh`/`export.ps1`
first). Then:

```sh
./build.sh         # -> dist/duta_s3zero_tinyuf2.bin
```

## Flash (the partition integration)

TinyUF2 is its own ESP-IDF app living in a dedicated partition, with the **main
Duta app moved into an OTA slot**. Adopting it therefore replaces the espressif
firmware's partition table with TinyUF2's scheme (`tinyuf2` + `ota_0`/`ota_1`).
First-time install over the ROM downloader (drop in via `REBOOT mode=1`):

```sh
esptool --chip esp32s3 -p COM32 write_flash \
  0x0000  bootloader.bin \
  0x8000  partition-table.bin \
  0xe000  ota_data_initial.bin \
  0x2d0000 dist/duta_s3zero_tinyuf2.bin
```

(offsets per TinyUF2's port README; the Duta app is then flashed as a UF2, or to
the OTA slot). After this, day-to-day flashing is: `REBOOT mode=1` → the **`DUTA`**
UF2 drive mounts → drag the app `.uf2`. Recovery is always the BOOT button + esptool.

## Status

Authored, **not yet built here** — this machine has no exported ESP-IDF /
`idf.py` on PATH (the PlatformIO IDF isn't directly `idf.py`-invokable). Building
+ the partition-table integration into the espressif env is the remaining step.

## Notes / gotchas

- `USB_PID 0x5DE0` is a placeholder in the `0x1209` space — register a real
  pid.codes PID before shipping.
- Keep the base board 4MB/no-PSRAM to match the S3-Zero; a PSRAM/flash mismatch
  just fails to boot TinyUF2 (recover via the ROM downloader — not a brick).
- The S3-Zero's WS2812 is RGB-order; the TinyUF2 indicator color may look swapped
  (cosmetic).
- Bump `UPSTREAM_SHA` in `build.sh` deliberately; the board variant is decoupled
  from upstream, so rebases are cheap.
