// Duta on Zephyr — skeleton. Builds for native_sim (CI) and real boards
// (nRF52840, ESP32, RP2040). The protocol/CRC/COBS come from protocol.h; the
// command dispatch mirrors platforms/host/src/main.c.
//
// TODO: wire a transport (USB CDC ACM or BLE NUS), accumulate 0x00-delimited
// frames, COBS-decode, and answer PING/INFO/DEVICE_NAME/OUTPUT_* — then bridge
// a hardware UART to the DATA console.
#include <zephyr/kernel.h>

#include "protocol.h"

int main(void) {
  printk("Duta zephyr skeleton — protocol IDs ok (PING=0x%02x)\n", SKRIT_PING);
  return 0;
}
