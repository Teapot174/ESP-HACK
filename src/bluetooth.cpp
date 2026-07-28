#include "display.h"
#include <GyverButton.h>
#include "CONFIG.h"
#include "misc.h"
#include <SD.h>
#include "Explorer.h"
#include "interface.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <NimBLEServer.h>
#include <NimBLEAdvertising.h>
#include <atomic>
#include "menu/bluetooth.h"
#include "ble_spam.h"
#include "menu/subghz.h"

extern DisplayType display;
extern GButton buttonUp;
extern GButton buttonDown;
extern GButton buttonOK;
extern GButton buttonBack;
extern bool inMenu;
extern byte currentMenu;
extern byte bluetoothMenuIndex;
extern bool inBadBLE;
extern byte badBLEScriptIndex;
extern bool scriptSelected;
extern byte selectedScript;
extern void OLED_printMenu(DisplayType &display, byte menuIndex);

void displayBadKBScriptExec(DisplayType &display, const String& filename, const std::vector<String>& logs, int logTop);

static const uint8_t hidReportDescriptor[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
  0x85, 0x01,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
  0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
  0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
  0xC0
};

struct MouseReport {
  uint8_t buttons;
  int8_t x;
  int8_t y;
  int8_t wheel;
} __attribute__((packed));

static const uint8_t compositeHidReportDescriptor[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
  0x85, 0x01,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
  0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
  0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
  0xC0,
  0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
  0x85, 0x02,
  0x09, 0x01, 0xA1, 0x00,
  0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
  0x15, 0x00, 0x25, 0x01,
  0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
  0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
  0x15, 0x81, 0x25, 0x7F,
  0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
  0xC0, 0xC0
};

enum BLEHidMode {
  BLE_HID_NONE = 0,
  BLE_HID_KEYBOARD,
  BLE_HID_MOUSE
};

static const uint8_t kSharedBleAddr[6] = {0x33, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};

using BleSubscriberHandles =
    std::array<uint16_t, CONFIG_BT_NIMBLE_MAX_CONNECTIONS>;

static BleSubscriberHandles emptyBleSubscriberHandles() {
  BleSubscriberHandles handles;
  handles.fill(BLE_HS_CONN_HANDLE_NONE);
  return handles;
}

bool                  gBleInited   = false;
NimBLEServer*         gServer      = nullptr;
NimBLEHIDDevice*      gHid         = nullptr;
NimBLECharacteristic* gKeyboardInput = nullptr;
NimBLECharacteristic* gMouseInput    = nullptr;
NimBLEAdvertising*    gAdv         = nullptr;
BLEHidMode            gBleMode     = BLE_HID_NONE;
std::atomic_bool       gBleSessionActive{false};
BleSubscriberHandles  gKeyboardSubscribers = emptyBleSubscriberHandles();
BleSubscriberHandles  gMouseSubscribers = emptyBleSubscriberHandles();
portMUX_TYPE           gBleSubscriptionMux = portMUX_INITIALIZER_UNLOCKED;

static bool hasBleSubscriberUnlocked(
    const BleSubscriberHandles &subscribers,
    uint16_t handle
) {
  for (uint16_t subscriber : subscribers) {
    if (subscriber == handle) return true;
  }
  return false;
}

static bool hasBleSubscribers(const BleSubscriberHandles &subscribers) {
  bool hasSubscribers = false;

  portENTER_CRITICAL(&gBleSubscriptionMux);
  for (uint16_t subscriber : subscribers) {
    if (subscriber != BLE_HS_CONN_HANDLE_NONE) {
      hasSubscribers = true;
      break;
    }
  }
  portEXIT_CRITICAL(&gBleSubscriptionMux);

  return hasSubscribers;
}

static bool addBleSubscriber(
    BleSubscriberHandles &subscribers,
    uint16_t handle
) {
  bool added = false;

  portENTER_CRITICAL(&gBleSubscriptionMux);
  if (!hasBleSubscriberUnlocked(subscribers, handle)) {
    for (uint16_t &subscriber : subscribers) {
      if (subscriber == BLE_HS_CONN_HANDLE_NONE) {
        subscriber = handle;
        added = true;
        break;
      }
    }
  }
  portEXIT_CRITICAL(&gBleSubscriptionMux);

  return added;
}

static bool removeBleSubscriberUnlocked(
    BleSubscriberHandles &subscribers,
    uint16_t handle
) {
  bool removed = false;
  for (uint16_t &subscriber : subscribers) {
    if (subscriber == handle) {
      subscriber = BLE_HS_CONN_HANDLE_NONE;
      removed = true;
    }
  }
  return removed;
}

static bool removeBleSubscriber(
    BleSubscriberHandles &subscribers,
    uint16_t handle
) {
  portENTER_CRITICAL(&gBleSubscriptionMux);
  const bool removed = removeBleSubscriberUnlocked(subscribers, handle);
  portEXIT_CRITICAL(&gBleSubscriptionMux);

  return removed;
}

static void resetBleSubscriptions() {
  portENTER_CRITICAL(&gBleSubscriptionMux);
  gKeyboardSubscribers.fill(BLE_HS_CONN_HANDLE_NONE);
  gMouseSubscribers.fill(BLE_HS_CONN_HANDLE_NONE);
  portEXIT_CRITICAL(&gBleSubscriptionMux);
}

static void removeBleSubscriptions(uint16_t handle) {
  portENTER_CRITICAL(&gBleSubscriptionMux);
  removeBleSubscriberUnlocked(gKeyboardSubscribers, handle);
  removeBleSubscriberUnlocked(gMouseSubscribers, handle);
  portEXIT_CRITICAL(&gBleSubscriptionMux);
}

static bool hasBleConnections() {
  return gServer && gServer->getConnectedCount() > 0;
}

static void startBleAdvertisingIfCapacity() {
  if (!gBleSessionActive.load() ||
      !gServer ||
      !gAdv ||
      gAdv->isAdvertising() ||
      gServer->getConnectedCount() >= CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
    return;
  }

  if (gAdv->start()) {
    if (!gBleSessionActive.load()) {
      gAdv->stop();
      return;
    }

    Serial.printf(
        "[BLE Pair] Advertising started (%u/%u peers)\n",
        gServer->getConnectedCount(),
        CONFIG_BT_NIMBLE_MAX_CONNECTIONS
    );
  }
}

static inline void _bb_hidSend(const uint8_t rpt[8]) {
  if (!gKeyboardInput || !hasBleSubscribers(gKeyboardSubscribers)) return;
  gKeyboardInput->setValue((uint8_t*)rpt, 8);
  gKeyboardInput->notify();
  delay(12);
}
static inline void _bb_hidPress(uint8_t mod, uint8_t key) {
  uint8_t rpt[8] = {0};
  rpt[0] = mod; rpt[2] = key;
  _bb_hidSend(rpt);
}
static inline void _bb_hidRelease() {
  uint8_t rpt[8] = {0};
  _bb_hidSend(rpt);
}

struct BluetoothReleaseButtonState {
  bool wasPressed = false;
  unsigned long pressedAt = 0;
};

