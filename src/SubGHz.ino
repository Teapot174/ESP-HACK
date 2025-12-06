#include "subghz.h"
#include "subghz_menu.h"
#include <Adafruit_SH110X.h>
#include <SD.h>
#include <SPI.h>
#include <RCSwitch.h>

// CC1101
#define DEFAULT_RF_FREQUENCY 433.92 // MHz
float frequency = DEFAULT_RF_FREQUENCY;
const float frequencies[] = {315.0, 433.92, 868.0, 915.0};
const int numFrequencies = sizeof(frequencies) / sizeof(frequencies[0]);
int freqIndex = 1; // Default 433.92 MHz
int current_scan_index = 0;
const int rssi_threshold = -85; // RSSI threshold

// RCSwitch
RCSwitch rcswitch = RCSwitch();

#define MAX_DATA_LOG 512

byte menuIndex = 0;
bool validKeyReceived = false;
bool readRAW = true;
bool autoSave = false;
int signals = 0;
uint64_t lastSavedKey = 0;
tpKeyData keyData1;
float last_detected_frequency = 0.0;
bool isJamming = false;
unsigned long scanTimer = 0;

// File explorer state
#define MAX_FILES 50
struct FileEntry {
  String name;
  bool isDir;
};
FileEntry fileList[MAX_FILES];
int fileCount = 0;
int fileIndex = 0;
bool inFileExplorer = false;
bool inDeleteConfirm = false;
String selectedFile = "";
String currentDir = "/SubGHz";
bool SubFiles = false;

#define CC1101_SPI_MODULE 1

struct BruceConfig {
  int rfModule = CC1101_SPI_MODULE;
  int rfTx = CC1101_GDO0;
} bruceConfig;

struct BruceConfigPins {
  struct {
    int io0 = CC1101_GDO0;
  } CC1101_bus;
} bruceConfigPins;

void setupCC1101();
void configureCC1101();
void restoreReceiveMode();
void read_rcswitch(tpKeyData* kd);
void read_raw(tpKeyData* kd);
void OLED_printWaitingSignal();
void OLED_printKey(tpKeyData* kd, String fileName = "", bool isSending = false);
void OLED_printError(String st, bool err = true);
bool saveKeyToSD(tpKeyData* kd);
bool loadKeyFromSD(String fileName, tpKeyData* kd);
void sendSynthKey(tpKeyData* kd);
void stepFrequency(int step);
void RCSwitch_send(uint64_t data, unsigned int bits, int pulse, int protocol, int repeat);
void RCSwitch_RAW_send(int *ptrtransmittimings);
void OLED_printAnalyzer(bool signalReceived = false, float detectedFreq = 0.0);
void OLED_printJammer();
void OLED_printFileExplorer();
void OLED_printDeleteConfirm();
void startJamming();
void stopJamming();
String getTypeName(emKeys tp);
void loadFileList();
bool initRfModule(String mode, float freq);
void deinitRfModule();

bool initRfModule(String mode, float freq) {
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.setGDO0(CC1101_GDO0);
  ELECHOUSE_cc1101.SpiStrobe(0x30); // SRES
  delayMicroseconds(1000);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setMHZ(freq);
  ELECHOUSE_cc1101.setModulation(2); // OOK/ASK
  if (mode == "tx" || mode == "TX") {
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setCrc(0);
    ELECHOUSE_cc1101.setPktFormat(3); // async serial on GDO0
    ELECHOUSE_cc1101.SetTx();
    pinMode(CC1101_GDO0, OUTPUT);
    digitalWrite(CC1101_GDO0, LOW);
  } else {
    ELECHOUSE_cc1101.setPktFormat(0); // normal packet mode for RX
    ELECHOUSE_cc1101.SetRx();
  }
  ELECHOUSE_cc1101.SpiStrobe(0x33); // SCAL
  delayMicroseconds(2000);
  return true;
}

void deinitRfModule() {
  ELECHOUSE_cc1101.SpiStrobe(0x36); // SIDLE
  ELECHOUSE_cc1101.SetRx();
  pinMode(CC1101_GDO0, OUTPUT);
  digitalWrite(CC1101_GDO0, LOW);
}

