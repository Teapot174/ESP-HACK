#include <Adafruit_SH110X.h>
#include <GyverButton.h>
#include "CONFIG.h"
#include "menu/bluetooth.h"
#include "ble_spam.h"
#include "menu/subghz.h"
#include <SD.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <NimBLEServer.h>
#include <NimBLEAdvertising.h>
#include <vector>
#include <string>
#include <algorithm>

extern Adafruit_SH1106G display;
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

void displayBadKBScriptExec(Adafruit_SH1106G &display, const String& filename, const std::vector<String>& logs, int logTop);

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

bool                  gBleInited   = false;
NimBLEServer*         gServer      = nullptr;
NimBLEHIDDevice*      gHid         = nullptr;
NimBLECharacteristic* gInput       = nullptr;
NimBLEAdvertising*    gAdv         = nullptr;
uint16_t              gConnHandle  = 0xFFFF;

static inline void _bb_hidSend(const uint8_t rpt[8]) {
  if (!gInput) return;
  gInput->setValue((uint8_t*)rpt, 8);
  gInput->notify();
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

static bool _bb_runDuckyScript(const char* filename, std::vector<String>& logs, Adafruit_SH1106G &display) {
  if (!gServer || !gInput) {
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
    Serial.printf("[BLE Pair] Connected (handle=%u)\n", gConnHandle);
  }
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    Serial.printf("[BLE Pair] Disconnected, reason=%d\n", reason);
    gConnHandle = 0xFFFF;
    if (gAdv) {
      gAdv->start();
    }
  }
};

static void ensureBleHidInited() {
  if (gBleInited) return;

  Serial.println("[BLE Pair] Initializing BLE...");
  NimBLEDevice::init("ESP-BLE");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(true, false, true);

  gServer = NimBLEDevice::createServer();
  static PairServerCallbacks sCallbacks;
  gServer->setCallbacks(&sCallbacks);

  gHid = new NimBLEHIDDevice(gServer);
  gInput = gHid->getInputReport(1);

  gHid->setManufacturer("xAI");
  gHid->setPnp(0x02, 0x1234, 0xABCD, 0x0110);
  gHid->setHidInfo(0x00, 0x01);
  gHid->setReportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));
  gHid->setBatteryLevel(100);
  gHid->startServices();

  gAdv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  NimBLEAdvertisementData scanData;

  advData.setFlags(0x06);
  advData.setAppearance(0x03C1);
  advData.addServiceUUID(gHid->getHidService()->getUUID());
  scanData.setName("ESP-BLE");

  gAdv->setAdvertisementData(advData);
  gAdv->setScanResponseData(scanData);
  gAdv->setMinInterval(32);
  gAdv->setMaxInterval(64);

  gBleInited = true;
  Serial.println("[BLE Pair] BLE/HID initialized");
}

static void stopBLE() {
  if (gAdv && gAdv->isAdvertising()) {
    gAdv->stop();
    Serial.println("[BLE Pair] Advertising stopped");
  }
  if (gBleInited) {
    NimBLEDevice::deinit();
    gBleInited = false;
    gServer = nullptr;
    gHid = nullptr;
    gInput = nullptr;
    gAdv = nullptr;
    gConnHandle = 0xFFFF;
    Serial.println("[BLE Pair] BLE deinitialized");
  }
}

#define MAX_FILES 50
struct BadKBFileEntry {
  String name;
  bool isDir;
};
BadKBFileEntry badKBFileList[MAX_FILES];
int badKBFileCount = 0;
int badKBFileIndex = 0;
String badKBCurrentDir = "/BadKB";

static void loadBadKBFileList() {
  badKBFileCount = 0;
  File root = SD.open(badKBCurrentDir);
  if (!root) {
    Serial.println(F("Failed to open directory"));
    return;
  }
  if (!root.isDirectory()) {
    Serial.println(F("Not a directory"));
    root.close();
    return;
  }

  int dirCount = 0, fileCount = 0;
  File file = root.openNextFile();
  while (file) {
    String fname = String(file.name());
    if (fname[0] != '.') {
      if (file.isDirectory()) dirCount++;
      else if (fname.endsWith(".txt")) fileCount++;
    }
    file.close();
    file = root.openNextFile();
  }

  root.rewindDirectory();
  file = root.openNextFile();
  std::vector<String> dirs;
  while (file) {
    String fname = String(file.name());
    if (file.isDirectory() && fname[0] != '.') {
      dirs.push_back(fname);
    }
    file.close();
    file = root.openNextFile();
  }
  std::sort(dirs.begin(), dirs.end());
  for (auto& d : dirs) {
    badKBFileList[badKBFileCount].name = d;
    badKBFileList[badKBFileCount].isDir = true;
    badKBFileCount++;
  }

  root.rewindDirectory();
  file = root.openNextFile();
  std::vector<String> files;
  while (file) {
    String fname = String(file.name());
    if (!file.isDirectory() && fname[0] != '.' && fname.endsWith(".txt")) {
      files.push_back(fname);
    }
    file.close();
    file = root.openNextFile();
  }
  std::sort(files.begin(), files.end());
  for (auto& f : files) {
    badKBFileList[badKBFileCount].name = f;
    badKBFileList[badKBFileCount].isDir = false;
    badKBFileCount++;
  }

  root.close();
}

