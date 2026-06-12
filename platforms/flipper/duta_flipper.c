// duta_flipper.c — Duta on the Flipper Zero (an external .fap app).
// ============================================================================
// Turns a Flipper into a Duta skrit node. The host link is the Flipper's USB
// CDC (Sutra/MCP connect to it like any serial Duta, the skrit-mux stream); the
// DATA console is bridged to a target over the GPIO-header USART (pin 13 TX /
// pin 14 RX); the Duta outputs are GPIO-header pins plus the onboard RGB LED.
// A status screen shows the link + IO state. Built with `ufbt`.
//
// The portable skrit core (skrit_device.h + protocol.h) is vendored here (like
// duta/protocol mirrors skrit) so the .fap builds with no extra include paths.
// ============================================================================
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_cdc.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "skrit_device.h" // the portable core (vendored; pulls protocol.h locally)

#define FW_LO 0x05
#define FW_HI 0x00
#define DUTA_RING 1024
#define CDC_IF 0 // the single CDC interface

// ---- Duta outputs: GPIO-header pins + the onboard RGB LED -------------------
typedef struct {
    const GpioPin* pin;
    const char* name;
} DutaGpioOut;
static const DutaGpioOut duta_gpio[] = {
    {&gpio_ext_pa7, "GPIO PA7"}, // header pin 2
    {&gpio_ext_pa6, "GPIO PA6"}, // pin 3
    {&gpio_ext_pa4, "GPIO PA4"}, // pin 4
    {&gpio_ext_pb3, "GPIO PB3"}, // pin 5
};
#define N_GPIO (sizeof(duta_gpio) / sizeof(duta_gpio[0]))
#define RGB_IDX N_GPIO
#define N_OUT (N_GPIO + 1) // gpios + the RGB LED

typedef struct {
    FuriHalSerialHandle* serial; // DATA bridge: USART on the GPIO header
    FuriStreamBuffer* usb_rx;    // CDC -> loop
    FuriStreamBuffer* uart_rx;   // USART -> loop
    NotificationApp* notif;
    FuriHalUsbInterface* usb_prev;
    Gui* gui;
    ViewPort* vp;
    FuriMessageQueue* input_q;
    skrit_dev dev;
    skrit_hal hal;
    // display state
    volatile bool usb_linked;
    uint32_t baud;
    uint8_t out_state[N_OUT];
    uint8_t rgb[3];
    uint32_t rx_bytes, tx_bytes;
    bool running;
} Duta;

static Duta* g_duta;

// ---- onboard RGB LED via the notification service --------------------------
static void duta_led_apply(uint8_t r, uint8_t g, uint8_t b) {
    NotificationMessage m_r = {.type = NotificationMessageTypeLedRed, .data.led.value = r};
    NotificationMessage m_g = {.type = NotificationMessageTypeLedGreen, .data.led.value = g};
    NotificationMessage m_b = {.type = NotificationMessageTypeLedBlue, .data.led.value = b};
    const NotificationSequence seq = {&m_r, &m_g, &m_b, &message_do_not_reset, NULL};
    notification_message(g_duta->notif, &seq);
}

// ---- skrit_hal callbacks ---------------------------------------------------
static void hal_link_write(void* c, const uint8_t* p, uint16_t n) {
    UNUSED(c);
    furi_hal_cdc_send(CDC_IF, (uint8_t*)p, n); // device -> host (CMD/console over USB)
    g_duta->tx_bytes += n;
}
static void hal_data_write(void* c, const uint8_t* p, uint16_t n) {
    UNUSED(c);
    if(g_duta->serial) furi_hal_serial_tx(g_duta->serial, p, n); // host -> target UART
}
static uint16_t hal_data_read(void* c, uint8_t* out, uint16_t cap) {
    UNUSED(c);
    return (uint16_t)furi_stream_buffer_receive(g_duta->uart_rx, out, cap, 0);
}

static void hal_out_set(void* c, uint8_t idx, uint8_t on) {
    UNUSED(c);
    if(idx >= N_OUT) return;
    g_duta->out_state[idx] = on ? 1 : 0;
    if(idx < N_GPIO) {
        furi_hal_gpio_write(duta_gpio[idx].pin, on ? true : false);
    } else { // RGB on/off -> dim white / off
        uint8_t v = on ? 0x40 : 0;
        g_duta->rgb[0] = g_duta->rgb[1] = g_duta->rgb[2] = v;
        duta_led_apply(v, v, v);
    }
}
static uint8_t hal_out_get(void* c, uint8_t idx) {
    UNUSED(c);
    return idx < N_OUT ? g_duta->out_state[idx] : 0;
}
static void hal_out_desc(void* c, uint8_t idx, uint8_t* type, const char** name) {
    UNUSED(c);
    if(idx >= N_OUT) return;
    if(idx < N_GPIO) {
        *type = SKRIT_CTRL_IO;
        *name = duta_gpio[idx].name;
    } else {
        *type = SKRIT_CTRL_RGB;
        *name = "RGB LED";
    }
}