void runSubGHz() {
  setupCC1101();
  rcswitch.enableReceive(CC1101_GDO0);
  emMenuState menuState = menuMain;
  menuIndex = 0;
  SubFiles = false;
  inDeleteConfirm = false;
  OLED_printSubGHzMenu(display, menuIndex);

  while (true) {
    buttonUp.tick();
    buttonDown.tick();
    buttonOK.tick();
    buttonBack.tick();

    if (menuState == menuMain) {
      if (buttonUp.isClick()) {
        menuIndex = (menuIndex == 0) ? 3 : menuIndex - 1;
        OLED_printSubGHzMenu(display, menuIndex);
      }
      if (buttonDown.isClick()) {
        menuIndex = (menuIndex == 3) ? 0 : menuIndex + 1;
        OLED_printSubGHzMenu(display, menuIndex);
      }
      if (buttonOK.isClick()) {
        if (menuIndex == 0) {
          menuState = menuReceive;
          inDeleteConfirm = false;
          setupCC1101();
          rcswitch.disableReceive();
          rcswitch.enableReceive(CC1101_GDO0);
          validKeyReceived = false;
          signals = 0;
          memset(&keyData1, 0, sizeof(tpKeyData));
          lastSavedKey = 0;
          rcswitch.resetAvailable();
          OLED_printWaitingSignal();
        } else if (menuIndex == 1) {
          menuState = menuTransmit;
          inFileExplorer = true;
          inDeleteConfirm = false;
          SubFiles = true;
          fileIndex = 0;
          fileCount = 0;
          currentDir = "/SubGHz"; // Reset to root directory
          loadFileList();
          OLED_printFileExplorer();
        } else if (menuIndex == 2) {
          menuState = menuAnalyzer;
          inDeleteConfirm = false;
          setupCC1101();
          rcswitch.disableReceive();
          current_scan_index = 0;
          last_detected_frequency = 0.0;
          scanTimer = millis();
          OLED_printAnalyzer();
        } else if (menuIndex == 3) {
          menuState = menuJammer;
          inDeleteConfirm = false;
          isJamming = false;
          OLED_printJammer();
        }
      }
      if (buttonBack.isClick()) {
        SubFiles = false;
        inDeleteConfirm = false;
        break;
      }
    } else if (menuState == menuReceive) {
      if (buttonUp.isClick()) {
        stepFrequency(1);
        keyData1.frequency = frequency;
        setupCC1101();
        OLED_printWaitingSignal();
      }
      if (buttonDown.isClick()) {
        stepFrequency(-1);
        keyData1.frequency = frequency;
        setupCC1101();
        OLED_printWaitingSignal();
      }
      if (buttonOK.isHolded() && validKeyReceived) {
        if (saveKeyToSD(&keyData1)) {
          display.clearDisplay();
          display.drawBitmap(16, 6, image_DolphinSaved_bits, 92, 58, SH110X_WHITE);
          display.setTextColor(SH110X_WHITE);
          display.setCursor(6, 16);
          display.print("Saved");
          display.display();
          Serial.println(F("Key saved successfully to SD"));
          delay(1000);
          validKeyReceived = false;
          signals = 0;
          memset(&keyData1, 0, sizeof(tpKeyData));
          lastSavedKey = 0;
          rcswitch.resetAvailable();
        } else {
          OLED_printError(F("Key not saved"), true);
          Serial.println(F("Failed to save key to SD"));
        }
        OLED_printWaitingSignal();
      }
      if (buttonBack.isClick()) {
        menuState = menuMain;
        SubFiles = false;
        inDeleteConfirm = false;
        OLED_printSubGHzMenu(display, menuIndex);
      }
      if (rcswitch.available()) {
        Serial.println(F(" Signal:"));
        if (!readRAW) read_rcswitch(&keyData1);
        else read_raw(&keyData1);
        if (validKeyReceived && autoSave && (lastSavedKey != keyData1.keyID[0] || keyData1.keyID[0] == 0)) {
          if (saveKeyToSD(&keyData1)) {
            lastSavedKey = keyData1.keyID[0];
            Serial.println(F("Key auto-saved successfully to SD"));
            validKeyReceived = false;
            signals = 0;
            memset(&keyData1, 0, sizeof(tpKeyData));
            rcswitch.resetAvailable();
          } else {
            Serial.println(F("Auto-save failed"));
          }
        }
      }
    } else if (menuState == menuTransmit && inFileExplorer && !inDeleteConfirm) {
      if (buttonUp.isClick()) {
        fileIndex = (fileIndex == 0) ? (fileCount - 1) : fileIndex - 1;
        OLED_printFileExplorer();
      }
      if (buttonDown.isClick()) {
        fileIndex = (fileIndex == fileCount - 1) ? 0 : fileIndex + 1;
        OLED_printFileExplorer();
      }
      if (buttonOK.isClick() && fileCount > 0) {
        if (fileList[fileIndex].isDir) {
          currentDir += "/" + fileList[fileIndex].name;
          fileIndex = 0;
          fileCount = 0;
          loadFileList();
          OLED_printFileExplorer();
        } else {
          inFileExplorer = false;
          inDeleteConfirm = false;
          selectedFile = fileList[fileIndex].name;
          if (loadKeyFromSD(selectedFile, &keyData1)) {
            OLED_printKey(&keyData1, selectedFile);
          } else {
            OLED_printError(F("Failed to load file"), true);
            delay(1000);
            inFileExplorer = true;
            OLED_printFileExplorer();
          }
        }
      }
      if (buttonBack.isHolded() && fileCount > 0 && SubFiles) {
        inDeleteConfirm = true;
        OLED_printDeleteConfirm();
      }
      if (buttonBack.isClick()) {
        if (currentDir != "/SubGHz") {
          int lastSlash = currentDir.lastIndexOf('/');
          currentDir = currentDir.substring(0, lastSlash);
          if (currentDir == "") currentDir = "/SubGHz";
          fileIndex = 0;
          fileCount = 0;
          loadFileList();
          OLED_printFileExplorer();
        } else {
          menuState = menuMain;
          inFileExplorer = false;
          inDeleteConfirm = false;
          SubFiles = false;
          selectedFile = "";
          OLED_printSubGHzMenu(display, menuIndex);
        }
      }
    } else if (menuState == menuTransmit && inFileExplorer && inDeleteConfirm) {
      if (buttonOK.isClick()) {
        String filePath = currentDir + "/" + fileList[fileIndex].name;
        if (SD.remove(filePath)) {
          display.clearDisplay();
          display.drawBitmap(5, 2, image_DolphinMafia_bits, 119, 62, SH110X_WHITE);
          display.setTextColor(SH110X_WHITE);
          display.setCursor(84, 15);
          display.print("Deleted");
          display.display();
          Serial.print(F("File deleted: "));
          Serial.println(filePath);
          delay(1000);
          fileCount = 0;
          fileIndex = 0;
          loadFileList();
          inDeleteConfirm = false;
          OLED_printFileExplorer();
        } else {
          OLED_printError(F("Failed to delete"), true);
          Serial.print(F("Failed to delete file: "));
          Serial.println(filePath);
          delay(1000);
          inDeleteConfirm = false;
          OLED_printFileExplorer();
        }
      }
      if (buttonBack.isClick()) {
        inDeleteConfirm = false;
        OLED_printFileExplorer();
      }
    } else if (menuState == menuTransmit && !inFileExplorer) {
      if (buttonOK.isClick()) {
        sendSynthKey(&keyData1);
        restoreReceiveMode();
      }
      if (buttonBack.isClick()) {
        inFileExplorer = true;
        inDeleteConfirm = false;
        OLED_printFileExplorer();
      }
    } else if (menuState == menuAnalyzer) {
      if (buttonBack.isClick()) {
        menuState = menuMain;
        SubFiles = false;
        inDeleteConfirm = false;
        rcswitch.disableReceive();
        restoreReceiveMode();
        OLED_printSubGHzMenu(display, menuIndex);
      } else if (millis() - scanTimer >= 250) {
        float detectedFrequency = 0.0;
        if (ELECHOUSE_cc1101.getRssi() >= rssi_threshold) {
          detectedFrequency = frequencies[current_scan_index];
          if (detectedFrequency != last_detected_frequency) {
            last_detected_frequency = detectedFrequency;
            Serial.print(F("Signal detected at "));
            Serial.print(detectedFrequency);
            Serial.println(F(" MHz"));
            OLED_printAnalyzer(true, last_detected_frequency);
          }
        }
        current_scan_index = (current_scan_index + 1) % numFrequencies;
        ELECHOUSE_cc1101.setMHZ(frequencies[current_scan_index]);
        ELECHOUSE_cc1101.SetRx();
        delayMicroseconds(3500);
        scanTimer = millis();
      }
    } else if (menuState == menuJammer) {
      if (buttonUp.isClick()) {
        stepFrequency(1);
        if (isJamming) {
          stopJamming();
          startJamming();
        }
        OLED_printJammer();
      }
      if (buttonDown.isClick()) {
        stepFrequency(-1);
        if (isJamming) {
          stopJamming();
          startJamming();
        }
        OLED_printJammer();
      }
      if (buttonOK.isClick()) {
        if (!isJamming) {
          startJamming();
          isJamming = true;
        } else {
          stopJamming();
          isJamming = false;
        }
        OLED_printJammer();
      }
      if (buttonBack.isClick()) {
        if (isJamming) {
          stopJamming();
          isJamming = false;
        }
        menuState = menuMain;
        SubFiles = false;
        inDeleteConfirm = false;
        restoreReceiveMode();
        OLED_printSubGHzMenu(display, menuIndex);
      }
    }
  }

  if (isJamming) {
    stopJamming();
    isJamming = false;
  }
  SubFiles = false;
  inDeleteConfirm = false;
  restoreReceiveMode();
}