void drawBadKBExplorer(Adafruit_SH1106G &display) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(1, 1);
  display.print(badKBCurrentDir);
  display.setCursor(1, 12);
  display.println(F("---------------------"));

  if (badKBFileCount == 0) {
    display.setCursor(1, 24);
    display.println(F("No files."));
    display.display();
    return;
  }

  const int perPage = 4;
  int maxStart = (badKBFileCount > perPage) ? (badKBFileCount - perPage) : 0;
  int startIndex = badKBFileIndex - 1;
  if (startIndex < 0) startIndex = 0;
  if (startIndex > maxStart) startIndex = maxStart;
  for (int i = 0; i < perPage; i++) {
    int idx = startIndex + i;
    if (idx >= badKBFileCount) break;
    display.setCursor(1, 22 + i * 10);
    if (idx == badKBFileIndex) {
      display.fillRect(0, 22 + i * 10 - 1, display.width(), 10, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }
    String name = badKBFileList[idx].name;
    if (badKBFileList[idx].isDir) name = "[" + name + "]";
    display.print(name);
  }
  display.display();
}

void drawDeleteConfirm(Adafruit_SH1106G &display, const String& filename) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(1, 1);
  display.print(F("File: "));
  display.print(filename);
  display.setCursor(1, 24);
  display.print(F("Press OK to DELETE."));
  display.display();
}

void displayBadKBScriptExec(Adafruit_SH1106G &display, const String& filename, const std::vector<String>& logs, int logTop) {
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

String generateRandomName() {
  const char* charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  int len = random(1, 11);
  String randomName = "";
  for (int i = 0; i < len; ++i) {
    randomName += charset[random(0, strlen(charset))];
  }
  return randomName;
}

void displayBLESpamHeader(const char* spamType, bool running) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(1, 1);
  if (running) {
    display.println(String(spamType) + "...");
    display.println(F("---------------------"));
  } else {
    display.println(F("Press OK."));
    display.println(F("---------------------"));
  }
}

void displayBLESpamDevice(const char* deviceName) {
  display.fillRect(0, 16, display.width(), 10, SH110X_BLACK);
  display.setCursor(1, 16);
  display.print(deviceName);
  display.display();
}

