#include "display.h"
#include <GyverButton.h>
#include "CONFIG.h"
#include <SD.h>
#include "Explorer.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <NimBLEServer.h>
#include <NimBLEAdvertising.h>
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

bool                  gBleInited   = false;
NimBLEServer*         gServer      = nullptr;
NimBLEHIDDevice*      gHid         = nullptr;
NimBLECharacteristic* gKeyboardInput = nullptr;
NimBLECharacteristic* gMouseInput    = nullptr;
NimBLEAdvertising*    gAdv         = nullptr;
uint16_t              gConnHandle  = 0xFFFF;
BLEHidMode            gBleMode     = BLE_HID_NONE;
bool                  gBleConnected = false;
bool                  gBleStopping = false;

static inline void _bb_hidSend(const uint8_t rpt[8]) {
  if (!gKeyboardInput) return;
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

static inline void _bm_sendMouseReport(int8_t x, int8_t y, uint8_t buttons = 0) {
  if (!gMouseInput) return;
  MouseReport report = {buttons, x, y, 0};
  gMouseInput->setValue(reinterpret_cast<uint8_t*>(&report), sizeof(report));
  gMouseInput->notify();
  delay(10);
}

static inline bool _bb_isNotifyEnabled(NimBLECharacteristic* ch) {
  if (!ch) return false;
  NimBLEDescriptor* cccd = ch->getDescriptorByUUID((uint16_t)0x2902);
  if (!cccd) return false;
  std::string v = cccd->getValue();
  if (v.size() < 2) return false;
  return (static_cast<uint8_t>(v[0]) & 0x01) != 0;
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

static void _bb_sendText(const String& s) {
  for (size_t i = 0; i < s.length(); ++i) {
    uint8_t key, mod;
    if (!_bb_asciiToHID(s[i], key, mod)) continue;
    _bb_hidPress(mod, key);
    delay(8);
    _bb_hidRelease();
    delay(8);
  }
}

// DuckyScript parser
static int _bb_parseDelay(const String& s) {
  long v = s.toInt();
  if (v < 0) v = 0;
  if (v > 60000) v = 60000;
  return (int)v;
}

static bool _bb_runDuckyScript(const char* filename, std::vector<String>& logs, DisplayType &display) {
  if (!gServer || !gKeyboardInput) {
    logs.push_back("BLE not initialized");
    Serial.println("[BadKB] BLE not initialized");
    displayBadKBScriptExec(display, filename, logs, logs.size() > 4 ? logs.size() - 4 : 0);
    return false;
  }
  if (gServer->getConnectedCount() == 0) {
    logs.push_back("No BLE connection");
    Serial.println("[BadKB] No BLE connection");
    displayBadKBScriptExec(display, filename, logs, logs.size() > 4 ? logs.size() - 4 : 0);
    return false;
  }

  File file = SD.open(filename, FILE_READ);
  if (!file) {
    logs.push_back("File not found");
    Serial.printf("[BadKB] Failed to open file: %s\n", filename);
    displayBadKBScriptExec(display, filename, logs, logs.size() > 4 ? logs.size() - 4 : 0);
    return false;
  }

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    logs.push_back(line);
    Serial.printf("[BadKB] Executing: %s\n", line.c_str());
    displayBadKBScriptExec(display, filename, logs, logs.size() > 4 ? logs.size() - 4 : 0);

    if (line.startsWith("REM")) { continue; }

    if (line.startsWith("DELAY ")) {
      int ms = _bb_parseDelay(line.substring(6));
      Serial.printf("[BadKB] Delaying %d ms\n", ms);
      delay(ms);
      continue;
    }

    if (line.equalsIgnoreCase("ENTER")) {
      Serial.println("[BadKB] Sending ENTER");
      _bb_hidPress(0x00, 0x28); delay(25); _bb_hidRelease();
      delay(6);
      continue;
    }

    if (line.equalsIgnoreCase("GUI R") || line.equalsIgnoreCase("GUI r") || line.equalsIgnoreCase("WINR")) {
      Serial.println("[BadKB] Sending GUI+R");
      _bb_hidPress(0x08, 0x15); delay(30); _bb_hidRelease();
      delay(60);
      continue;
    }

    if (line.startsWith("STRING ")) {
      String text = line.substring(7);
      Serial.printf("[BadKB] Sending STRING: %s\n", text.c_str());
      _bb_sendText(text);
      continue;
    }

    Serial.printf("[BadKB] Skipping unknown command: %s\n", line.c_str());
  }

  file.close();
  Serial.println("[BadKB] Script execution completed");
  logs.push_back("Done");
  displayBadKBScriptExec(display, filename, logs, logs.size() > 4 ? logs.size() - 4 : 0);
  return true;
}

class PairServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    gConnHandle = connInfo.getConnHandle();
    gBleConnected = true;
    pServer->updateConnParams(gConnHandle, 6, 6, 0, 30);
    Serial.printf("[BLE Pair] Connected (handle=%u)\n", gConnHandle);
  }
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    Serial.printf("[BLE Pair] Disconnected, reason=%d\n", reason);
    gBleConnected = false;
    gConnHandle = 0xFFFF;
    if (!gBleStopping && gAdv) {
      gAdv->start();
    }
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
  gConnHandle = 0xFFFF;
  gBleMode = BLE_HID_NONE;
  gBleConnected = false;
  gBleStopping = false;
}