static bool bluetoothButtonReleasedWithin(uint8_t pin, BluetoothReleaseButtonState& state,
                                          unsigned long maxPressMs = BUTTON_RELEASE_CLICK_MS) {
  const bool pressed = digitalRead(pin) == LOW;
  const unsigned long now = millis();

  if (pressed) {
    if (!state.wasPressed) {
      state.wasPressed = true;
      state.pressedAt = now;
    }
    return false;
  }

  if (state.wasPressed) {
    const bool releasedInTime = now - state.pressedAt <= maxPressMs;
    state.wasPressed = false;
    state.pressedAt = 0;
    return releasedInTime;
  }

  return false;
}

static int badKbVisibleLogRows(DisplayType& display) {
  return (display.height() - 20) / 10;
}

static int badKbLastLogTop(const std::vector<String>& logs, DisplayType& display) {
  const int visibleRows = badKbVisibleLogRows(display);
  return logs.size() > static_cast<size_t>(visibleRows) ? logs.size() - visibleRows : 0;
}

static bool badKbIsLogNavPress(uint8_t pin, MenuButtonState& state) {
  const bool pressed = digitalRead(pin) == LOW;
  const unsigned long now = millis();
  const unsigned long initialDelayMs = 100;
  const unsigned long repeatDelayMs = 100;

  if (!pressed) {
    state.wasPressed = false;
    state.nextRepeatAt = 0;
    return false;
  }

  if (!state.wasPressed) {
    state.wasPressed = true;
    state.nextRepeatAt = now + initialDelayMs;
    return true;
  }

  if (now >= state.nextRepeatAt) {
    state.nextRepeatAt = now + repeatDelayMs;
    return true;
  }

  return false;
}

static bool badKbPumpExecInput(const String& filename, std::vector<String>& logs, DisplayType& display,
                               int& logTop, bool& followTail, BluetoothReleaseButtonState& backState,
                               MenuButtonState& upState, MenuButtonState& downState) {
  buttonUp.tick();
  buttonDown.tick();
  buttonOK.tick();
  buttonBack.tick();

  if (badKbIsLogNavPress(BUTTON_UP, upState) && logTop > 0) {
    logTop--;
    followTail = false;
    displayBadKBScriptExec(display, filename, logs, logTop);
  }
  if (badKbIsLogNavPress(BUTTON_DOWN, downState)) {
    const int lastTop = badKbLastLogTop(logs, display);
    if (logTop < lastTop) {
      logTop++;
      followTail = logTop >= lastTop;
      displayBadKBScriptExec(display, filename, logs, logTop);
    } else {
      followTail = true;
    }
  }
  if (bluetoothButtonReleasedWithin(BUTTON_BACK, backState)) {
    _bb_hidRelease();
    return false;
  }
  return true;
}

static bool badKbResponsiveDelay(unsigned long delayMs, const String& filename, std::vector<String>& logs,
                                 DisplayType& display, int& logTop, bool& followTail,
                                 BluetoothReleaseButtonState& backState,
                                 MenuButtonState& upState, MenuButtonState& downState) {
  const unsigned long startedAt = millis();
  while (millis() - startedAt < delayMs) {
    if (!badKbPumpExecInput(filename, logs, display, logTop, followTail, backState, upState, downState)) {
      return false;
    }
    delay(1);
  }
  return true;
}

static void badKbAppendExecLog(const String& filename, std::vector<String>& logs, DisplayType& display,
                               int& logTop, bool& followTail, const String& line) {
  logs.push_back(line);
  if (followTail) {
    logTop = badKbLastLogTop(logs, display);
  }
  displayBadKBScriptExec(display, filename, logs, logTop);
}

static inline void _bm_sendMouseReport(int8_t x, int8_t y, uint8_t buttons = 0) {
  if (!gMouseInput || !hasBleSubscribers(gMouseSubscribers)) return;
  MouseReport report = {buttons, x, y, 0};
  gMouseInput->setValue(reinterpret_cast<uint8_t*>(&report), sizeof(report));
  gMouseInput->notify();
  delay(10);
}

// BadUSB simbols
static bool _bb_asciiToHID(char c, uint8_t &key, uint8_t &mod) {
  mod = 0x00;

  if (c >= 'a' && c <= 'z') { key = 0x04 + (c - 'a'); return true; }
  if (c >= 'A' && c <= 'Z') { key = 0x04 + (c - 'A'); mod = 0x02; return true; } // Shift

  if (c >= '1' && c <= '9') { key = 0x1E + (c - '1'); return true; }
  if (c == '0') { key = 0x27; return true; }

  if (c == ' ') { key = 0x2C; return true; }
  if (c == '\n' || c == '\r') { key = 0x28; return true; }
  if (c == '\t') { key = 0x2B; return true; }

  if (c == '!') { key = 0x1E; mod = 0x02; return true; } // Shift+1
  if (c == '@') { key = 0x1F; mod = 0x02; return true; } // Shift+2
  if (c == '#') { key = 0x20; mod = 0x02; return true; } // Shift+3
  if (c == '$') { key = 0x21; mod = 0x02; return true; } // Shift+4
  if (c == '%') { key = 0x22; mod = 0x02; return true; } // Shift+5
  if (c == '^') { key = 0x23; mod = 0x02; return true; } // Shift+6
  if (c == '&') { key = 0x24; mod = 0x02; return true; } // Shift+7
  if (c == '*') { key = 0x25; mod = 0x02; return true; } // Shift+8
  if (c == '(') { key = 0x26; mod = 0x02; return true; } // Shift+9
  if (c == ')') { key = 0x27; mod = 0x02; return true; } // Shift+0

  if (c == '-') { key = 0x2D; return true; }
  if (c == '_') { key = 0x2D; mod = 0x02; return true; }

  if (c == '=') { key = 0x2E; return true; }
  if (c == '+') { key = 0x2E; mod = 0x02; return true; }

  if (c == '[') { key = 0x2F; return true; }
  if (c == '{') { key = 0x2F; mod = 0x02; return true; }

  if (c == ']') { key = 0x30; return true; }
  if (c == '}') { key = 0x30; mod = 0x02; return true; }

  if (c == '\\') { key = 0x31; return true; }
  if (c == '|') { key = 0x31; mod = 0x02; return true; }

  if (c == ';') { key = 0x33; return true; }
  if (c == ':') { key = 0x33; mod = 0x02; return true; }

  if (c == '\'') { key = 0x34; return true; }
  if (c == '\"') { key = 0x34; mod = 0x02; return true; }

  if (c == '`') { key = 0x35; return true; }
  if (c == '~') { key = 0x35; mod = 0x02; return true; }

  if (c == ',') { key = 0x36; return true; }
  if (c == '<') { key = 0x36; mod = 0x02; return true; }

  if (c == '.') { key = 0x37; return true; }
  if (c == '>') { key = 0x37; mod = 0x02; return true; }

  if (c == '/') { key = 0x38; return true; }
  if (c == '?') { key = 0x38; mod = 0x02; return true; }

  return false;
}

static bool _bb_sendText(const String& s, const String& filename, std::vector<String>& logs, DisplayType& display,
                         int& logTop, bool& followTail, BluetoothReleaseButtonState& backState,
                         MenuButtonState& upState, MenuButtonState& downState) {
  for (size_t i = 0; i < s.length(); ++i) {
    uint8_t key, mod;
    if (!_bb_asciiToHID(s[i], key, mod)) continue;
    _bb_hidPress(mod, key);
    if (!badKbResponsiveDelay(8, filename, logs, display, logTop, followTail, backState, upState, downState)) return false;
    _bb_hidRelease();
    if (!badKbResponsiveDelay(8, filename, logs, display, logTop, followTail, backState, upState, downState)) return false;
  }
  return true;
}

