/*
  Duta - dual-CDC USB-to-TTL adapter for WeAct CH552
  ----------------------------------------------------------------------
  Exposes TWO virtual COM ports over one USB cable:

    CDC #1  "Duta DATA" : transparent USB <-> UART0 bridge (auto-baud)
                               P3.1 = TXD -> target RX
                               P3.0 = RXD -> target TX
    CDC #2  "Duta CMD"  : line-based control port
        R1 ON | R1 OFF | R1 TOGGLE | R2 ...   2x relay (active-LOW)
        STATUS   -> "R1=0 R2=0"
        PING     -> "PONG"          (use to tell the two ports apart)
        ID       -> firmware id
        HELP     -> command list
        OLED <t> -> show text on the SSD1306 (when ENABLE_OLED)

  Optional SSD1306 128x64 OLED mirrors the DATA port's RX as scrolling text.

  Build: ch55xduino, board option "USER CODE w/ 266B USB ram" (user266).
  See README.md.

  Public domain.
*/

#ifndef USER_USB_RAM
#error "Compile with a USER USB setting (Tools > USB Settings > USER CODE w/ 266B USB ram)"
#endif

#include "src/config.h" // selects the board -> RELAY*/LED*/OLED* pins
#include "src/usb/USBCDC.h"

// ---------------------------------------------------------------------------
// Pin map comes from the selected board (src/boards/*.h via src/config.h):
//   RELAY1_PIN RELAY2_PIN RELAY_ACTIVE_LOW  LED_PIN LED_ACTIVE_LOW
//   OLED_SCL_PIN OLED_SDA_PIN OLED_I2C_ADDR
// ---------------------------------------------------------------------------
#define DEFAULT_BAUD   9600UL

#if ENABLE_OLED
#include "src/oled/ssd1306.h"
#endif

#define FW_VERSION "Duta v3 dual-cdc"
#define FW_VER_LO  0x03
#define FW_VER_HI  0x00

// ---------------------------------------------------------------------------
__xdata uint32_t currentBaud = DEFAULT_BAUD;

__bit relay1On = 0;
__bit relay2On = 0;
__bit ledOn = 0;

#define CMD_BUF_SIZE 32
__xdata char cmdBuf[CMD_BUF_SIZE];
__data uint8_t cmdLen = 0;

static void applyRelay(uint8_t pin, __bit on) {
#if RELAY_ACTIVE_LOW
  digitalWrite(pin, on ? LOW : HIGH);
#else
  digitalWrite(pin, on ? HIGH : LOW);
#endif
}

static void applyLed(__bit on) {
#if LED_ACTIVE_LOW
  digitalWrite(LED_PIN, on ? LOW : HIGH);
#else
  digitalWrite(LED_PIN, on ? HIGH : LOW);
#endif
}

static __bit bufEq(__xdata char *s, __code char *t) {
  while (*t) {
    if (*s != *t)
      return 0;
    s++;
    t++;
  }
  return (*s == '\0');
}

static void printlnB(__code char *s) {
  while (*s)
    CdcB_write(*s++);
  CdcB_write('\r');
  CdcB_write('\n');
}

static void reportStatus(void) {
  CdcB_print("R1=");
  CdcB_write(relay1On ? '1' : '0');
  CdcB_print(" R2=");
  CdcB_write(relay2On ? '1' : '0');
  CdcB_print(" LED=");
  CdcB_write(ledOn ? '1' : '0');
  CdcB_write('\r');
  CdcB_write('\n');
}