static void disconnectAllBlePeers() {
  if (!gServer) return;

  const int peerCount = gServer->getConnectedCount();
  for (int peerIndex = peerCount - 1; peerIndex >= 0; --peerIndex) {
    NimBLEConnInfo peerInfo = gServer->getPeerInfo((uint8_t)peerIndex);
    gServer->disconnect(peerInfo.getConnHandle());
    delay(35);
  }
}

static void pauseBLE() {
  if (!gBleInited) return;

  gBleStopping = true;

  if (gAdv && gAdv->isAdvertising()) {
    gAdv->stop();
    delay(20);
  }

  disconnectAllBlePeers();
  gBleConnected = false;
  gConnHandle = 0xFFFF;
  gBleStopping = false;
}

static void ensureBleHidInited(BLEHidMode mode) {
  (void)mode;
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

  gHid->setManufacturer("ESP-HACK");
  gHid->setPnp(0x02, 0x303A, 0x4001, 0x0100);
  gHid->setHidInfo(0x00, 0x01);
  gHid->setReportMap((uint8_t*)compositeHidReportDescriptor, sizeof(compositeHidReportDescriptor));
  gHid->setBatteryLevel(100);
  gHid->startServices();

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
  gBleConnected = false;
  gBleStopping = false;
  Serial.println("[BLE Pair] BLE/HID initialized");
}

static void stopBLE() {
  gBleStopping = true;

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
ExplorerConfig badKBExplorerCfg = {"/BadKB", badKbExts, 1, true, false, true, true};

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
  display.setCursor(1, 1);
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
  if (strcmp(spamType, "IOS-SPM") == 0) return "IOS-SPAM";
  if (strcmp(spamType, "ANDR-SPM") == 0) return "ANDROID-SPAM";
  if (strcmp(spamType, "WIN-SPM") == 0) return "WINDOWS-SPAM";
  return spamType;
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
  display.setCursor(1, 1);
  if (running) {
    display.println(String(getBLESpamHeaderName(spamType)) + "...");
    display.println(F("---------------------"));
  } else {
    display.println(F("Press OK."));
    display.println(F("---------------------"));
  }
}