void setupCC1101() {
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.setGDO0(CC1101_GDO0);
  ELECHOUSE_cc1101.SpiStrobe(0x30); // SRES
  delayMicroseconds(1000);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.SpiStrobe(0x36); // SCAL
  delayMicroseconds(2000);
  configureCC1101();
}

void configureCC1101() {
  ELECHOUSE_cc1101.setModulation(2); // ASK/OOK
  ELECHOUSE_cc1101.setMHZ(frequency);
  ELECHOUSE_cc1101.setRxBW(270.0);
  ELECHOUSE_cc1101.setDeviation(0);
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.SpiStrobe(0x36); // SCAL
  delayMicroseconds(2000);
  ELECHOUSE_cc1101.SetRx();
}

void restoreReceiveMode() {
  ELECHOUSE_cc1101.SpiStrobe(0x30); // SRES
  delayMicroseconds(1000);
  ELECHOUSE_cc1101.Init();
  configureCC1101();
  rcswitch.disableReceive();
  rcswitch.enableReceive(CC1101_GDO0);
}

void read_rcswitch(tpKeyData* kd) {
  uint32_t decoded = rcswitch.getReceivedValue();
  if (decoded) {
    Serial.println(F("RcSwitch signal captured"));
    signals++;
    kd->frequency = frequency;
    int numBytes = (kd->bitLength + 7) / 8;
    if (numBytes > 4) numBytes = 4;
    for (int i = 0; i < 8; i++) {
      kd->keyID[i] = (i < 8 - numBytes) ? 0 : (decoded >> ((numBytes - 1 - (i - (8 - numBytes))) * 8)) & 0xFF;
    }
    kd->type = (rcswitch.getReceivedProtocol() == 1 && rcswitch.getReceivedBitlength() == 24) ? kPrinceton : kRcSwitch;
    if (rcswitch.getReceivedBitlength() <= 40 && rcswitch.getReceivedProtocol() == 11) {
      kd->type = kCAME;
    }
    kd->te = rcswitch.getReceivedDelay();
    kd->bitLength = rcswitch.getReceivedBitlength();
    kd->codeLenth = kd->bitLength;
    strncpy(kd->preset, "0", sizeof(kd->preset) - 1);
    kd->preset[sizeof(kd->preset) - 1] = '\0';
    kd->rawData[0] = '\0';
    unsigned int* raw = rcswitch.getReceivedRawdata();
    String rawStr = "";
    for (int i = 0; i < kd->bitLength * 2 && i < 15; i++) {
      if (i > 0) rawStr += " ";
      int sign = (i % 2 == 0) ? 1 : -1;
      rawStr += String(sign * (int)raw[i]);
    }
    strncpy(kd->rawData, rawStr.c_str(), sizeof(kd->rawData) - 1);
    kd->rawData[sizeof(kd->rawData) - 1] = '\0';
    validKeyReceived = true;
    OLED_printKey(kd, "", false);
  }
  rcswitch.resetAvailable();
}