// Handle one '\0'-terminated, upper-cased command line.
static void handleCommand(void) {
  if (cmdLen == 0)
    return;

  if (bufEq(cmdBuf, "PING")) {
    printlnB("PONG");
  } else if (bufEq(cmdBuf, "ID")) {
    printlnB(FW_VERSION);
  } else if (bufEq(cmdBuf, "STATUS")) {
    reportStatus();
  } else if (bufEq(cmdBuf, "HELP")) {
    printlnB("CMDS: R1/R2/LED ON|OFF|TOGGLE, STATUS, PING, ID, OLED <t>, HELP");
  } else if (bufEq(cmdBuf, "LED ON")) {
    ledOn = 1; applyLed(1); printlnB("OK");
  } else if (bufEq(cmdBuf, "LED OFF")) {
    ledOn = 0; applyLed(0); printlnB("OK");
  } else if (bufEq(cmdBuf, "LED TOGGLE")) {
    ledOn = !ledOn; applyLed(ledOn); printlnB("OK");
  } else if (bufEq(cmdBuf, "R1 ON")) {
    relay1On = 1; applyRelay(RELAY1_PIN, 1); printlnB("OK");
  } else if (bufEq(cmdBuf, "R1 OFF")) {
    relay1On = 0; applyRelay(RELAY1_PIN, 0); printlnB("OK");
  } else if (bufEq(cmdBuf, "R1 TOGGLE")) {
    relay1On = !relay1On; applyRelay(RELAY1_PIN, relay1On); printlnB("OK");
  } else if (bufEq(cmdBuf, "R2 ON")) {
    relay2On = 1; applyRelay(RELAY2_PIN, 1); printlnB("OK");
  } else if (bufEq(cmdBuf, "R2 OFF")) {
    relay2On = 0; applyRelay(RELAY2_PIN, 0); printlnB("OK");
  } else if (bufEq(cmdBuf, "R2 TOGGLE")) {
    relay2On = !relay2On; applyRelay(RELAY2_PIN, relay2On); printlnB("OK");
  } else if (cmdBuf[0] == 'O' && cmdBuf[1] == 'L' && cmdBuf[2] == 'E' &&
             cmdBuf[3] == 'D' && cmdBuf[4] == ' ') {
#if ENABLE_OLED
    oledClear();
    oledPrint(&cmdBuf[5]);
    printlnB("OK");
#else
    printlnB("ERR oled-disabled");
#endif
  } else {
    printlnB("ERR");
  }
}

// ===========================================================================
// Binary CMD protocol (see ../../protocol/PROTOCOL.md)
//   wire frame = 0x00  COBS( TYPE SEQ LEN BODY[LEN] CRC8 )  0x00
// Coexists with the ASCII line commands above via a small mode state machine:
//   idle + 0x00      -> binary frame starting
//   idle + printable -> ASCII line
// ===========================================================================
#define BIN_BUF_SIZE 72
__xdata uint8_t binBuf[BIN_BUF_SIZE]; // incoming COBS bytes / outgoing COBS bytes
__xdata uint8_t pbuf[BIN_BUF_SIZE];   // decoded request / built response (raw)
__xdata uint8_t respBody[40];         // room for self-describe name strings
__data uint8_t binLen = 0;

#define M_IDLE 0
#define M_ASCII 1
#define M_BIN 2
__data uint8_t cmdMode = M_IDLE;

// message types / responses
#define T_PING 0x01
#define T_INFO 0x02
#define T_DEV_NAME 0x03 // self-describe: device name string
#define T_OUT_SET 0x10
#define T_OUT_GET 0x11
#define T_OUT_TOG 0x12
#define T_OUT_DESC 0x13 // self-describe: per-output {index, type, name}
#define T_RESP 0x80
#define ST_OK 0x00
#define ST_BADCRC 0x01
#define ST_UNKNOWN 0x02
#define ST_BADARGS 0x03
#define ST_STORAGE 0x04

static uint8_t crc8b(__xdata uint8_t *p, __data uint8_t n) {
  __data uint8_t c = 0;
  while (n--) {
    c ^= *p++;
    for (__data uint8_t i = 0; i < 8; i++)
      c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  }
  return c;
}

// COBS-decode binBuf[0..len) -> pbuf; returns decoded length or 0xFF on error.
static uint8_t cobsDecode(__data uint8_t len) {
  __data uint8_t i = 0, outn = 0;
  while (i < len) {
    __data uint8_t code = binBuf[i++];
    if (code == 0)
      return 0xFF;
    for (__data uint8_t j = 1; j < code; j++) {
      if (i >= len || outn >= BIN_BUF_SIZE)
        return 0xFF;
      pbuf[outn++] = binBuf[i++];
    }
    if (code < 0xFF && i < len) {
      if (outn >= BIN_BUF_SIZE)
        return 0xFF;
      pbuf[outn++] = 0;
    }
  }
  return outn;
}