void displayFullBLESpamScreen(const char* spamType, bool running, const char* deviceName = "") {
  displayBLESpamHeader(spamType, running);
  if (running) {
    display.setCursor(1, 16);
    display.print(deviceName);
  }
  display.display();
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
  static bool deleteConfirm = false;
  static String deleteFile = "";
  static bool justEnteredBadBLE = false;

  if (inBadBLE) {
    if (deleteConfirm) {
      drawDeleteConfirm(display, deleteFile);
      if (buttonOK.isClick()) {
        String fullPath = badKBCurrentDir + "/" + deleteFile;
        bool success;
        if (badKBFileList[badKBFileIndex].isDir) {
          success = SD.rmdir(fullPath);
        } else {
          success = SD.remove(fullPath);
        }
        display.clearDisplay();
        display.setTextColor(SH110X_WHITE);
        display.setTextWrap(false);
        if (success) {
          display.drawBitmap(5, 2, image_DolphinMafia_bits, 119, 62, SH110X_WHITE);
          display.setCursor(84, 15);
          display.print(F("Deleted"));
          display.display();
          Serial.printf("[BadKB] File deleted: %s\n", fullPath.c_str());
        } else {
          display.setCursor(1, 1);
          display.print(F("Failed to delete"));
          display.display();
          Serial.printf("[BadKB] Failed to delete file: %s\n", fullPath.c_str());
        }
        delay(1000);
        deleteConfirm = false;
        explorerLoaded = false;
        badKBFileCount = 0;
        badKBFileIndex = 0;
        loadBadKBFileList();
        drawBadKBExplorer(display);
      }
      if (buttonBack.isClick()) {
        deleteConfirm = false;
        drawBadKBExplorer(display);
      }
      return;
    }
    if (scriptSelected) {
      if (scriptRunning) {
        ensureBleHidInited();
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
        loadBadKBFileList();
        explorerLoaded = true;
        justEnteredBadBLE = false;
      }
      drawBadKBExplorer(display);
    }
  } else {
    if (bleSpamState == IDLE) {
      displayBluetoothMenu(display, bluetoothMenuIndex);
    } else {
      static String currentDeviceName = "";
      if (bleSpamState == READY) {
        displayFullBLESpamScreen(bluetoothMenuItems[bluetoothMenuIndex], false);
      } else {
        static bool firstRun = true;
        if (firstRun) {
          displayFullBLESpamScreen(
            bluetoothMenuItems[bluetoothMenuIndex],
            true,
            currentDeviceName.c_str()
          );
          firstRun = false;
        }
      }
    }
  }

  if (inBadBLE) {
    if (deleteConfirm) {
    } else if (scriptSelected) {
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
        explorerLoaded = false;
        drawBadKBExplorer(display);
      }
      if (buttonOK.isClick() && scriptDone) {
        scriptRunning = true;
        scriptDone = false;
        waitingForConnection = false;
      }
    } else {
      if (buttonUp.isClick()) {
        if (badKBFileIndex > 0) {
          badKBFileIndex--;
          drawBadKBExplorer(display);
        }
        Serial.println(F("[BadKB] menu Up"));
      }
      if (buttonDown.isClick()) {
        if (badKBFileIndex < badKBFileCount - 1) {
          badKBFileIndex++;
          drawBadKBExplorer(display);
        }
        Serial.println(F("[BadKB] menu Down"));
      }
      if (buttonOK.isClick()) {
        if (badKBFileCount == 0) return;
        String selName = badKBFileList[badKBFileIndex].name;
        if (badKBFileList[badKBFileIndex].isDir) {
          badKBCurrentDir += "/" + selName;
          badKBFileIndex = 0;
          explorerLoaded = false;
          drawBadKBExplorer(display);
          Serial.println(F("[BadKB] Entered BadKB directory"));
        } else if (selName.endsWith(".txt")) {
          selectedFile = badKBCurrentDir + "/" + selName;
          scriptSelected = true;
          scriptRunning = true;
          scriptDone = false;
          waitingForConnection = false;
          execLogs.clear();
          execLogTop = 0;
          ensureBleHidInited();
          displayBadKBScriptExec(display, selectedFile, execLogs, execLogTop);
          Serial.println(F("[BadKB] Selected BadKB script"));
        }
      }
      if (buttonBack.isHold()) {
        if (badKBFileCount == 0) return;
        deleteFile = badKBFileList[badKBFileIndex].name;
        deleteConfirm = true;
        drawDeleteConfirm(display, deleteFile);
      }
      if (buttonBack.isClick() && !justEnteredBadBLE) {
        if (badKBCurrentDir != "/BadKB") {
          int lastSlash = badKBCurrentDir.lastIndexOf('/');
          badKBCurrentDir = badKBCurrentDir.substring(0, lastSlash);
          badKBFileIndex = 0;
          explorerLoaded = false;
          drawBadKBExplorer(display);
        } else {
          inBadBLE = false;
          explorerLoaded = false;
          justEnteredBadBLE = false;
          stopBLE();
          inMenu = true;
          bluetoothMenuIndex = 0;
          display.clearDisplay();
          OLED_printMenu(display, currentMenu);
          display.display();
          Serial.println(F("[BadKB] Back to main menu from BadKB"));
        }
      }
    }
  } else {
    if (bleSpamState == IDLE) {
      if (buttonUp.isClick()) {
        bluetoothMenuIndex = (bluetoothMenuIndex - 1 + BLUETOOTH_MENU_ITEM_COUNT) % BLUETOOTH_MENU_ITEM_COUNT;
        displayBluetoothMenu(display, bluetoothMenuIndex);
      }
      if (buttonDown.isClick()) {
        bluetoothMenuIndex = (bluetoothMenuIndex + 1) % BLUETOOTH_MENU_ITEM_COUNT;
        displayBluetoothMenu(display, bluetoothMenuIndex);
      }
      if (buttonOK.isClick()) {
        if (bluetoothMenuIndex == 3) {
          inBadBLE = true;
          badKBCurrentDir = "/BadKB";
          badKBFileIndex = 0;
          explorerLoaded = false;
          justEnteredBadBLE = true;
          ensureBleHidInited();
          Serial.println(F("[BadKB] Entered BadKB"));
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
        inMenu = true;
        bluetoothMenuIndex = 0;
        display.clearDisplay();
        OLED_printMenu(display, currentMenu);
        display.display();
      }
    } else if (bleSpamState == READY || bleSpamState == RUNNING) {
      static unsigned long lastSpamTime = 0;
      static int deviceIndex = 0;
      static String currentDeviceName = "";
      static bool firstRun = true;

      if (buttonOK.isClick()) {
        if (bleSpamState == READY) {
          bleSpamState = RUNNING;
          BLEDevice::init("ESP-HACK");
          lastSpamTime = 0;
          deviceIndex = 0;
          firstRun = true;
          Serial.println(F("[Bluetooth] Started BLE spam"));
        } else {
          bleSpamState = READY;
          BLEDevice::deinit();
          firstRun = true;
          Serial.println(F("[Bluetooth] Stopped BLE spam"));
          displayFullBLESpamScreen(bluetoothMenuItems[bluetoothMenuIndex], false);
        }
      }

      if (buttonBack.isClick()) {
        if (bleSpamState == RUNNING) {
          BLEDevice::deinit();
        }
        bleSpamState = IDLE;
        firstRun = true;
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

          if (firstRun) {
            displayFullBLESpamScreen(
              bluetoothMenuItems[bluetoothMenuIndex],
              true,
              currentDeviceName.c_str()
            );
            firstRun = false;
          } else {
            displayBLESpamDevice(currentDeviceName.c_str());
          }

          deviceIndex++;
          lastSpamTime = millis();
        }
      }
    }
  }
}