// DuckyScript parser
static int _bb_parseDelay(const String& s) {
  long v = s.toInt();
  if (v < 0) v = 0;
  if (v > 60000) v = 60000;
  return (int)v;
}

static bool _bb_runDuckyScript(const char* filename, std::vector<String>& logs, DisplayType &display,
                               int& logTop, bool& followTail, BluetoothReleaseButtonState& backState,
                               MenuButtonState& upState, MenuButtonState& downState,
                               bool& aborted) {
  String filenameString(filename);
  aborted = false;
  if (!gServer || !gKeyboardInput) {
    badKbAppendExecLog(filenameString, logs, display, logTop, followTail, "BLE not initialized");
    Serial.println("[BadKB] BLE not initialized");
    return false;
  }
  if (!hasBleSubscribers(gKeyboardSubscribers)) {
    badKbAppendExecLog(filenameString, logs, display, logTop, followTail, "No BLE keyboard");
    Serial.println("[BadKB] No BLE keyboard subscription");
    return false;
  }

  File file = SD.open(filename, FILE_READ);
  if (!file) {
    badKbAppendExecLog(filenameString, logs, display, logTop, followTail, "File not found");
    Serial.printf("[BadKB] Failed to open file: %s\n", filename);
    return false;
  }

  while (file.available()) {
    if (!badKbPumpExecInput(filenameString, logs, display, logTop, followTail, backState, upState, downState)) {
      aborted = true;
      file.close();
      return false;
    }

    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    badKbAppendExecLog(filenameString, logs, display, logTop, followTail, line);
    Serial.printf("[BadKB] Executing: %s\n", line.c_str());

    if (line.startsWith("REM")) { continue; }

    if (line.startsWith("DELAY ")) {
      int ms = _bb_parseDelay(line.substring(6));
      Serial.printf("[BadKB] Delaying %d ms\n", ms);
      if (!badKbResponsiveDelay(ms, filenameString, logs, display, logTop, followTail, backState, upState, downState)) {
        aborted = true;
        file.close();
        return false;
      }
      continue;
    }

    if (line.equalsIgnoreCase("ENTER")) {
      Serial.println("[BadKB] Sending ENTER");
      _bb_hidPress(0x00, 0x28);
      if (!badKbResponsiveDelay(25, filenameString, logs, display, logTop, followTail, backState, upState, downState)) {
        aborted = true;
        file.close();
        return false;
      }
      _bb_hidRelease();
      if (!badKbResponsiveDelay(6, filenameString, logs, display, logTop, followTail, backState, upState, downState)) {
        aborted = true;
        file.close();
        return false;
      }
      continue;
    }

    if (line.equalsIgnoreCase("GUI R") || line.equalsIgnoreCase("GUI r") || line.equalsIgnoreCase("WINR")) {
      Serial.println("[BadKB] Sending GUI+R");
      _bb_hidPress(0x08, 0x15);
      if (!badKbResponsiveDelay(30, filenameString, logs, display, logTop, followTail, backState, upState, downState)) {
        aborted = true;
        file.close();
        return false;
      }
      _bb_hidRelease();
      if (!badKbResponsiveDelay(60, filenameString, logs, display, logTop, followTail, backState, upState, downState)) {
        aborted = true;
        file.close();
        return false;
      }
      continue;
    }

    if (line.startsWith("STRING ")) {
      String text = line.substring(7);
      Serial.printf("[BadKB] Sending STRING: %s\n", text.c_str());
      if (!_bb_sendText(text, filenameString, logs, display, logTop, followTail, backState, upState, downState)) {
        aborted = true;
        file.close();
        return false;
      }
      continue;
    }

    Serial.printf("[BadKB] Skipping unknown command: %s\n", line.c_str());
  }

  file.close();
  Serial.println("[BadKB] Script execution completed");
  badKbAppendExecLog(filenameString, logs, display, logTop, followTail, "Done");
  return true;
}

class HidInputCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(
      NimBLECharacteristic *characteristic,
      NimBLEConnInfo &connInfo,
      uint16_t subValue
  ) override {
    const bool subscribedToNotifications = (subValue & 0x01) != 0;
    const uint16_t handle = connInfo.getConnHandle();

    if (characteristic == gKeyboardInput) {
      if (subscribedToNotifications) {
        if (addBleSubscriber(gKeyboardSubscribers, handle)) {
          Serial.printf(
              "[BLE HID] Keyboard subscribed (handle=%u)\n",
              handle
          );
        }
      } else if (removeBleSubscriber(gKeyboardSubscribers, handle)) {
        Serial.printf(
            "[BLE HID] Keyboard unsubscribed (handle=%u)\n",
            handle
        );
      }
    } else if (characteristic == gMouseInput) {
      if (subscribedToNotifications) {
        if (addBleSubscriber(gMouseSubscribers, handle)) {
          Serial.printf(
              "[BLE HID] Mouse subscribed (handle=%u)\n",
              handle
          );
        }
      } else if (removeBleSubscriber(gMouseSubscribers, handle)) {
        Serial.printf(
            "[BLE HID] Mouse unsubscribed (handle=%u)\n",
            handle
        );
      }
    }
  }
};

class PairServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    const uint16_t handle = connInfo.getConnHandle();
    pServer->updateConnParams(handle, 6, 6, 0, 30);
    Serial.printf("[BLE Pair] Connected (handle=%u)\n", handle);
    startBleAdvertisingIfCapacity();
  }
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    const uint16_t handle = connInfo.getConnHandle();
    Serial.printf(
        "[BLE Pair] Disconnected (handle=%u, reason=%d)\n",
        handle,
        reason
    );
    removeBleSubscriptions(handle);
    startBleAdvertisingIfCapacity();
  }
};

static void stopBLE();
static void pauseBLE();

static void resetBleRuntimeState() {
  gBleInited = false;
  gServer = nullptr;
  gHid = nullptr;
  gKeyboardInput = nullptr;
  gMouseInput = nullptr;
  gAdv = nullptr;
  gBleMode = BLE_HID_NONE;
  gBleSessionActive.store(false);
  resetBleSubscriptions();
}

static void disconnectAllBlePeers() {
  if (!gServer) return;

  const std::vector<uint16_t> peerHandles = gServer->getPeerDevices();
  for (uint16_t handle : peerHandles) {
    gServer->disconnect(handle);
    delay(35);
  }
}

static void pauseBLE() {
  gBleSessionActive.store(false);
  if (!gBleInited) return;

  if (gAdv && gAdv->isAdvertising()) {
    gAdv->stop();
    delay(20);
  }

  disconnectAllBlePeers();
  resetBleSubscriptions();
}