// COBS-encode pbuf[0..n) -> binBuf; returns encoded length.
static uint8_t cobsEncode(__data uint8_t n) {
  __data uint8_t outn = 0, codeIdx = 0, code = 1;
  binBuf[outn++] = 0;
  for (__data uint8_t i = 0; i < n; i++) {
    if (pbuf[i] == 0) {
      binBuf[codeIdx] = code;
      codeIdx = outn;
      binBuf[outn++] = 0;
      code = 1;
    } else {
      binBuf[outn++] = pbuf[i];
      if (++code == 0xFF) {
        binBuf[codeIdx] = code;
        codeIdx = outn;
        binBuf[outn++] = 0;
        code = 1;
      }
    }
  }
  binBuf[codeIdx] = code;
  return outn;
}

static uint8_t outBitmap(void) {
  return (relay1On ? 1 : 0) | (relay2On ? 2 : 0) | (ledOn ? 4 : 0);
}

static void applyOutput(__data uint8_t idx, __bit on) {
  if (idx == 0) { relay1On = on; applyRelay(RELAY1_PIN, on); }
  else if (idx == 1) { relay2On = on; applyRelay(RELAY2_PIN, on); }
  else if (idx == 2) { ledOn = on; applyLed(on); }
}

static void toggleOutput(__data uint8_t idx) {
  if (idx == 0) applyOutput(0, !relay1On);
  else if (idx == 1) applyOutput(1, !relay2On);
  else if (idx == 2) applyOutput(2, !ledOn);
}

// Send a response frame (respBody must already be filled with blen bytes).
static void sendResp(__data uint8_t type, __data uint8_t seq, __data uint8_t blen) {
  __data uint8_t n = 0;
  pbuf[n++] = type;
  pbuf[n++] = seq;
  pbuf[n++] = blen;
  for (__data uint8_t i = 0; i < blen; i++)
    pbuf[n++] = respBody[i];
  pbuf[n] = crc8b(pbuf, n);
  n++;
  __data uint8_t en = cobsEncode(n);
  CdcB_write(0x00);
  for (__data uint8_t i = 0; i < en; i++)
    CdcB_write(binBuf[i]);
  CdcB_write(0x00);
  CdcB_flush();
}

static void handleBinFrame(void) {
  if (binLen == 0)
    return;
  __data uint8_t n = cobsDecode(binLen);
  if (n == 0xFF || n < 4)
    return;
  if (crc8b(pbuf, n - 1) != pbuf[n - 1]) {
    respBody[0] = ST_BADCRC;
    sendResp(pbuf[0] | T_RESP, pbuf[1], 1);
    return;
  }
  __data uint8_t type = pbuf[0];
  __data uint8_t seq = pbuf[1];
  __data uint8_t blen = pbuf[2];
  if ((uint8_t)(blen + 4) != n) {
    respBody[0] = ST_BADARGS;
    sendResp(type | T_RESP, seq, 1);
    return;
  }

  switch (type) {
  case T_PING:
    respBody[0] = ST_OK;
    respBody[1] = 'P'; respBody[2] = 'O'; respBody[3] = 'N'; respBody[4] = 'G';
    sendResp(T_PING | T_RESP, seq, 5);
    break;
  case T_INFO:
    respBody[0] = ST_OK;
    respBody[1] = FW_VER_LO;
    respBody[2] = FW_VER_HI;
    respBody[3] = (ENABLE_OLED ? 0x02 : 0x00) |
                  (PARITY_SUPPORT ? 0x08 : 0x00); // caps: bit1=OLED, bit3=parity
    respBody[4] = 3;                           // n_outputs (R1,R2,LED)
    respBody[5] = 0;                           // eeprom KB (none yet)
    respBody[6] = 1;                           // proto_ver
    sendResp(T_INFO | T_RESP, seq, 7);
    break;
  case T_OUT_SET: {
    __data uint8_t idx = pbuf[3], val = pbuf[4];
    applyOutput(idx, val ? 1 : 0);
    respBody[0] = ST_OK;
    sendResp(T_OUT_SET | T_RESP, seq, 1);
    break;
  }
  case T_OUT_GET:
    respBody[0] = ST_OK;
    respBody[1] = outBitmap();
    sendResp(T_OUT_GET | T_RESP, seq, 2);
    break;
  case T_OUT_TOG: {
    __data uint8_t idx = pbuf[3];
    toggleOutput(idx);
    respBody[0] = ST_OK;
    respBody[1] = outBitmap();
    sendResp(T_OUT_TOG | T_RESP, seq, 2);
    break;
  }
  case T_DEV_NAME: {
    respBody[0] = ST_OK;
    __code char *nm = BOARD_NAME;
    __data uint8_t n = 1;
    while (*nm && n < sizeof(respBody))
      respBody[n++] = *nm++;
    sendResp(T_DEV_NAME | T_RESP, seq, n);
    break;
  }
  case T_OUT_DESC: {
    __data uint8_t idx = pbuf[3];
    __code char *nm;
    __data uint8_t type; // 0 = relay, 1 = led
    if (idx == 0) {
      nm = "Relay 1";
      type = 0;
    } else if (idx == 1) {
      nm = "Relay 2";
      type = 0;
    } else if (idx == 2) {
      nm = "Aux LED";
      type = 1;
    } else {
      respBody[0] = ST_BADARGS;
      sendResp(T_OUT_DESC | T_RESP, seq, 1);
      break;
    }
    respBody[0] = ST_OK;
    respBody[1] = idx;
    respBody[2] = type;
    __data uint8_t n = 3;
    while (*nm && n < sizeof(respBody))
      respBody[n++] = *nm++;
    sendResp(T_OUT_DESC | T_RESP, seq, n);
    break;
  }
  default:
    // snippet/eeprom range (0x20..0x41): no storage until the EEPROM is fitted
    respBody[0] = (type >= 0x20 && type <= 0x41) ? ST_STORAGE : ST_UNKNOWN;
    sendResp(type | T_RESP, seq, 1);
    break;
  }
}