void read_raw(tpKeyData* kd) {
  delay(400);
  unsigned int* raw = rcswitch.getReceivedRawdata();
  uint32_t decoded = rcswitch.getReceivedValue();
  String data = "";
  int transitions = 0;
  for (transitions = 0; transitions < MAX_DATA_LOG && raw[transitions] != 0; transitions++) {
    if (transitions > 0) data += " ";
    int sign = (transitions % 2 == 0) ? 1 : -1;
    data += String(sign * (int)raw[transitions]);
  }
  if (transitions > 20) {
    Serial.println(F("Raw signal captured"));
    signals++;
    kd->frequency = frequency;
    if (data.length() >= sizeof(kd->rawData)) {
      data = data.substring(0, sizeof(kd->rawData) - 1);
    }
    strncpy(kd->rawData, data.c_str(), sizeof(kd->rawData) - 1);
    kd->rawData[sizeof(kd->rawData) - 1] = '\0';
    kd->type = kUnknown;
    kd->te = 0;
    kd->bitLength = 0;
    strncpy(kd->preset, "0", sizeof(kd->preset) - 1);
    kd->preset[sizeof(kd->preset) - 1] = '\0';
    kd->codeLenth = transitions;
    if (decoded) {
      int numBytes = (rcswitch.getReceivedBitlength() + 7) / 8;
      if (numBytes > 4) numBytes = 4;
      for (int i = 0; i < 8; i++) {
        kd->keyID[i] = (i < 8 - numBytes) ? 0 : (decoded >> ((numBytes - 1 - (i - (8 - numBytes))) * 8)) & 0xFF;
      }
      kd->type = (rcswitch.getReceivedProtocol() == 1 && rcswitch.getReceivedBitlength() == 24) ? kPrinceton : kRcSwitch;
      if (rcswitch.getReceivedBitlength() <= 40 && rcswitch.getReceivedProtocol() == 11) {
        kd->type = kCAME;
      }
      kd->te = rcswitch.getReceivedDelay();
      kd->bitLength = rcswitch.getReceivedBitlength();
      kd->codeLenth = kd->bitLength;
      strncpy(kd->preset, "0", sizeof(kd->preset) - 1);
      kd->preset[sizeof(kd->preset) - 1] = '\0';
    } else {
      if (transitions >= 129 && transitions <= 137) kd->type = kStarLine;
      else if (transitions >= 133 && transitions <= 137) kd->type = kKeeLoq;
      else if (transitions >= 40 && transitions <= 60) kd->type = kCAME;
    }
    validKeyReceived = true;
    OLED_printKey(kd, "", false);
  }
  rcswitch.resetAvailable();
}