static void ensureBleHidInited(BLEHidMode mode) {
  (void)mode;
  gBleSessionActive.store(true);
  if (gBleInited) return;

  Serial.println("[BLE Pair] Initializing BLE...");
  NimBLEDevice::init(bleDeviceName);
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  NimBLEDevice::setOwnAddr(kSharedBleAddr);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(true, false, true);

  gServer = NimBLEDevice::createServer();
  static PairServerCallbacks sCallbacks;
  gServer->setCallbacks(&sCallbacks);

  gHid = new NimBLEHIDDevice(gServer);
  gKeyboardInput = gHid->getInputReport(1);
  gMouseInput = gHid->getInputReport(2);
  static HidInputCallbacks sHidInputCallbacks;
  gKeyboardInput->setCallbacks(&sHidInputCallbacks);
  gMouseInput->setCallbacks(&sHidInputCallbacks);

  gHid->setManufacturer("ESP-HACK");
  gHid->setPnp(0x02, 0x303A, 0x4001, 0x0100);
  gHid->setHidInfo(0x00, 0x01);
  gHid->setReportMap((uint8_t*)compositeHidReportDescriptor, sizeof(compositeHidReportDescriptor));
  gHid->setBatteryLevel(100);

  gAdv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  NimBLEAdvertisementData scanData;

  advData.setFlags(0x06);
  advData.setAppearance(0x03C0);
  advData.addServiceUUID(gHid->getHidService()->getUUID());
  scanData.setName(bleDeviceName);

  gAdv->setAdvertisementData(advData);
  gAdv->setScanResponseData(scanData);
  gAdv->setMinInterval(32);
  gAdv->setMaxInterval(64);
  gAdv->enableScanResponse(true);

  gBleInited = true;
  gBleMode = BLE_HID_KEYBOARD;
  resetBleSubscriptions();
  Serial.println("[BLE Pair] BLE/HID initialized");
}

static void stopBLE() {
  gBleSessionActive.store(false);

  if (!gBleInited && !gServer && !gHid && !gAdv) {
    resetBleRuntimeState();
    return;
  }

  if (gAdv && gAdv->isAdvertising()) {
    gAdv->stop();
    Serial.println("[BLE Pair] Advertising stopped");
    delay(30);
  }

  disconnectAllBlePeers();

  if (gHid) {
    delete gHid;
    gHid = nullptr;
    gKeyboardInput = nullptr;
    gMouseInput = nullptr;
    delay(20);
  }

  if (gBleInited) {
    NimBLEDevice::deinit(true);
    delay(120);
  }

  resetBleRuntimeState();
  Serial.println("[BLE Pair] BLE deinitialized");
}

#define MAX_FILES 50
static const char* badKbExts[] = {".txt"};
ExplorerEntry badKBFileList[MAX_FILES];
ExplorerState badKBExplorer;
ExplorerConfig badKBExplorerCfg = {"/badkb", badKbExts, 1, true, false, true, true};

static void loadBadKBFileList() {
  if (badKBExplorer.currentDir.length() == 0) {
    badKBExplorer.currentDir = badKBExplorerCfg.rootDir;
  }
  ExplorerLoad(badKBExplorer, badKBExplorerCfg);
}

void drawBadKBExplorer(DisplayType &display) {
  ExplorerDraw(badKBExplorer, display);
}

void displayBadKBScriptExec(DisplayType &display, const String& filename, const std::vector<String>& logs, int logTop) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(3, 3);
  display.println(filename);
  display.println(F("---------------------"));

  int maxLogs = (display.height() - 20) / 10;
  for (int i = 0; i < maxLogs; i++) {
    int idx = logTop + i;
    if (idx >= (int)logs.size()) break;
    display.setCursor(1, 20 + i * 10);
    display.print(logs[idx]);
  }
  display.display();
}

enum BLESpamState { IDLE, READY, RUNNING };
BLESpamState bleSpamState = IDLE;
EBLEPayloadType currentSpamType;
bool inSpamMenu = false;
byte spamMenuIndex = 0;
uint16_t spamIntervalMs = 100;
static const byte SPAM_MENU_ITEM_COUNT = 5;
static const char* spamMenuItems[] = {"Apple", "Android", "Samsung", "Xiaomi", "Windows"};
const byte BLE_SPAM_LOG_LINES = 5;
String bleSpamLog[BLE_SPAM_LOG_LINES];
byte bleSpamLogSize = 0;

String generateRandomName() {
  const char* charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  int len = random(1, 11);
  String randomName = "";
  for (int i = 0; i < len; ++i) {
    randomName += charset[random(0, strlen(charset))];
  }
  return randomName;
}

const char* getBLESpamHeaderName(const char* spamType) {
  return spamType;
}

void displaySpamMenu(byte menuIndex, int previousIndex = -1) {
  displayInterfaceSubmenu(display, spamMenuItems, SPAM_MENU_ITEM_COUNT, menuIndex, previousIndex);
}

void clearBLESpamLog() {
  bleSpamLogSize = 0;
  for (byte i = 0; i < BLE_SPAM_LOG_LINES; i++) bleSpamLog[i] = "";
}

void appendBLESpamLine(const String &line) {
  if (bleSpamLogSize < BLE_SPAM_LOG_LINES) {
    bleSpamLog[bleSpamLogSize++] = line;
  } else {
    for (byte i = 1; i < BLE_SPAM_LOG_LINES; i++) {
      bleSpamLog[i - 1] = bleSpamLog[i];
    }
    bleSpamLog[BLE_SPAM_LOG_LINES - 1] = line;
  }
}

void pushBLESpamLog(const String &deviceName) {
  if (deviceName.length() == 0) {
    appendBLESpamLine("");
    return;
  }
  appendBLESpamLine(deviceName);
}

void displayBLESpamHeader(const char* spamType, bool running) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(3, 3);
  if (running) {
    display.println(String(getBLESpamHeaderName(spamType)) + "...");
    display.println(F("---------------------"));
  } else {
    display.println(F("Press OK."));
    display.println(F("---------------------"));
  }
  const int16_t contentY = display.getCursorY();
  display.setCursor(96, 3);
  display.print(spamIntervalMs);
  display.print(F("ms"));
  display.setCursor(0, contentY);
}

void displayBLESpamDevice(const char* deviceName) {
  pushBLESpamLog(deviceName);
  displayBLESpamHeader(spamMenuItems[spamMenuIndex], true);
  int16_t startY = display.getCursorY();
  for (byte i = 0; i < bleSpamLogSize; i++) {
    display.setCursor(1, startY + i * 9);
    display.println(bleSpamLog[i]);
  }
  display.display();
}

void displayFullBLESpamScreen(const char* spamType, bool running, const char* deviceName = "") {
  displayBLESpamHeader(spamType, running);
  if (running) {
    if (strlen(deviceName) > 0) {
      pushBLESpamLog(deviceName);
    }
    int16_t startY = display.getCursorY();
    for (byte i = 0; i < bleSpamLogSize; i++) {
      display.setCursor(1, startY + i * 9);
      display.println(bleSpamLog[i]);
    }
  }
  display.display();
}

static const uint8_t mousePowerValues[] = {25, 50, 75, 100};
static const uint8_t mouseSpeedValues[] = {25, 50, 75, 100};
static const char* mouseModeLabels[] = {"Circle", "Square", "Up", "Down", "Left", "Right"};
static const uint8_t mouseModeCount = sizeof(mouseModeLabels) / sizeof(mouseModeLabels[0]);

