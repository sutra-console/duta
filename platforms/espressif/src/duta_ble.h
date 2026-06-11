// duta_ble.h — ESP32 BLE GATT transport (a connectable, pairing Duta peripheral).
// ============================================================================
// Parity with the nRF BLE transport: the ESP32 advertises the two skrit GATT
// services and Sutra connects to it over BLE instead of USB/WiFi.
//   DATA svc 6E40 0001 / 0002 (RX) / 0003 (TX)  — raw target console (NUS-compatible)
//   CMD  svc 6E41 0001 / 0002 (RX) / 0003 (TX)  — framed CMD protocol
// Security mirrors the nRF: the RX + CCC attributes require an ENCRYPTED link, so a
// central must complete LE Secure Connections pairing before it can drive the device
// or subscribe. No display/keyboard → Just Works; bonds persist in NVS. Built on the
// Arduino BLE (Bluedroid) server API; compiled in only for -DDUTA_BLE_TRANSPORT.
// ============================================================================
#ifndef DUTA_BLE_H
#define DUTA_BLE_H
#ifdef DUTA_BLE_TRANSPORT

#include <Arduino.h>
#include <BLE2902.h>
#include <BLECharacteristic.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <esp_gap_ble_api.h>
#include <esp_mac.h>

extern "C" {
#include "skrit_device.h"
}

#define DUTA_BLE_DATA_SVC "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define DUTA_BLE_DATA_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define DUTA_BLE_DATA_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define DUTA_BLE_CMD_SVC "6e410001-b5a3-f393-e0a9-e50e24dcca9e"
#define DUTA_BLE_CMD_RX "6e410002-b5a3-f393-e0a9-e50e24dcca9e"
#define DUTA_BLE_CMD_TX "6e410003-b5a3-f393-e0a9-e50e24dcca9e"

namespace duta_ble {

static skrit_dev *g_dev = nullptr;
static void (*g_data_sink)(const uint8_t *, uint16_t) = nullptr; // host -> target (DATA-RX)
static BLEServer *g_server = nullptr;
static BLECharacteristic *g_data_tx = nullptr;
static BLECharacteristic *g_cmd_tx = nullptr;
static volatile bool g_connected = false;
static uint16_t g_conn_id = 0;

// device -> host TX rings, drained in loop() so a congested controller never drops
// a notify mid-COBS-frame (Arduino notify() can't report congestion, so we pace it).
template <size_t N> struct Ring {
  uint8_t buf[N];
  volatile size_t head = 0, tail = 0;
  void push(const uint8_t *p, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) {
      size_t nh = (head + 1) % N;
      if (nh == tail) return; // full: bounded drop of the tail, never a partial head
      buf[head] = p[i];
      head = nh;
    }
  }
  size_t len() const { return (head + N - tail) % N; }
  void reset() { tail = head; }
};
static Ring<1024> g_data_q;
static Ring<768> g_cmd_q;

static void start_advertising() {
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(DUTA_BLE_CMD_SVC); // the skrit CMD UUID identifies a Duta in a scan
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
}

struct ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override {
    g_connected = true;
    g_conn_id = s->getConnId();
  }
  void onDisconnect(BLEServer *s) override {
    g_connected = false;
    g_data_q.reset();
    g_cmd_q.reset();
    s->startAdvertising(); // become connectable again
  }
};

// host -> device: DATA-RX bytes go to the target console; CMD-RX bytes feed the
// protocol dispatcher. Both characteristics are encrypted, so these only fire on
// a paired link.
struct DataRxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String v = c->getValue();
    if (g_data_sink && v.length()) g_data_sink((const uint8_t *)v.c_str(), (uint16_t)v.length());
  }
};
struct CmdRxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String v = c->getValue();
    for (size_t i = 0; i < v.length(); i++) skrit_dev_rx(g_dev, (uint8_t)v[i]);
  }
};

inline void cmd_notify(const uint8_t *p, uint16_t n) { g_cmd_q.push(p, n); }
inline void data_notify(const uint8_t *p, uint16_t n) { g_data_q.push(p, n); }

template <size_t N> static void drain(Ring<N> &q, BLECharacteristic *tx) {
  if (!g_connected || !tx) return;
  uint16_t mtu = g_server ? g_server->getPeerMTU(g_conn_id) : 23;
  uint16_t chunk = (mtu > 3) ? (uint16_t)(mtu - 3) : 20;
  uint8_t tmp[247];
  if (chunk > sizeof tmp) chunk = sizeof tmp;
  // A few chunks per loop pass: keep the controller fed without starving the loop.
  for (int pass = 0; pass < 8 && q.len(); pass++) {
    size_t avail = q.len();
    uint16_t c2 = avail < chunk ? (uint16_t)avail : chunk;
    for (uint16_t i = 0; i < c2; i++) tmp[i] = q.buf[(q.tail + i) % N];
    tx->setValue(tmp, c2);
    tx->notify();
    q.tail = (q.tail + c2) % N;
  }
}

// Bring up the controller, security (LESC Just Works + bonding), the two services,
// and start advertising as Duta-XXXX.
inline void init(skrit_dev *dev, void (*data_sink)(const uint8_t *, uint16_t)) {
  g_dev = dev;
  g_data_sink = data_sink;

  // Name Duta-XXXX from the BLE MAC (set before init so it lands in the GAP name).
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  char name[16];
  snprintf(name, sizeof name, "Duta-%02X%02X", mac[4], mac[5]);
  BLEDevice::init(name);
  BLEDevice::setMTU(247); // request a large ATT MTU; the peer negotiates down if needed

  // LE Secure Connections, bondable, no IO → Just Works. Encrypted + persisted keys.
  // (set_security_param copies the value synchronously, so locals are fine.)
  uint8_t auth = ESP_LE_AUTH_REQ_SC_BOND;
  uint8_t iocap = ESP_IO_CAP_NONE;
  uint8_t keysize = 16;
  uint8_t keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, sizeof auth);
  esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof iocap);
  esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &keysize, sizeof keysize);
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &keys, sizeof keys);
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &keys, sizeof keys);

  g_server = BLEDevice::createServer();
  g_server->setCallbacks(new ServerCb());

  const uint32_t enc = ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED;

  BLEService *data = g_server->createService(DUTA_BLE_DATA_SVC);
  g_data_tx = data->createCharacteristic(DUTA_BLE_DATA_TX, BLECharacteristic::PROPERTY_NOTIFY);
  g_data_tx->addDescriptor(new BLE2902());
  BLECharacteristic *data_rx = data->createCharacteristic(
      DUTA_BLE_DATA_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  data_rx->setAccessPermissions(enc);
  data_rx->setCallbacks(new DataRxCb());
  data->start();

  BLEService *cmd = g_server->createService(DUTA_BLE_CMD_SVC);
  g_cmd_tx = cmd->createCharacteristic(DUTA_BLE_CMD_TX, BLECharacteristic::PROPERTY_NOTIFY);
  g_cmd_tx->addDescriptor(new BLE2902());
  BLECharacteristic *cmd_rx = cmd->createCharacteristic(
      DUTA_BLE_CMD_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  cmd_rx->setAccessPermissions(enc);
  cmd_rx->setCallbacks(new CmdRxCb());
  cmd->start();

  start_advertising();
}

inline void loop() {
  drain(g_cmd_q, g_cmd_tx);
  drain(g_data_q, g_data_tx);
}

inline bool connected() { return g_connected; }

} // namespace duta_ble

#endif // DUTA_BLE_TRANSPORT
#endif // DUTA_BLE_H