void OLED_printWaitingSignal() {
  display.clearDisplay();
  display.drawBitmap(0, 4, image_DolphinReceive_bits, 97, 61, SH110X_WHITE);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(65, 38);
  display.print("Waiting");
  display.setCursor(65, 47);
  display.print("signal...");
  display.setCursor(72, 13);
  display.print(String(frequency, 2) + "MHz");
  display.display();
}

void OLED_printKey(tpKeyData* kd, String fileName, bool isSending) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(0, 0);
  if (fileName != "") {
    String st = fileName;
    if (st.length() > 16) st = st.substring(0, 16);
    display.print("File: " + st);
  } else {
    display.print("Signal:");
  }
  String st = "";
  bool leadingZero = true;
  for (int i = 0; i < 8; i++) {
    if (kd->keyID[i] != 0 || !leadingZero || i == 7) {
      leadingZero = false;
      if (kd->keyID[i] < 0x10) st += "0";
      st += String(kd->keyID[i], HEX);
      if (i < 7) st += ":";
    }
  }
  display.setCursor(0, 12);
  display.print("Code: " + st);
  st = "Type: " + getTypeName(kd->type);
  display.setCursor(0, 24);
  display.print(st);
  st = "Freq: " + String(kd->frequency) + " MHz";
  display.setCursor(0, 36);
  display.print(st);
  if (kd->bitLength > 0) {
    st = "Bits: " + String(kd->bitLength);
    display.setCursor(0, 48);
    display.print(st);
  }
  if (isSending) {
    display.setCursor(67, 54);
    display.print("Sending...");
    display.drawBitmap(109, 40, image_satellite_dish_bits, 15, 16, SH110X_WHITE);
  }
  display.display();
}

void OLED_printError(String st, bool err) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.print(err ? F("Error!") : F("OK"));
  display.setCursor(0, 12);
  display.print(st);
  display.display();
}

void OLED_printDeleteConfirm() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(0, 0);
  String name = fileList[fileIndex].name;
  if (name.length() > 16) name = name.substring(0, 16);
  display.print("File: " + name);
  display.setCursor(0, 24);
  display.print("Press OK to DELETE.");
  display.display();
}

bool saveKeyToSD(tpKeyData* kd) {
  if (!kd || kd->codeLenth == 0 || kd->frequency == 0.0) {
    Serial.println(F("Invalid key data"));
    return false;
  }
  int fileNum = 1;
  String fileName;
  do {
    fileName = currentDir + "/Signal_" + String(fileNum++) + ".sub";
  } while (SD.exists(fileName) && fileNum <= 999);
  File file = SD.open(fileName, FILE_WRITE);
  if (!file) {
    Serial.print(F("Failed to open file for writing: "));
    Serial.println(fileName);
    return false;
  }
  file.println(F("Filetype: Flipper SubGhz Key File"));
  file.println(F("Version: 1"));
  file.print(F("Frequency: "));
  file.println((unsigned long)(kd->frequency * 1000000));
  file.print(F("Protocol: "));
  file.println(getTypeName(kd->type));
  file.print(F("Bit: "));
  file.println(kd->bitLength);
  file.print(F("Key: "));
  for (int i = 0; i < 8; i++) {
    if (kd->keyID[i] < 0x10) file.print("0");
    file.print(kd->keyID[i], HEX);
    if (i < 7) file.print(" ");
  }
  file.println();
  file.print(F("TE: "));
  file.println(kd->te);
  file.close();
  Serial.print(F("Saved key to "));
  Serial.println(fileName);
  return true;
}