static uint8_t hal_rgb_count(void* c, uint8_t idx) {
    UNUSED(c);
    return idx == RGB_IDX ? 1 : 0;
}
static uint8_t hal_rgb_set(void* c, uint8_t idx, uint8_t px, uint8_t r, uint8_t g, uint8_t b) {
    UNUSED(c);
    UNUSED(px);
    if(idx != RGB_IDX) return 0;
    g_duta->rgb[0] = r;
    g_duta->rgb[1] = g;
    g_duta->rgb[2] = b;
    duta_led_apply(r, g, b);
    g_duta->out_state[idx] = (r | g | b) ? 1 : 0;
    return 1;
}
static void hal_rgb_get(void* c, uint8_t idx, uint8_t px, uint8_t* r, uint8_t* g, uint8_t* b) {
    UNUSED(c);
    UNUSED(idx);
    UNUSED(px);
    *r = g_duta->rgb[0];
    *g = g_duta->rgb[1];
    *b = g_duta->rgb[2];
}

static void
    hal_proto_get(void* c, uint8_t idx, uint32_t* value, uint8_t* o0, uint8_t* o1, uint8_t* o2) {
    UNUSED(c);
    UNUSED(idx);
    *value = g_duta->baud; // uart: value=baud, opt0=data_bits, opt1=parity, opt2=stop
    *o0 = 8;
    *o1 = SKRIT_PAR_NONE;
    *o2 = 1;
}
static void
    hal_proto_set(void* c, uint8_t idx, uint32_t value, uint8_t o0, uint8_t o1, uint8_t o2) {
    UNUSED(c);
    UNUSED(idx);
    UNUSED(o0);
    UNUSED(o1);
    UNUSED(o2);
    if(value) {
        g_duta->baud = value;
        if(g_duta->serial) furi_hal_serial_set_br(g_duta->serial, value);
    }
}

static void hal_reboot(void* c, uint8_t mode) {
    UNUSED(c);
    UNUSED(mode); // app/bootloader: just reset the Flipper (DFU is a button combo)
    furi_hal_power_reset();
}
static uint32_t hal_millis(void* c) {
    UNUSED(c);
    return furi_get_tick(); // Flipper kernel tick = 1 kHz, so ticks are ms
}
static void hal_pump(void* c) {
    UNUSED(c);
    furi_thread_yield();
}

// ---- USB CDC callbacks (USB context) ---------------------------------------
static void cdc_rx(void* ctx) {
    UNUSED(ctx);
    uint8_t buf[CDC_DATA_SZ];
    int32_t len = furi_hal_cdc_receive(CDC_IF, buf, sizeof(buf));
    if(len > 0) furi_stream_buffer_send(g_duta->usb_rx, buf, (size_t)len, 0);
}
static void cdc_state(void* ctx, CdcState state) {
    UNUSED(ctx);
    g_duta->usb_linked = (state == CdcStateConnected);
}
static void cdc_tx(void* ctx) {
    UNUSED(ctx);
}
static void cdc_ctrl_line(void* ctx, CdcCtrlLine lines) {
    UNUSED(ctx);
    UNUSED(lines);
}
static void cdc_config(void* ctx, struct usb_cdc_line_coding* cfg) {
    UNUSED(ctx);
    UNUSED(cfg);
}
static CdcCallbacks cdc_cb = {cdc_tx, cdc_rx, cdc_state, cdc_ctrl_line, cdc_config};

// ---- DATA target UART rx (serial context) ----------------------------------
static void uart_rx(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    UNUSED(ctx);
    if(event == FuriHalSerialRxEventData) {
        uint8_t b = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(g_duta->uart_rx, &b, 1, 0);
    }
}

// ---- GUI -------------------------------------------------------------------
static void draw_callback(Canvas* canvas, void* ctx) {
    Duta* d = ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "DUTA");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 36, 11, d->usb_linked ? "host linked" : "plug in host");
    canvas_draw_line(canvas, 0, 14, 128, 14);

    char line[40];
    snprintf(line, sizeof(line), "DATA UART %lu", (unsigned long)d->baud);
    canvas_draw_str(canvas, 2, 25, line);
    snprintf(
        line,
        sizeof(line),
        "rx %lu  tx %lu",
        (unsigned long)d->rx_bytes,
        (unsigned long)d->tx_bytes);
    canvas_draw_str(canvas, 2, 35, line);

    canvas_draw_str(canvas, 2, 49, "OUT");
    for(uint8_t i = 0; i < N_GPIO; i++) {
        int x = 24 + i * 14;
        if(d->out_state[i])
            canvas_draw_box(canvas, x, 42, 11, 8);
        else
            canvas_draw_frame(canvas, x, 42, 11, 8);
    }
    // RGB swatch (filled if lit)
    if(d->out_state[RGB_IDX])
        canvas_draw_box(canvas, 24 + N_GPIO * 14, 42, 11, 8);
    else
        canvas_draw_frame(canvas, 24 + N_GPIO * 14, 42, 11, 8);

    canvas_draw_str(canvas, 2, 62, "Open the USB serial in Sutra");
}
static void input_callback(InputEvent* event, void* ctx) {
    FuriMessageQueue* q = ctx;
    furi_message_queue_put(q, event, FuriWaitForever);
}