static void asciiPush(__data uint8_t c) {
  if (cmdLen < (CMD_BUF_SIZE - 1)) {
    if (c >= 'a' && c <= 'z')
      c -= 32; // upper-case for case-insensitive matching
    cmdBuf[cmdLen++] = c;
  }
}

static void pollCmdPort(void) {
  while (CdcB_available()) {
    __data uint8_t c = CdcB_read();
    if (cmdMode == M_IDLE) {
      if (c == 0x00) {
        cmdMode = M_BIN;
        binLen = 0;
      } else if (c >= 0x20) {
        cmdMode = M_ASCII;
        cmdLen = 0;
        asciiPush(c);
      }
      // \n, \r and stray control bytes while idle are ignored
    } else if (cmdMode == M_ASCII) {
      if (c == '\n' || c == '\r') {
        cmdBuf[cmdLen] = '\0';
        handleCommand();
        cmdMode = M_IDLE;
      } else if (c == 0x00) {
        cmdMode = M_BIN; // a binary frame is starting
        binLen = 0;
      } else {
        asciiPush(c);
      }
    } else { // M_BIN
      if (c == 0x00) {
        handleBinFrame();
        cmdMode = M_IDLE;
      } else if (binLen < BIN_BUF_SIZE) {
        binBuf[binLen++] = c;
      } else {
        cmdMode = M_IDLE; // overflow → drop frame
      }
    }
  }
  CdcB_flush();
}

void setup() {
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  applyRelay(RELAY1_PIN, 0);
  applyRelay(RELAY2_PIN, 0);
  applyLed(0);

  USBInit();
  Serial0_begin(currentBaud);

#if ENABLE_OLED
  oledInit();
  oledPrint(FW_VERSION);
#endif
}

void loop() {
  // ---- DATA port <-> UART0 bridge ----
  while (CdcA_available()) {
    Serial0_write(CdcA_read());
  }
  if (CdcA_connected()) {
    while (Serial0_available()) {
      char c = Serial0_read();
      CdcA_write(c);
#if ENABLE_OLED
      oledPutc(c);
#endif
    }
    CdcA_flush();
  }

  // ---- DATA port baud follows the host ----
  __xdata uint32_t reqBaud = *((__xdata uint32_t *)LineCodingA);
  if (reqBaud != currentBaud && reqBaud != 0) {
    currentBaud = reqBaud;
    Serial0_flush();
    Serial0_begin(currentBaud);
  }

  // ---- CMD port ----
  pollCmdPort();
}