static void displayMousePairScreen(bool connected, bool ready, const char* errorText = nullptr) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(3, 3);
  display.println(F("Mouse"));
  display.setCursor(1, 10);
  display.println(F("---------------------"));

  display.setCursor(1, 18);
  if (!connected) display.print(F("Waiting for Pair..."));
  else if (!ready) display.print(F("Connecting..."));
  else display.print(F("Connected"));

  if (errorText && errorText[0] != '\0') {
    display.setCursor(1, 26);
    display.print(errorText);
  }

  display.display();
}

static int16_t getMouseConfigArrowY(uint8_t selection) {
  switch (selection) {
    case 0: return 22;
    case 1: return 30;
    case 2: return 38;
    default: return 50;
  }
}

static void drawMouseConfigFrame(uint8_t selection, uint8_t powerIndex, uint8_t speedIndex,
                                 uint8_t modeIndex, bool running, const char* errorText,
                                 int16_t arrowY = -1) {
  const int valueX = 54;
  const int labelX = 16;
  if (arrowY < 0) arrowY = getMouseConfigArrowY(selection);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(3, 3);
  display.println(F("Mouse"));
  display.setCursor(1, 10);
  display.println(F("---------------------"));

  if (errorText && errorText[0] != '\0') {
    display.setCursor(1, 18);
    display.print(errorText);
  }

  display.setCursor(labelX, 22);
  display.print(F("Power:"));
  display.setCursor(valueX, 22);
  display.print(mousePowerValues[powerIndex]);
  display.println(F("%"));

  display.setCursor(labelX, 30);
  display.print(F("Speed:"));
  display.setCursor(valueX, 30);
  display.print(mouseSpeedValues[speedIndex]);
  display.println(F("%"));

  display.setCursor(labelX, 38);
  display.print(F("Mode: "));
  display.println(mouseModeLabels[modeIndex]);

  display.setCursor(labelX, 50);
  display.println(running ? F("Stop") : F("Start"));
  display.setCursor(6, arrowY);
  display.print(F(">"));
  display.display();
}

static void displayMouseConfigScreen(uint8_t selection, uint8_t powerIndex, uint8_t speedIndex,
                                     uint8_t modeIndex, bool running, const char* errorText = nullptr,
                                     int previousSelection = -1) {
  if (previousSelection < 0 || previousSelection == selection) {
    drawMouseConfigFrame(selection, powerIndex, speedIndex, modeIndex, running, errorText);
    return;
  }

  int16_t fromY = getMouseConfigArrowY(previousSelection);
  int16_t toY = getMouseConfigArrowY(selection);
  const byte steps = 4;
  for (byte step = 1; step <= steps; step++) {
    int progress = (step * 100) / steps;
    int eased = progress < 50
      ? (2 * progress * progress) / 100
      : 100 - (2 * (100 - progress) * (100 - progress)) / 100;
    int16_t arrowY = fromY + ((toY - fromY) * eased) / 100;
    drawMouseConfigFrame(selection, powerIndex, speedIndex, modeIndex, running, errorText, arrowY);
    delay(1);
  }
  drawMouseConfigFrame(selection, powerIndex, speedIndex, modeIndex, running, errorText, toY);
}

static void resetMouseMotionState(float& phase, float& prevX, float& prevY, unsigned long& lastStepAt) {
  phase = 0.0f;
  prevX = 0.0f;
  prevY = 0.0f;
  lastStepAt = 0;
}

static void resetButtonStates() {
  buttonUp.resetStates();
  buttonDown.resetStates();
  buttonOK.resetStates();
  buttonBack.resetStates();
}

static void resetMenuButtonState(MenuButtonState& state) {
  state.wasPressed = false;
  state.nextRepeatAt = 0;
}