bool loadKeyFromSD(String fileName, tpKeyData* kd) {
  File file = SD.open(currentDir + "/" + fileName, FILE_READ);
  if (!file) {
    Serial.print(F("Failed to open file: "));
    Serial.println(fileName);
    return false;
  }
  memset(kd, 0, sizeof(tpKeyData));
  String line;
  while (file.available()) {
    line = file.readStringUntil('\n');
    line.trim();
    if (line.startsWith("Filetype:") && !line.equals("Filetype: Flipper SubGhz Key File")) {
      file.close();
      Serial.print(F("Invalid file format: "));
      Serial.println(fileName);
      return false;
    } else if (line.startsWith("Frequency:")) {
      kd->frequency = line.substring(10).toFloat() / 1000000.0;
    } else if (line.startsWith("Protocol:")) {
      String protocol = line.substring(9);
      protocol.trim();
      if (protocol == "Princeton") kd->type = kPrinceton;
      else if (protocol == "RcSwitch") kd->type = kRcSwitch;
      else if (protocol == "CAME") kd->type = kCAME;
      else if (protocol == "NICE") kd->type = kNICE;
      else if (protocol == "HOLTEK") kd->type = kHOLTEK;
      else if (protocol == "KeeLoq") kd->type = kKeeLoq;
      else if (protocol == "StarLine") kd->type = kStarLine;
      else kd->type = kUnknown;
    } else if (line.startsWith("Bit:")) {
      kd->bitLength = line.substring(4).toInt();
      kd->codeLenth = kd->bitLength;
    } else if (line.startsWith("Key:")) {
      String keyStr = line.substring(4);
      keyStr.trim();
      keyStr.replace(" ", "");
      for (int i = 0; i < keyStr.length() / 2 && i < 8; i++) {
        String byteStr = keyStr.substring(i * 2, i * 2 + 2);
        kd->keyID[i] = strtol(byteStr.c_str(), NULL, 16);
      }
    } else if (line.startsWith("TE:")) {
      kd->te = line.substring(3).toInt();
    }
  }
  file.close();
  Serial.print(F("Loaded key from "));
  Serial.println(fileName);
  return true;
}

void loadFileList() {
  fileCount = 0;
  File dir = SD.open(currentDir);
  if (!dir) {
    Serial.print(F("Failed to open directory: "));
    Serial.println(currentDir);
    return;
  }
  // First collect directories
  while (fileCount < MAX_FILES) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (entry.isDirectory()) {
      fileList[fileCount].name = entry.name();
      fileList[fileCount].isDir = true;
      fileCount++;
    }
    entry.close();
  }
  // Then collect .sub files
  dir.rewindDirectory();
  while (fileCount < MAX_FILES) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory() && String(entry.name()).endsWith(".sub")) {
      fileList[fileCount].name = entry.name();
      fileList[fileCount].isDir = false;
      fileCount++;
    }
    entry.close();
  }
  dir.close();
  Serial.print(F("Found "));
  Serial.print(fileCount);
  Serial.println(F(" entries"));
}

void OLED_printFileExplorer() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setTextColor(SH110X_WHITE);

  display.setCursor(0, 0);
  display.print(currentDir);

  display.setCursor(0, 12);
  display.println(F("---------------------"));

  if (fileCount == 0) {
    display.setCursor(0, 24);
    display.println(F("No files."));
    display.display();
    return;
  }

  const int perPage = 4;
  int maxStart = (fileCount > perPage) ? (fileCount - perPage) : 0;
  int startIndex = fileIndex - 1; // чтобы активный элемент был не внизу
  if (startIndex < 0) startIndex = 0;
  if (startIndex > maxStart) startIndex = maxStart;

  int itemsToShow = fileCount - startIndex;
  if (itemsToShow > perPage) itemsToShow = perPage;

  const int baseRow = 2; // строки с 22px: 0: header, 1: line, 2..5: items

  for (int i = 0; i < itemsToShow; i++) {
    int idx = startIndex + i;
    String name = fileList[idx].name;
    if (fileList[idx].isDir) {
      // каталоги без скобок (как в Bluetooth.ino)
      // name = "[" + name + "]"; // если надо — раскомментируйте
    }
    if (name.length() > 18) name = name.substring(0, 18);

    int y = (i + baseRow) * 11;

    if (idx == fileIndex) {
      display.fillRect(0, y - 1, 128, 11, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }
    display.setCursor(0, y);
    display.println(name);
  }

  display.display();
}


