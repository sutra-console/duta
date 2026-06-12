// duta_ws2812_rmt.h — minimal WS2812 driver on the ESP-IDF RMT TX API.
// Self-contained (no led_strip managed component): a tiny bytes-encoder that
// clocks out GRB/RGB pixels. Used by the pure-IDF firmware for the onboard
// addressable LED. -DDUTA_PURE_IDF.
#ifndef DUTA_WS2812_RMT_H
#define DUTA_WS2812_RMT_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"

#define WS2812_MAX 32

static rmt_channel_handle_t ws_chan;
static rmt_encoder_handle_t ws_enc;
static uint8_t ws_buf[WS2812_MAX * 3]; // on-wire order (GRB or RGB per ws_rgb)
static uint16_t ws_n;
static bool ws_rgb; // true = send R,G,B; false = G,R,B (the WS2812 default)

static void ws2812_init(int gpio, uint16_t count, bool rgb_order) {
  ws_n = count > WS2812_MAX ? WS2812_MAX : count;
  ws_rgb = rgb_order;
  rmt_tx_channel_config_t cc = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .gpio_num = gpio,
      .mem_block_symbols = 64,
      .resolution_hz = 10 * 1000 * 1000, // 10 MHz → 0.1 µs/tick
      .trans_queue_depth = 4,
  };
  rmt_new_tx_channel(&cc, &ws_chan);
  rmt_bytes_encoder_config_t ec = {
      .bit0 = {.level0 = 1, .duration0 = 4, .level1 = 0, .duration1 = 8}, // 0.4µs H / 0.8µs L
      .bit1 = {.level0 = 1, .duration0 = 8, .level1 = 0, .duration1 = 4}, // 0.8µs H / 0.4µs L
      .flags = {.msb_first = 1},
  };
  rmt_new_bytes_encoder(&ec, &ws_enc);
  rmt_enable(ws_chan);
}

static void ws2812_set(uint16_t px, uint8_t r, uint8_t g, uint8_t b) {
  if (px >= ws_n) return;
  uint8_t *p = &ws_buf[px * 3];
  if (ws_rgb) { p[0] = r; p[1] = g; p[2] = b; }
  else { p[0] = g; p[1] = r; p[2] = b; }
}
static void ws2812_get(uint16_t px, uint8_t *r, uint8_t *g, uint8_t *b) {
  if (px >= ws_n) { *r = *g = *b = 0; return; }
  uint8_t *p = &ws_buf[px * 3];
  if (ws_rgb) { *r = p[0]; *g = p[1]; *b = p[2]; }
  else { *g = p[0]; *r = p[1]; *b = p[2]; }
}
static void ws2812_show(void) {
  rmt_transmit_config_t tc = {.loop_count = 0};
  rmt_transmit(ws_chan, ws_enc, ws_buf, (size_t)ws_n * 3, &tc);
  rmt_tx_wait_all_done(ws_chan, 100); // the >50µs idle after = the WS2812 reset latch
}

#endif // DUTA_WS2812_RMT_H