void displayBLESpamDevice(const char* deviceName) {
  pushBLESpamLog(deviceName);
  displayBLESpamHeader(bluetoothMenuItems[bluetoothMenuIndex], true);
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
  display.setCursor(1, 1);
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

static void displayMouseConfigScreen(uint8_t selection, uint8_t powerIndex, uint8_t speedIndex,
                                     uint8_t modeIndex, bool running, const char* errorText = nullptr) {
  const int valueX = 54;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(1, 1);
  display.println(F("Mouse"));
  display.setCursor(1, 10);
  display.println(F("---------------------"));

  if (errorText && errorText[0] != '\0') {
    display.setCursor(1, 18);
    display.print(errorText);
  }

  display.setCursor(6, 22);
  display.print(selection == 0 ? ">" : " ");
  display.print(F("Power:"));
  display.setCursor(valueX, 22);
  display.print(mousePowerValues[powerIndex]);
  display.println(F("%"));

  display.setCursor(6, 30);
  display.print(selection == 1 ? ">" : " ");
  display.print(F("Speed:"));
  display.setCursor(valueX, 30);
  display.print(mouseSpeedValues[speedIndex]);
  display.println(F("%"));

  display.setCursor(6, 38);
  display.print(selection == 2 ? ">" : " ");
  display.print(F("Mode: "));
  display.println(mouseModeLabels[modeIndex]);

  display.setCursor(8, 50);
  display.print(selection == 3 ? ">" : " ");
  display.println(running ? F("Stop") : F("Start"));
  display.display();
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
  static bool scriptRunning = false;
  static bool scriptDone = false;
  static String selectedFile = "";
  static bool waitingForConnection = false;
  static bool justEnteredBadBLE = false;
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

  if (inBadBLE) {
    if (scriptSelected) {
      if (scriptRunning) {
        ensureBleHidInited(BLE_HID_KEYBOARD);
        if (!(gAdv && gAdv->isAdvertising())) {
          gAdv->start();
          Serial.println("[BadKB] Started BLE advertising");
        }

        if (!waitingForConnection) {
          waitingForConnection = true;
          execLogs.clear();
          execLogs.push_back("Waiting for BLE...");
          displayBadKBScriptExec(display, selectedFile, execLogs, 0);
        }

        const uint32_t connectionTimeout = 30000;
        static uint32_t waitStartTime = 0;
        if (waitStartTime == 0) {
          waitStartTime = millis();
        }

        if (gServer && gServer->getConnectedCount() > 0) {
          waitingForConnection = false;
          waitStartTime = 0;
          delay(3000);
          _bb_hidRelease();
          delay(40);
          execLogs.clear();
          bool ok = _bb_runDuckyScript(selectedFile.c_str(), execLogs, display);
          scriptRunning = false;
          scriptDone = true;
          if (!ok) {
            execLogs.push_back("Failed");
          }
          displayBadKBScriptExec(display, selectedFile, execLogs, execLogs.size() > 4 ? execLogs.size() - 4 : 0);
        } else if (millis() - waitStartTime >= connectionTimeout) {
          waitingForConnection = false;
          waitStartTime = 0;
          scriptRunning = false;
          scriptDone = true;
          execLogs.clear();
          execLogs.push_back("Connection timeout");
          displayBadKBScriptExec(display, selectedFile, execLogs, 0);
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
      if (gAdv && !gAdv->isAdvertising()) {
        gAdv->start();
      }
      mousePairingStarted = true;
    }
    if (mouseRunning) {
      ensureBleHidInited(BLE_HID_MOUSE);
      if (!(gAdv && gAdv->isAdvertising()) && !gBleConnected && gAdv) {
        gAdv->start();
      }
    }
  } else {
    if (bleSpamState == IDLE) {
      displayBluetoothMenu(display, bluetoothMenuIndex);
    } else if (bleSpamState == READY) {
      displayFullBLESpamScreen(bluetoothMenuItems[bluetoothMenuIndex], false);
    }
  }

  if (inBadBLE) {
    if (scriptSelected) {
      if (buttonUp.isClick() && execLogTop > 0) {
        execLogTop--;
        displayBadKBScriptExec(display, selectedFile, execLogs, execLogTop);
      }
      if (buttonDown.isClick() && execLogTop + ((display.height() - 20) / 10) < (int)execLogs.size()) {
        execLogTop++;
        displayBadKBScriptExec(display, selectedFile, execLogs, execLogTop);
      }
      if (buttonBack.isClick()) {
        scriptSelected = false;
        scriptRunning = false;
        scriptDone = false;
        waitingForConnection = false;
        execLogs.clear();
        execLogTop = 0;
        if (gAdv && gAdv->isAdvertising()) {
          gAdv->stop();
        }
        ExplorerInit(badKBExplorer, badKBFileList, MAX_FILES, badKBExplorerCfg);
        loadBadKBFileList();
        explorerLoaded = true;
        drawBadKBExplorer(display);
      }
      if (buttonOK.isClick() && scriptDone) {
        scriptRunning = true;
        scriptDone = false;
        waitingForConnection = false;
      }
    } else {
      bool backClick = buttonBack.isClick();
      if (justEnteredBadBLE) backClick = false;
      ExplorerAction action = ExplorerHandle(
        badKBExplorer,
        badKBExplorerCfg,
        display,
        buttonUp.isClick(),
        buttonDown.isClick(),
        buttonOK.isClick(),
        backClick,
        buttonBack.isHold()
      );
      if (action == EXPLORER_SELECT_FILE) {
        selectedFile = badKBExplorer.currentDir + "/" + badKBExplorer.selectedFile;
        scriptSelected = true;
        scriptRunning = true;
        scriptDone = false;
        waitingForConnection = false;
        execLogs.clear();
        execLogTop = 0;
        ensureBleHidInited(BLE_HID_KEYBOARD);
        displayBadKBScriptExec(display, selectedFile, execLogs, execLogTop);
        Serial.println(F("[BadKB] Selected BadKB script"));
      } else if (action == EXPLORER_EXIT) {
        inBadBLE = false;
        explorerLoaded = false;
        justEnteredBadBLE = false;
        pauseBLE();
        bluetoothMenuIndex = 0;
        display.clearDisplay();
        returnToMainMenu();
        display.display();
        Serial.println(F("[BadKB] Back to main menu from BadKB"));
      }
      if (justEnteredBadBLE) justEnteredBadBLE = false;
    }
  } else if (inMouseMenu) {
    static MenuButtonState mouseUpHeld;
    static MenuButtonState mouseDownHeld;
    const bool mouseReady = gBleConnected;
    const char* mouseErrorText = (mouseRunning && gBleInited && !gMouseInput) ? "BLE error" : "";

    if (lastMouseConnected != gBleConnected || lastMouseReady != mouseReady) {
      lastMouseConnected = gBleConnected;
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
        displayMousePairScreen(gBleConnected, mouseReady, mouseErrorText);
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
      mouseSelection = (mouseSelection == 0) ? 3 : mouseSelection - 1;
      displayMouseConfigScreen(mouseSelection, mousePowerIndex, mouseSpeedIndex, mouseModeIndex,
                               mouseRunning, mouseErrorText);
    }
    if (isMenuButtonPress(BUTTON_DOWN, mouseDownHeld)) {
      mouseSelection = (mouseSelection + 1) % 4;
      displayMouseConfigScreen(mouseSelection, mousePowerIndex, mouseSpeedIndex, mouseModeIndex,
                               mouseRunning, mouseErrorText);
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
          if (gAdv && !gAdv->isAdvertising()) {
            gAdv->start();
          }
        }
        lastMouseConnected = gBleConnected;
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
  } else {
    if (bleSpamState == IDLE) {
      static MenuButtonState upHeld;
      static MenuButtonState downHeld;

      if (isMenuButtonPress(BUTTON_UP, upHeld)) {
        byte previousIndex = bluetoothMenuIndex;
        bluetoothMenuIndex = (bluetoothMenuIndex - 1 + BLUETOOTH_MENU_ITEM_COUNT) % BLUETOOTH_MENU_ITEM_COUNT;
        displayBluetoothMenu(display, bluetoothMenuIndex, previousIndex);
      }
      if (isMenuButtonPress(BUTTON_DOWN, downHeld)) {
        byte previousIndex = bluetoothMenuIndex;
        bluetoothMenuIndex = (bluetoothMenuIndex + 1) % BLUETOOTH_MENU_ITEM_COUNT;
        displayBluetoothMenu(display, bluetoothMenuIndex, previousIndex);
      }
      if (buttonOK.isClick()) {
        if (bluetoothMenuIndex == 3) {
          inBadBLE = true;
          inMouseMenu = false;
          explorerLoaded = false;
          justEnteredBadBLE = true;
          ensureBleHidInited(BLE_HID_KEYBOARD);
          Serial.println(F("[BadKB] Entered BadKB"));
        } else if (bluetoothMenuIndex == 4) {
          inMouseMenu = true;
          inBadBLE = false;
          explorerLoaded = false;
          mouseRunning = false;
          mouseSelection = 0;
          mousePairingStarted = false;
          mouseIgnoreButtonsUntilRelease = true;
          lastMouseConnected = gBleConnected;
          lastMouseReady = false;
          displayMousePairScreen(false, false, "");
          Serial.println(F("[Mouse] Entered Mouse menu"));
        } else {
          bleSpamState = READY;
          switch (bluetoothMenuIndex) {
            case 0: currentSpamType = AppleJuice; break;
            case 1: currentSpamType = Google; break;
            case 2: currentSpamType = Microsoft; break;
          }
          displayFullBLESpamScreen(bluetoothMenuItems[bluetoothMenuIndex], false);
          Serial.println(F("[Bluetooth] Ready for BLE spam"));
        }
      }
      if (buttonBack.isClick()) {
        inMouseMenu = false;
        bluetoothMenuIndex = 0;
        display.clearDisplay();
        returnToMainMenu();
        display.display();
      }
    } else if (bleSpamState == READY || bleSpamState == RUNNING) {
      static unsigned long lastSpamTime = 0;
      static int deviceIndex = 0;
      static String currentDeviceName = "";

      if (buttonOK.isClick()) {
        if (bleSpamState == READY) {
          bleSpamState = RUNNING;
          if (gBleInited) {
            stopBLE();
          }
          BLEDevice::init(bleDeviceName);
          lastSpamTime = 0;
          deviceIndex = 0;
          clearBLESpamLog();
          Serial.println(F("[Bluetooth] Started BLE spam"));
        } else {
          bleSpamState = READY;
          BLEDevice::deinit();
          Serial.println(F("[Bluetooth] Stopped BLE spam"));
          displayFullBLESpamScreen(bluetoothMenuItems[bluetoothMenuIndex], false);
        }
      }

      if (buttonBack.isClick()) {
        if (bleSpamState == RUNNING) {
          BLEDevice::deinit();
        }
        bleSpamState = IDLE;
        displayBluetoothMenu(display, bluetoothMenuIndex);
      }

      if (bleSpamState == RUNNING) {
        const unsigned long spamInterval = 100; // Spam Interval

        if (millis() - lastSpamTime >= spamInterval) {
          switch (currentSpamType) {
            case AppleJuice:
              currentDeviceName = appleDevices[deviceIndex % appleDevicesCount].name;
              break;
            case Google:
              currentDeviceName = devices[deviceIndex % devicesCount].name;
              break;
            case Microsoft:
              currentDeviceName = generateRandomName();
              break;
          }

          executeSpam(currentSpamType);
          displayBLESpamDevice(currentDeviceName.c_str());

          deviceIndex++;
          lastSpamTime = millis();
        }
      }
    }
  }
}