void sendSynthKey(tpKeyData* kd) {
  Serial.println(F("Starting transmission"));
  OLED_printKey(kd, selectedFile, true);

  Serial.print(F("Transmitting key, protocol: "));
  Serial.print(getTypeName(kd->type));
  Serial.print(F(", ID: "));
  for (byte i = 0; i < 8; i++) {
    if (kd->keyID[i] < 0x10) Serial.print("0");
    Serial.print(kd->keyID[i], HEX);
    if (i < 7) Serial.print(" ");
  }
  Serial.print(F(", Freq: "));
  Serial.print(kd->frequency);
  Serial.println(F(" MHz"));

  uint32_t frequency = kd->frequency * 1000000;
  String protocol = getTypeName(kd->type);
  String data = String(kd->rawData);
  uint64_t key = 0;
  for (int i = 0; i < 8; i++) {
    key |= ((uint64_t)kd->keyID[i] << ((7 - i) * 8));
  }
  byte modulation = 2; // ASK/OOK
  float rxBW = 270.0;
  float deviation = 0;
  float dataRate = 10;

  int rcswitch_protocol_no = 1; // Default protocol

  if (!initRfModule("tx", kd->frequency)) {
    Serial.println(F("Failed to initialize RF module"));
    OLED_printError(F("RF init failed"), true);
    delay(1000);
    OLED_printKey(kd, selectedFile);
    return;
  }

  if (bruceConfig.rfModule == CC1101_SPI_MODULE) {
    ELECHOUSE_cc1101.setModulation(modulation);
    ELECHOUSE_cc1101.setRxBW(rxBW);
    ELECHOUSE_cc1101.setDeviation(deviation);
    ELECHOUSE_cc1101.setDRate(dataRate);
    pinMode(bruceConfigPins.CC1101_bus.io0, OUTPUT);
    ELECHOUSE_cc1101.setPA(12);
    ELECHOUSE_cc1101.SetTx();
  } else {
    if (modulation != 2) {
      Serial.print("unsupported modulation: ");
      Serial.println(modulation);
      OLED_printError(F("Unsupported modulation"), true);
      delay(1000);
      OLED_printKey(kd, selectedFile);
      deinitRfModule();
      return;
    }
    initRfModule("tx", kd->frequency);
  }

  bool transmissionSuccess = false;

  if (protocol == "RAW") {
    int buff_size = 0;
    int index = 0;
    while (index >= 0) {
      index = data.indexOf(' ', index + 1);
      buff_size++;
    }
    int *transmittimings = (int *)calloc(sizeof(int), buff_size + 1);
    if (!transmittimings) {
      Serial.println(F("Memory allocation failed"));
      OLED_printError(F("Memory error"), true);
      delay(1000);
      OLED_printKey(kd, selectedFile);
      deinitRfModule();
      return;
    }

    int startIndex = 0;
    index = 0;
    for (size_t i = 0; i < buff_size; i++) {
      index = data.indexOf(' ', startIndex);
      if (index == -1) {
        transmittimings[i] = data.substring(startIndex).toInt();
      } else {
        transmittimings[i] = data.substring(startIndex, index).toInt();
      }
      startIndex = index + 1;
    }
    transmittimings[buff_size] = 0;

    display.setCursor(67, 54);
    display.print("Sending...");
    display.display();
    RCSwitch_RAW_send(transmittimings);
    free(transmittimings);
    transmissionSuccess = true;
  } else if (protocol == "RcSwitch") {
    data.replace(" ", "");
    uint64_t data_val = key;
    int bits = kd->bitLength;
    int pulse = kd->te;
    int repeat = 10;
    display.setCursor(67, 54);
    display.print("Sending...");
    display.display();
    RCSwitch_send(data_val, bits, pulse, rcswitch_protocol_no, repeat);
    transmissionSuccess = true;
  } else if (protocol == "Princeton") {
    RCSwitch_send(key, kd->bitLength, 350, 1, 10);
    transmissionSuccess = true;
  } else {
    Serial.print("unsupported protocol: ");
    Serial.println(protocol);
    Serial.println("Sending RcSwitch 11 protocol");
    RCSwitch_send(key, kd->bitLength, 270, 11, 10);
    transmissionSuccess = true;
  }

  deinitRfModule();

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(15, 47);
  if (transmissionSuccess) {
    display.print("Successfully!");
    Serial.println(F("Transmission completed successfully"));
  } else {
    display.print("Failed!");
    Serial.println(F("Transmission failed"));
  }
  display.drawBitmap(15, 12, image_Connected_bits, 62, 31, SH110X_WHITE);
  display.display();
  delay(1000);
  OLED_printKey(kd, selectedFile);
}

void RCSwitch_send(uint64_t data, unsigned int bits, int pulse, int protocol, int repeat) {
  RCSwitch mySwitch = RCSwitch();

  int txPin = bruceConfig.rfTx;
  if (bruceConfig.rfModule == CC1101_SPI_MODULE) {
    txPin = bruceConfigPins.CC1101_bus.io0;
  }
  mySwitch.enableTransmit(txPin);

  mySwitch.setProtocol(protocol);
  if (pulse) { mySwitch.setPulseLength(pulse); }
  int rep = repeat > 0 ? repeat : 6;
  if (rep > 6) rep = 6; // keep under ~1s
  mySwitch.setRepeatTransmit(rep);
  mySwitch.send(data, bits);

  mySwitch.disableTransmit();
}