void handleBluetoothSubmenu() {
  buttonUp.tick();
  buttonDown.tick();
  buttonOK.tick();
  buttonBack.tick();

  static bool explorerLoaded = false;
  static std::vector<String> execLogs;
  static int execLogTop = 0;
  static bool execFollowTail = true;
  static bool scriptRunning = false;
  static bool scriptDone = false;
  static String selectedFile = "";
  static bool waitingForConnection = false;
  static uint32_t waitStartTime = 0;
  static bool justEnteredBadBLE = false;
  static BluetoothReleaseButtonState badBleBackReleaseState;
  static MenuButtonState badBleLogUpState;
  static MenuButtonState badBleLogDownState;
  static bool inMouseMenu = false;
  static bool mouseRunning = false;
  static uint8_t mouseSelection = 0;
  static uint8_t mousePowerIndex = 0;
  static uint8_t mouseSpeedIndex = 2;
  static uint8_t mouseModeIndex = 0;
  static unsigned long mouseLastStep = 0;
  static float mousePhase = 0.0f;
  static float mousePrevX = 0.0f;
  static float mousePrevY = 0.0f;
  static bool lastMouseConnected = false;
  static bool lastMouseReady = false;
  static bool mousePairingStarted = false;
  static bool mouseIgnoreButtonsUntilRelease = false;
  bool scriptWasRunningThisPass = false;

  auto resetBadBleInputStates = [&]() {
    badBleBackReleaseState = {};
    resetMenuButtonState(badBleLogUpState);
    resetMenuButtonState(badBleLogDownState);
    resetButtonStates();
  };

  if (inBadBLE) {
    if (scriptSelected) {
      scriptWasRunningThisPass = scriptRunning;
      if (scriptRunning) {
        ensureBleHidInited(BLE_HID_KEYBOARD);
        startBleAdvertisingIfCapacity();

        if (!waitingForConnection) {
          waitingForConnection = true;
          waitStartTime = millis();
          execLogs.clear();
          execLogTop = 0;
          execFollowTail = true;
          badKbAppendExecLog(selectedFile, execLogs, display, execLogTop, execFollowTail, "Waiting for BLE...");
        }

        const uint32_t connectionTimeout = 30000;
        if (!hasBleSubscribers(gKeyboardSubscribers) &&
            !badKbPumpExecInput(selectedFile, execLogs, display, execLogTop,
                                execFollowTail, badBleBackReleaseState,
                                badBleLogUpState, badBleLogDownState)) {
          scriptSelected = false;
          scriptRunning = false;
          scriptDone = false;
          waitingForConnection = false;
          waitStartTime = 0;
          execLogs.clear();
          execLogTop = 0;
          execFollowTail = true;
          resetBadBleInputStates();
          if (gAdv && gAdv->isAdvertising()) {
            gAdv->stop();
          }
          ExplorerInit(badKBExplorer, badKBFileList, MAX_FILES, badKBExplorerCfg);
          loadBadKBFileList();
          explorerLoaded = true;
          drawBadKBExplorer(display);
          return;
        }
        if (hasBleSubscribers(gKeyboardSubscribers)) {
          waitingForConnection = false;
          waitStartTime = 0;
          if (!badKbResponsiveDelay(3000, selectedFile, execLogs, display, execLogTop,
                                    execFollowTail, badBleBackReleaseState,
                                    badBleLogUpState, badBleLogDownState)) {
            scriptSelected = false;
            scriptRunning = false;
            scriptDone = false;
            waitingForConnection = false;
            waitStartTime = 0;
            execLogs.clear();
            execLogTop = 0;
            execFollowTail = true;
            resetBadBleInputStates();
            if (gAdv && gAdv->isAdvertising()) {
              gAdv->stop();
            }
            ExplorerInit(badKBExplorer, badKBFileList, MAX_FILES, badKBExplorerCfg);
            loadBadKBFileList();
            explorerLoaded = true;
            drawBadKBExplorer(display);
            return;
          }
          _bb_hidRelease();
          if (!badKbResponsiveDelay(40, selectedFile, execLogs, display, execLogTop,
                                    execFollowTail, badBleBackReleaseState,
                                    badBleLogUpState, badBleLogDownState)) {
            scriptSelected = false;
            scriptRunning = false;
            scriptDone = false;
            waitingForConnection = false;
            waitStartTime = 0;
            execLogs.clear();
            execLogTop = 0;
            execFollowTail = true;
            resetBadBleInputStates();
            ExplorerInit(badKBExplorer, badKBFileList, MAX_FILES, badKBExplorerCfg);
            loadBadKBFileList();
            explorerLoaded = true;
            drawBadKBExplorer(display);
            return;
          }
          execLogs.clear();
          execLogTop = 0;
          execFollowTail = true;
          bool scriptAborted = false;
          bool ok = _bb_runDuckyScript(selectedFile.c_str(), execLogs, display, execLogTop,
                                       execFollowTail, badBleBackReleaseState,
                                       badBleLogUpState, badBleLogDownState, scriptAborted);
          if (scriptAborted) {
            scriptSelected = false;
            scriptRunning = false;
            scriptDone = false;
            waitingForConnection = false;
            waitStartTime = 0;
            execLogs.clear();
            execLogTop = 0;
            execFollowTail = true;
            resetBadBleInputStates();
            ExplorerInit(badKBExplorer, badKBFileList, MAX_FILES, badKBExplorerCfg);
            loadBadKBFileList();
            explorerLoaded = true;
            drawBadKBExplorer(display);
            return;
          }
          scriptRunning = false;
          scriptDone = true;
          if (!ok) {
            badKbAppendExecLog(selectedFile, execLogs, display, execLogTop, execFollowTail, "Failed");
          }
          displayBadKBScriptExec(display, selectedFile, execLogs, execLogTop);
          resetBadBleInputStates();
        } else if (millis() - waitStartTime >= connectionTimeout) {
          waitingForConnection = false;
          waitStartTime = 0;
          scriptRunning = false;
          scriptDone = true;
          execLogs.clear();
          execLogTop = 0;
          execFollowTail = true;
          badKbAppendExecLog(selectedFile, execLogs, display, execLogTop, execFollowTail, "Connection timeout");
          resetBadBleInputStates();
          Serial.println("[BadKB] Connection timeout");
        }
      } else {
        displayBadKBScriptExec(display, selectedFile, execLogs, execLogTop);
      }
    } else {
      if (!explorerLoaded) {
        ExplorerInit(badKBExplorer, badKBFileList, MAX_FILES, badKBExplorerCfg);
        loadBadKBFileList();
        explorerLoaded = true;
        justEnteredBadBLE = false;
        drawBadKBExplorer(display);
      }
    }
  } else if (inMouseMenu) {
    if (!mousePairingStarted) {
      ensureBleHidInited(BLE_HID_MOUSE);
      startBleAdvertisingIfCapacity();
      mousePairingStarted = true;
    }
    if (mouseRunning) {
      ensureBleHidInited(BLE_HID_MOUSE);
      startBleAdvertisingIfCapacity();
    }
  } else {
    if (inSpamMenu && bleSpamState == IDLE) {
      displaySpamMenu(spamMenuIndex);
    } else if (inSpamMenu && bleSpamState == READY) {
      displayFullBLESpamScreen(spamMenuItems[spamMenuIndex], false);
    } else if (bleSpamState == IDLE) {
      displayBluetoothMenu(display, bluetoothMenuIndex);
    } else if (bleSpamState == READY) {
      displayFullBLESpamScreen(spamMenuItems[spamMenuIndex], false);
    }
  }

  if (inBadBLE) {
    if (scriptSelected) {
      if (badKbIsLogNavPress(BUTTON_UP, badBleLogUpState) && execLogTop > 0) {
        execLogTop--;
        execFollowTail = false;
        displayBadKBScriptExec(display, selectedFile, execLogs, execLogTop);
      }
      bool downLogClick = badKbIsLogNavPress(BUTTON_DOWN, badBleLogDownState);
      if (downLogClick && execLogTop + ((display.height() - 20) / 10) < (int)execLogs.size()) {
        execLogTop++;
        execFollowTail = execLogTop >= badKbLastLogTop(execLogs, display);
        displayBadKBScriptExec(display, selectedFile, execLogs, execLogTop);
      } else if (downLogClick) {
        execFollowTail = true;
      }
      if (bluetoothButtonReleasedWithin(BUTTON_BACK, badBleBackReleaseState)) {
        scriptSelected = false;
        scriptRunning = false;
        scriptDone = false;
        waitingForConnection = false;
        waitStartTime = 0;
        execLogs.clear();
        execLogTop = 0;
        execFollowTail = true;
        resetBadBleInputStates();
        if (gAdv && gAdv->isAdvertising()) {
          gAdv->stop();
        }
        ExplorerInit(badKBExplorer, badKBFileList, MAX_FILES, badKBExplorerCfg);
        loadBadKBFileList();
        explorerLoaded = true;
        drawBadKBExplorer(display);
        return;
      }
      if (!scriptWasRunningThisPass && buttonOK.isClick() && scriptDone) {
        scriptRunning = true;
        scriptDone = false;
        waitingForConnection = false;
        waitStartTime = 0;
        execLogTop = 0;
        execFollowTail = true;
      }
    } else {
      bool backRelease = bluetoothButtonReleasedWithin(BUTTON_BACK, badBleBackReleaseState);
      if (justEnteredBadBLE) backRelease = false;
      ExplorerAction action = ExplorerHandle(
        badKBExplorer,
        badKBExplorerCfg,
        display,
        buttonUp.isClick(),
        buttonDown.isClick(),
        buttonOK.isClick(),
        backRelease,
        buttonBack.isHold()
      );
      if (action == EXPLORER_SELECT_FILE) {
        selectedFile = badKBExplorer.currentDir + "/" + badKBExplorer.selectedFile;
        scriptSelected = true;
        scriptRunning = true;
        scriptDone = false;
        waitingForConnection = false;
        waitStartTime = 0;
        execLogs.clear();
        execLogTop = 0;
        execFollowTail = true;
        ensureBleHidInited(BLE_HID_KEYBOARD);
        displayBadKBScriptExec(display, selectedFile, execLogs, execLogTop);
        Serial.println(F("[BadKB] Selected BadKB script"));
      } else if (action == EXPLORER_EXIT) {
        inBadBLE = false;
        explorerLoaded = false;
        justEnteredBadBLE = false;
        scriptSelected = false;
        scriptRunning = false;
        scriptDone = false;
        waitingForConnection = false;
        waitStartTime = 0;
        execLogs.clear();
        execLogTop = 0;
        execFollowTail = true;
        resetBadBleInputStates();
        pauseBLE();
        display.clearDisplay();
        displayBluetoothMenu(display, bluetoothMenuIndex);
        display.display();
        Serial.println(F("[BadKB] Back to Bluetooth menu from BadKB"));
      }
      if (justEnteredBadBLE) justEnteredBadBLE = false;
    }
  } else if (inMouseMenu) {
    static MenuButtonState mouseUpHeld;
    static MenuButtonState mouseDownHeld;
    const bool mouseConnected = hasBleConnections();
    const bool mouseReady = hasBleSubscribers(gMouseSubscribers);
    const char* mouseErrorText = (mouseRunning && gBleInited && !gMouseInput) ? "BLE error" : "";

    if (lastMouseConnected != mouseConnected || lastMouseReady != mouseReady) {
      lastMouseConnected = mouseConnected;
      lastMouseReady = mouseReady;
      if (mouseReady) {
        mouseIgnoreButtonsUntilRelease = true;
        resetButtonStates();
        resetMenuButtonState(mouseUpHeld);
        resetMenuButtonState(mouseDownHeld);
        displayMouseConfigScreen(mouseSelection, mousePowerIndex, mouseSpeedIndex, mouseModeIndex,
                                 mouseRunning, mouseErrorText);
      } else {
        mouseIgnoreButtonsUntilRelease = true;
        displayMousePairScreen(mouseConnected, mouseReady, mouseErrorText);
      }
    }

    if (!mouseReady) {
      if (buttonBack.isClick()) {
        if (mousePairingStarted || gBleInited) {
          pauseBLE();
        }
        inMouseMenu = false;
        mouseRunning = false;
        mousePairingStarted = false;
        lastMouseConnected = false;
        lastMouseReady = false;
        mouseIgnoreButtonsUntilRelease = false;
        resetMouseMotionState(mousePhase, mousePrevX, mousePrevY, mouseLastStep);
        resetButtonStates();
        resetMenuButtonState(mouseUpHeld);
        resetMenuButtonState(mouseDownHeld);
        displayBluetoothMenu(display, bluetoothMenuIndex);
        return;
      }

      resetButtonStates();
      resetMenuButtonState(mouseUpHeld);
      resetMenuButtonState(mouseDownHeld);
      return;
    }

    if (mouseIgnoreButtonsUntilRelease) {
      if (digitalRead(BUTTON_UP) == LOW || digitalRead(BUTTON_DOWN) == LOW ||
          digitalRead(BUTTON_OK) == LOW || digitalRead(BUTTON_BACK) == LOW) {
        resetButtonStates();
        resetMenuButtonState(mouseUpHeld);
        resetMenuButtonState(mouseDownHeld);
        return;
      }
      mouseIgnoreButtonsUntilRelease = false;
      resetButtonStates();
      resetMenuButtonState(mouseUpHeld);
      resetMenuButtonState(mouseDownHeld);
      return;
    }

    if (isMenuButtonPress(BUTTON_UP, mouseUpHeld)) {
      uint8_t previousSelection = mouseSelection;
      mouseSelection = (mouseSelection == 0) ? 3 : mouseSelection - 1;
      displayMouseConfigScreen(mouseSelection, mousePowerIndex, mouseSpeedIndex, mouseModeIndex,
                               mouseRunning, mouseErrorText, previousSelection);
    }
    if (isMenuButtonPress(BUTTON_DOWN, mouseDownHeld)) {
      uint8_t previousSelection = mouseSelection;
      mouseSelection = (mouseSelection + 1) % 4;
      displayMouseConfigScreen(mouseSelection, mousePowerIndex, mouseSpeedIndex, mouseModeIndex,
                               mouseRunning, mouseErrorText, previousSelection);
    }

    if (buttonOK.isClick()) {
      if (mouseSelection == 0) {
        mousePowerIndex = (mousePowerIndex + 1) % 4;
        resetMouseMotionState(mousePhase, mousePrevX, mousePrevY, mouseLastStep);
      } else if (mouseSelection == 1) {
        mouseSpeedIndex = (mouseSpeedIndex + 1) % 4;
        resetMouseMotionState(mousePhase, mousePrevX, mousePrevY, mouseLastStep);
      } else if (mouseSelection == 2) {
        mouseModeIndex = (mouseModeIndex + 1) % mouseModeCount;
        resetMouseMotionState(mousePhase, mousePrevX, mousePrevY, mouseLastStep);
      } else {
        mouseRunning = !mouseRunning;
        if (mouseRunning) {
          ensureBleHidInited(BLE_HID_MOUSE);
          startBleAdvertisingIfCapacity();
        }
        lastMouseConnected = hasBleConnections();
        lastMouseReady = false;
        resetMouseMotionState(mousePhase, mousePrevX, mousePrevY, mouseLastStep);
      }
      displayMouseConfigScreen(mouseSelection, mousePowerIndex, mouseSpeedIndex, mouseModeIndex,
                               mouseRunning, mouseErrorText);
    }

    if (buttonBack.isClick()) {
      if (mousePairingStarted || gBleInited) {
        pauseBLE();
      }
      inMouseMenu = false;
      mouseRunning = false;
      mousePairingStarted = false;
      lastMouseConnected = false;
      lastMouseReady = false;
      mouseIgnoreButtonsUntilRelease = false;
      resetMouseMotionState(mousePhase, mousePrevX, mousePrevY, mouseLastStep);
      displayBluetoothMenu(display, bluetoothMenuIndex);
      return;
    }

    if (mouseRunning && mouseReady) {
      const uint8_t power = mousePowerValues[mousePowerIndex];
      const uint8_t speed = mouseSpeedValues[mouseSpeedIndex];
      const float baseRadius = 36.0f; // Double the default coverage; 25% uses this base
      const float radius = baseRadius * (power / 25.0f);
      const unsigned long moveInterval = 44 - (speed * 9UL) / 25UL;

      if (millis() - mouseLastStep >= moveInterval) {
        int8_t dx = 0;
        int8_t dy = 0;

        if (mouseModeIndex == 0) {
          const float x = cosf(mousePhase) * radius;
          const float y = sinf(mousePhase) * radius;
          dx = (int8_t)roundf(x - mousePrevX);
          dy = (int8_t)roundf(y - mousePrevY);
          mousePrevX = x;
          mousePrevY = y;
          mousePhase += 0.24f;
          if (mousePhase >= 6.2831853f) mousePhase -= 6.2831853f;
        } else if (mouseModeIndex == 1) {
          const float edge = radius;
          const float squarePhase = fmodf(mousePhase, 4.0f);
          float x = 0.0f;
          float y = 0.0f;
          if (squarePhase < 1.0f) {
            x = -edge + (squarePhase * 2.0f * edge);
            y = -edge;
          } else if (squarePhase < 2.0f) {
            x = edge;
            y = -edge + ((squarePhase - 1.0f) * 2.0f * edge);
          } else if (squarePhase < 3.0f) {
            x = edge - ((squarePhase - 2.0f) * 2.0f * edge);
            y = edge;
          } else {
            x = -edge;
            y = edge - ((squarePhase - 3.0f) * 2.0f * edge);
          }
          dx = (int8_t)roundf(x - mousePrevX);
          dy = (int8_t)roundf(y - mousePrevY);
          mousePrevX = x;
          mousePrevY = y;
          mousePhase += 0.16f;
          if (mousePhase >= 4.0f) mousePhase -= 4.0f;
        } else {
          const int8_t step = (int8_t)roundf(radius);
          if (mouseModeIndex == 2) dy = -step;
          else if (mouseModeIndex == 3) dy = step;
          else if (mouseModeIndex == 4) dx = -step;
          else if (mouseModeIndex == 5) dx = step;
        }

        if (dx != 0 || dy != 0) {
          _bm_sendMouseReport(dx, dy, 0);
        }
        mouseLastStep = millis();
      }
    }
  } else if (inSpamMenu && bleSpamState == IDLE) {
    static MenuButtonState spamUpHeld;
    static MenuButtonState spamDownHeld;

    const unsigned long repeatDelayMs = getInterfaceSubmenuRepeatDelay(submenu == 1);
    if (isMenuButtonPress(BUTTON_UP, spamUpHeld, repeatDelayMs)) {
      byte previousIndex = spamMenuIndex;
      spamMenuIndex = (spamMenuIndex - 1 + SPAM_MENU_ITEM_COUNT) % SPAM_MENU_ITEM_COUNT;
      displaySpamMenu(spamMenuIndex, previousIndex);
    }
    if (isMenuButtonPress(BUTTON_DOWN, spamDownHeld, repeatDelayMs)) {
      byte previousIndex = spamMenuIndex;
      spamMenuIndex = (spamMenuIndex + 1) % SPAM_MENU_ITEM_COUNT;
      displaySpamMenu(spamMenuIndex, previousIndex);
    }
    if (buttonOK.isClick()) {
      switch (spamMenuIndex) {
        case 0: currentSpamType = AppleJuice; break;
        case 1: currentSpamType = Google; break;
        case 2: currentSpamType = Samsung; break;
        case 3: currentSpamType = Xiaomi; break;
        case 4: currentSpamType = Microsoft; break;
      }
      bleSpamState = READY;
      spamIntervalMs = 100;
      displayFullBLESpamScreen(spamMenuItems[spamMenuIndex], false);
      Serial.println(F("[Bluetooth] Ready for BLE spam"));
    }
    if (buttonBack.isClick()) {
      inSpamMenu = false;
      spamMenuIndex = 0;
      displayBluetoothMenu(display, bluetoothMenuIndex);
    }
  } else {
    if (bleSpamState == IDLE) {
      static MenuButtonState upHeld;
      static MenuButtonState downHeld;

      const unsigned long repeatDelayMs = getInterfaceSubmenuRepeatDelay(submenu == 1);
      if (isMenuButtonPress(BUTTON_UP, upHeld, repeatDelayMs)) {
        byte previousIndex = bluetoothMenuIndex;
        bluetoothMenuIndex = (bluetoothMenuIndex - 1 + BLUETOOTH_MENU_ITEM_COUNT) % BLUETOOTH_MENU_ITEM_COUNT;
        displayBluetoothMenu(display, bluetoothMenuIndex, previousIndex);
      }
      if (isMenuButtonPress(BUTTON_DOWN, downHeld, repeatDelayMs)) {
        byte previousIndex = bluetoothMenuIndex;
        bluetoothMenuIndex = (bluetoothMenuIndex + 1) % BLUETOOTH_MENU_ITEM_COUNT;
        displayBluetoothMenu(display, bluetoothMenuIndex, previousIndex);
      }
      if (buttonOK.isClick()) {
        if (bluetoothMenuIndex == 1) {
          if (!ensureSDReadyInteractive(true)) {
            displayBluetoothMenu(display, bluetoothMenuIndex);
            return;
          }
          inBadBLE = true;
          inMouseMenu = false;
          explorerLoaded = false;
          justEnteredBadBLE = true;
          scriptSelected = false;
          scriptRunning = false;
          scriptDone = false;
          waitingForConnection = false;
          execLogs.clear();
          execLogTop = 0;
          execFollowTail = true;
          resetBadBleInputStates();
          waitStartTime = 0;
          ensureBleHidInited(BLE_HID_KEYBOARD);
          Serial.println(F("[BadKB] Entered BadKB"));
        } else if (bluetoothMenuIndex == 2) {
          inMouseMenu = true;
          inBadBLE = false;
          explorerLoaded = false;
          mouseRunning = false;
          mouseSelection = 0;
          mousePairingStarted = false;
          mouseIgnoreButtonsUntilRelease = true;
          lastMouseConnected = hasBleConnections();
          lastMouseReady = false;
          displayMousePairScreen(false, false, "");
          Serial.println(F("[Mouse] Entered Mouse menu"));
        } else {
          inSpamMenu = true;
          spamMenuIndex = 0;
          displaySpamMenu(spamMenuIndex);
          Serial.println(F("[Bluetooth] Entered Spam menu"));
        }
      }
      if (buttonBack.isClick()) {
        inMouseMenu = false;
        bluetoothMenuIndex = 0;
        inSpamMenu = false;
        display.clearDisplay();
        returnToMainMenu();
        display.display();
      }
    } else if (inSpamMenu && (bleSpamState == READY || bleSpamState == RUNNING)) {
      static unsigned long lastSpamTime = 0;
      static int deviceIndex = 0;
      static String currentDeviceName = "";

      bool intervalChanged = false;
      if (buttonUp.isClick()) {
        spamIntervalMs = min<uint16_t>(500, spamIntervalMs + 50);
        intervalChanged = true;
      }
      if (buttonDown.isClick()) {
        spamIntervalMs = max<uint16_t>(50, spamIntervalMs - 50);
        intervalChanged = true;
      }
      if (intervalChanged) {
        displayFullBLESpamScreen(spamMenuItems[spamMenuIndex], bleSpamState == RUNNING);
        return;
      }

      if (buttonOK.isClick()) {
        if (bleSpamState == READY) {
          bleSpamState = RUNNING;
          if (gBleInited) {
            stopBLE();
          }
          BLEDevice::init(bleDeviceName);
          BLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
          lastSpamTime = 0;
          deviceIndex = 0;
          clearBLESpamLog();
          Serial.println(F("[Bluetooth] Started BLE spam"));
        } else {
          bleSpamState = READY;
          BLEDevice::deinit();
          Serial.println(F("[Bluetooth] Stopped BLE spam"));
          displayFullBLESpamScreen(spamMenuItems[spamMenuIndex], false);
        }
      }

      if (buttonBack.isClick()) {
        if (bleSpamState == RUNNING) {
          BLEDevice::deinit();
        }
        bleSpamState = IDLE;
        inSpamMenu = true;
        displaySpamMenu(spamMenuIndex);
      }

      if (bleSpamState == RUNNING) {
        const unsigned long spamInterval = spamIntervalMs;

        if (millis() - lastSpamTime >= spamInterval) {
          switch (currentSpamType) {
            case AppleJuice:
              currentDeviceName = getSpamDeviceName(currentSpamType, deviceIndex);
              break;
            case Google:
              currentDeviceName = devices[deviceIndex % devicesCount].name;
              break;
            case Microsoft:
              currentDeviceName = generateRandomName();
              break;
            case Xiaomi:
              currentDeviceName = getSpamDeviceName(currentSpamType, deviceIndex);
              break;
            case Samsung:
              currentDeviceName = getSpamDeviceName(currentSpamType, deviceIndex);
              break;
          }

          Spam(currentSpamType, deviceIndex);
          displayBLESpamDevice(currentDeviceName.c_str());

          deviceIndex++;
          lastSpamTime = millis();
        }
      }
    }
  }
}