// ---- entry point -----------------------------------------------------------
int32_t duta_app(void* p) {
    UNUSED(p);
    Duta* d = malloc(sizeof(Duta));
    memset(d, 0, sizeof(Duta));
    g_duta = d;
    d->baud = 115200;
    d->running = true;
    d->usb_rx = furi_stream_buffer_alloc(DUTA_RING, 1);
    d->uart_rx = furi_stream_buffer_alloc(DUTA_RING, 1);
    d->notif = furi_record_open(RECORD_NOTIFICATION);
    d->input_q = furi_message_queue_alloc(8, sizeof(InputEvent));

    // GPIO outputs
    for(uint8_t i = 0; i < N_GPIO; i++) {
        furi_hal_gpio_init(duta_gpio[i].pin, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
        furi_hal_gpio_write(duta_gpio[i].pin, false);
    }

    // DATA bridge UART (GPIO-header USART, pin 13 TX / pin 14 RX)
    d->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(d->serial) {
        furi_hal_serial_init(d->serial, d->baud);
        furi_hal_serial_async_rx_start(d->serial, uart_rx, d, false);
    }

    // Host link: take over the USB CDC for the skrit-mux stream
    d->usb_prev = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    furi_hal_usb_set_config(&usb_cdc_single, NULL);
    furi_hal_cdc_set_callbacks(CDC_IF, &cdc_cb, d);
    d->usb_linked = (furi_hal_cdc_get_ctrl_line_state(CDC_IF) & CdcCtrlLineDTR) != 0;

    // GUI
    d->vp = view_port_alloc();
    view_port_draw_callback_set(d->vp, draw_callback, d);
    view_port_input_callback_set(d->vp, input_callback, d->input_q);
    d->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(d->gui, d->vp, GuiLayerFullscreen);

    // skrit core (muxed: CMD + DATA over the one USB endpoint)
    skrit_hal* h = &d->hal;
    h->name = "Duta Flipper";
    h->fw_ver = (FW_HI << 8) | FW_LO;
    h->caps = SKRIT_CAP_MUX | SKRIT_CAP_SERIAL | SKRIT_CAP_REBOOT;
    h->macro_tier = SKRIT_TIER_INTERACTIVE;
    h->n_outputs = N_OUT;
    h->link_write = hal_link_write;
    h->data_write = hal_data_write;
    h->data_read = hal_data_read;
    h->out_set = hal_out_set;
    h->out_get = hal_out_get;
    h->out_desc = hal_out_desc;
    h->rgb_count = hal_rgb_count;
    h->rgb_set = hal_rgb_set;
    h->rgb_get = hal_rgb_get;
    h->proto_get = hal_proto_get;
    h->proto_set = hal_proto_set;
    h->reboot = hal_reboot;
    h->millis = hal_millis;
    h->pump = hal_pump;
    skrit_dev_init(&d->dev, h, NULL, /*muxed*/ 1);

    uint8_t buf[64];
    uint32_t last_draw = 0;
    while(d->running) {
        // host -> device (CMD frames + host DATA) over USB
        size_t n = furi_stream_buffer_receive(d->usb_rx, buf, sizeof(buf), 0);
        for(size_t i = 0; i < n; i++) skrit_dev_rx(&d->dev, buf[i]);
        d->rx_bytes += n;
        // target console -> host (tee'd onto the mux link)
        skrit_dev_poll(&d->dev);

        InputEvent ev;
        if(furi_message_queue_get(d->input_q, &ev, 0) == FuriStatusOk)
            if(ev.type == InputTypeShort && ev.key == InputKeyBack) d->running = false;

        if(furi_get_tick() - last_draw > 100) {
            view_port_update(d->vp);
            last_draw = furi_get_tick();
        }
        furi_delay_ms(2);
    }

    // teardown
    gui_remove_view_port(d->gui, d->vp);
    furi_record_close(RECORD_GUI);
    view_port_free(d->vp);
    furi_hal_cdc_set_callbacks(CDC_IF, NULL, NULL);
    furi_hal_usb_set_config(d->usb_prev, NULL); // restore the CLI's USB config
    if(d->serial) {
        furi_hal_serial_async_rx_stop(d->serial);
        furi_hal_serial_deinit(d->serial);
        furi_hal_serial_control_release(d->serial);
    }
    for(uint8_t i = 0; i < N_GPIO; i++)
        furi_hal_gpio_init(duta_gpio[i].pin, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    duta_led_apply(0, 0, 0);
    furi_record_close(RECORD_NOTIFICATION);
    furi_message_queue_free(d->input_q);
    furi_stream_buffer_free(d->usb_rx);
    furi_stream_buffer_free(d->uart_rx);
    free(d);
    g_duta = NULL;
    return 0;
}