void RCSwitch_RAW_send(int *ptrtransmittimings) {
  int nTransmitterPin = bruceConfig.rfTx;
  if (bruceConfig.rfModule == CC1101_SPI_MODULE) {
    nTransmitterPin = bruceConfigPins.CC1101_bus.io0;
  }

  if (!ptrtransmittimings) return;

  // Determine if timings include negatives
  bool hasNeg = false;
  unsigned long sum_us = 0;
  for (int i = 0; ptrtransmittimings[i]; ++i) {
    if (ptrtransmittimings[i] < 0) hasNeg = true;
    int v = ptrtransmittimings[i] >= 0 ? ptrtransmittimings[i] : -ptrtransmittimings[i];
    sum_us += (unsigned long)v;
  }
  int nRepeatTransmit = 1;
  if (sum_us > 0) {
    nRepeatTransmit = (int)(900000UL / sum_us);
    if (nRepeatTransmit < 1) nRepeatTransmit = 1;
    if (nRepeatTransmit > 6) nRepeatTransmit = 6;
  }

  for (int nRepeat = 0; nRepeat < nRepeatTransmit; nRepeat++) {
    unsigned int currenttiming = 0;
    bool level = true; // used for unsigned case
    while (ptrtransmittimings[currenttiming]) {
      int dur = ptrtransmittimings[currenttiming];
      bool lvl;
      unsigned int t;
      if (hasNeg) {
        lvl = (dur >= 0);
        t = (unsigned int)(dur >= 0 ? dur : -dur);
      } else {
        // alternate HIGH/LOW starting with HIGH
        lvl = level;
        t = (unsigned int)(dur >= 0 ? dur : -dur);
        level = !level;
      }
      digitalWrite(nTransmitterPin, lvl ? HIGH : LOW);
      delayMicroseconds(t);
      currenttiming++;
    }
    digitalWrite(nTransmitterPin, LOW);
    delayMicroseconds(8000); // interframe gap
  }
}

void stepFrequency(int step) {
  freqIndex = (freqIndex + step + numFrequencies) % numFrequencies;
  frequency = frequencies[freqIndex];
}

void OLED_printAnalyzer(bool signalReceived, float detectedFreq) {
  display.clearDisplay();
  display.drawBitmap(0, 7, image_Dolphin_MHz_bits, 108, 57, SH110X_WHITE);
  display.drawBitmap(100, 6, image_MHz_1_bits, 25, 11, SH110X_WHITE);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(62, 10);
  if (signalReceived || detectedFreq != 0.0) {
    char freq_str[7];
    snprintf(freq_str, sizeof(freq_str), "%06.2f", detectedFreq);
    display.print(freq_str);
  } else {
    display.print("000.00");
  }
  display.display();
}

void OLED_printJammer() {
  display.clearDisplay();
  display.drawBitmap(0, 3, image_Dolphin_Send_bits, 97, 61, SH110X_WHITE);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(78, 12);
  display.print(String(frequency, 2));
  display.setCursor(65, 42);
  display.print(isJamming ? "Jamming..." : "Press OK");
  if (!isJamming) {
    display.setCursor(65, 51);
    display.print("to start");
  }
  display.display();
}

void startJamming() {
  Serial.println(F("Starting jammer"));
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(0);
  ELECHOUSE_cc1101.setMHZ(frequency);
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.setDeviation(0);
  ELECHOUSE_cc1101.setRxBW(270.0);
  ELECHOUSE_cc1101.SetTx();
  ELECHOUSE_cc1101.SpiWriteReg(0x3E, 0xFF);
  ELECHOUSE_cc1101.SpiWriteReg(0x35, 0x60);
}

void stopJamming() {
  Serial.println(F("Stopping jammer"));
  ELECHOUSE_cc1101.SpiWriteReg(0x35, 0x00);
  ELECHOUSE_cc1101.SetRx();
  restoreReceiveMode();
}

String getTypeName(emKeys tp) {
  switch (tp) {
    case kUnknown: return F("Unknown");
    case kP12bt: return F("Pre 12bit");
    case k12bt: return F("12bit");
    case k24bt: return F("24bit");
    case k64bt: return F("64bit");
    case kKeeLoq: return F("KeeLoq");
    case kANmotors64: return F("ANmotors");
    case kPrinceton: return F("Princeton");
    case kRcSwitch: return F("RcSwitch");
    case kStarLine: return F("StarLine");
    case kCAME: return F("CAME");
    case kNICE: return F("NICE");
    case kHOLTEK: return F("HOLTEK");
    default: return "";
  }
}
