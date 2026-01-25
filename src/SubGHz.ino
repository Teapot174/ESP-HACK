#include "subghz.h"
#include "menu/subghz.h"
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

// Bruteforce state
const char* bruteTypes[] = {"Came", "Nice", "Ansonic", "Holtek", "Chamberlain"};
const int BRUTE_TYPE_COUNT = 5;
const float bruteFreqOptions[] = {315.0, 433.92, 868.0, 915.0};
const char* bruteFreqLabels[] = {"315.00MHz", "433.92MHz", "868.00MHz", "915.00MHz"};
const int BRUTE_FREQ_COUNT = 4;
const int bruteBits = 12;
int bruteTypeIndex = 0;
int bruteFreqIndex = 1;
int bruteConfigSelection = 0;
uint16_t bruteProgress = 0;
const uint16_t bruteTotal = 4096;
unsigned long bruteLastStep = 0;
bool bruteRunning = false;
bool bruteRfActive = false;
int bruteTxPin = CC1101_GDO0;

const int cameZero[] = {-320, 640};
const int cameOne[] = {-640, 320};
const int camePilot[] = {-11520, 320};
const BruteProtocol protoCame = {cameZero, 2, cameOne, 2, camePilot, 2, nullptr, 0};

const int niceZero[] = {-700, 1400};
const int niceOne[] = {-1400, 700};
const int nicePilot[] = {-25200, 700};
const BruteProtocol protoNice = {niceZero, 2, niceOne, 2, nicePilot, 2, nullptr, 0};

const int ansonicZero[] = {-1111, 555};
const int ansonicOne[] = {-555, 1111};
const int ansonicPilot[] = {-19425, 555};
const BruteProtocol protoAnsonic = {ansonicZero, 2, ansonicOne, 2, ansonicPilot, 2, nullptr, 0};

const int holtekZero[] = {-870, 430};
const int holtekOne[] = {-430, 870};
const int holtekPilot[] = {-15480, 430};
const BruteProtocol protoHoltek = {holtekZero, 2, holtekOne, 2, holtekPilot, 2, nullptr, 0};

const int chamberZero[] = {-870, 430};
const int chamberOne[] = {-430, 870};
const int chamberStop[] = {-3000, 1000};
const BruteProtocol protoChamber = {chamberZero, 2, chamberOne, 2, nullptr, 0, chamberStop, 2};
const BruteProtocol* currentBruteProto = nullptr;

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
int nextSignalIndex = 1;
bool nextSignalIndexReady = false;

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
void resetButtonStates();
void OLED_printBruteIntro();
void OLED_printBruteConfig();
void OLED_printBruteProgress(uint16_t progress, uint16_t total);
bool bruteInitTx();
void bruteStopTx();
void bruteSendCode(uint16_t code);

bool initRfModule(String mode, float freq) {
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.setGDO0(CC1101_GDO0);
  ELECHOUSE_cc1101.SpiStrobe(0x30);
  delayMicroseconds(1000);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setMHZ(freq);
  ELECHOUSE_cc1101.setModulation(2);
  if (mode == "tx" || mode == "TX") {
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setCrc(0);
    ELECHOUSE_cc1101.setPktFormat(3);
    ELECHOUSE_cc1101.SetTx();
    pinMode(CC1101_GDO0, OUTPUT);
    digitalWrite(CC1101_GDO0, LOW);
  } else {
    ELECHOUSE_cc1101.setPktFormat(0);
    ELECHOUSE_cc1101.SetRx();
  }
  ELECHOUSE_cc1101.SpiStrobe(0x33);
  delayMicroseconds(2000);
  return true;
}

void deinitRfModule() {
  ELECHOUSE_cc1101.SpiStrobe(0x36);
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
      byte lastMenuIndex = SUBGHZ_MENU_ITEM_COUNT - 1;
      if (buttonUp.isClick()) {
        menuIndex = (menuIndex == 0) ? lastMenuIndex : menuIndex - 1;
        OLED_printSubGHzMenu(display, menuIndex);
      }
      if (buttonDown.isClick()) {
        menuIndex = (menuIndex == lastMenuIndex) ? 0 : menuIndex + 1;
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
          currentDir = "/SubGHz";
          loadFileList();
          OLED_printFileExplorer();
          resetButtonStates();
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
        } else if (menuIndex == 4) {
          menuState = menuBruteforce;
          inDeleteConfirm = false;
          bruteRunning = false;
          OLED_printBruteIntro();
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
          Serial.println(F("Key saved"));
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
        resetButtonStates();
        OLED_printSubGHzMenu(display, menuIndex);
      }
      if (rcswitch.available()) {
        Serial.println(F(" Signal:"));
        if (!readRAW) read_rcswitch(&keyData1);
        else read_raw(&keyData1);
        if (validKeyReceived && autoSave && (lastSavedKey != keyData1.keyID[0] || keyData1.keyID[0] == 0)) {
          if (saveKeyToSD(&keyData1)) {
            lastSavedKey = keyData1.keyID[0];
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
          resetButtonStates();
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
        resetButtonStates();
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
          resetButtonStates();
          OLED_printSubGHzMenu(display, menuIndex);
        }
      }
    } else if (menuState == menuTransmit && inFileExplorer && inDeleteConfirm) {
      
      if (buttonUp.isClick() || buttonDown.isClick()) {
      }
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
          for (int i = fileIndex; i < fileCount - 1; i++) {
            fileList[i] = fileList[i + 1];
          }
          if (fileCount > 0) {
            fileCount--;
          }
          if (fileIndex >= fileCount && fileCount > 0) {
            fileIndex = fileCount - 1;
          } else if (fileCount == 0) {
            fileIndex = 0;
          }
          delay(1000);
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
        resetButtonStates();
        OLED_printFileExplorer();
      }
    } else if (menuState == menuBruteforce) {
      if (buttonOK.isHolded()) {
        bruteConfigSelection = 0;
        menuState = menuBruteConfig;
        OLED_printBruteConfig();
      } else if (buttonOK.isClick()) {
        bruteProgress = 0;
        freqIndex = bruteFreqIndex;
        frequency = bruteFreqOptions[bruteFreqIndex];
        bruteLastStep = millis();
        if (bruteInitTx()) {
          bruteRunning = true;
          menuState = menuBruteRun;
          OLED_printBruteProgress(bruteProgress, bruteTotal);
        } else {
          bruteRunning = false;
          OLED_printError(F("Brute init fail"), true);
          delay(800);
          OLED_printBruteIntro();
        }
      }
      if (buttonBack.isClick()) {
        menuState = menuMain;
        SubFiles = false;
        inDeleteConfirm = false;
        bruteRunning = false;
        resetButtonStates();
        OLED_printSubGHzMenu(display, menuIndex);
      }
    } else if (menuState == menuBruteConfig) {
      if (buttonUp.isClick()) {
        bruteConfigSelection = (bruteConfigSelection == 0) ? 1 : bruteConfigSelection - 1;
        OLED_printBruteConfig();
      }
      if (buttonDown.isClick()) {
        bruteConfigSelection = (bruteConfigSelection + 1) % 2;
        OLED_printBruteConfig();
      }
      if (buttonOK.isClick()) {
        if (bruteConfigSelection == 0) {
          bruteTypeIndex = (bruteTypeIndex + 1) % BRUTE_TYPE_COUNT;
        } else {
          bruteFreqIndex = (bruteFreqIndex + 1) % BRUTE_FREQ_COUNT;
        }
        OLED_printBruteConfig();
      }
      if (buttonBack.isClick()) {
        freqIndex = bruteFreqIndex;
        frequency = bruteFreqOptions[bruteFreqIndex];
        menuState = menuBruteforce;
        resetButtonStates();
        OLED_printBruteIntro();
      }
    } else if (menuState == menuBruteRun) {
      if (buttonBack.isClick()) {
        bruteRunning = false;
        bruteStopTx();
        menuState = menuBruteforce;
        resetButtonStates();
        OLED_printBruteIntro();
      }
      if (buttonOK.isClick()) {
        bruteRunning = false;
        bruteStopTx();
        menuState = menuBruteforce;
        resetButtonStates();
        OLED_printBruteIntro();
      }
      if (bruteRunning && millis() - bruteLastStep >= 15) {
        bruteLastStep = millis();
        if (bruteProgress < bruteTotal) {
          bruteSendCode(bruteProgress);
          bruteProgress++;
          if (bruteProgress % 8 == 0 || bruteProgress == bruteTotal) {
            OLED_printBruteProgress(bruteProgress, bruteTotal);
          }
        }
        if (bruteProgress >= bruteTotal) {
          bruteRunning = false;
          bruteStopTx();
          menuState = menuBruteforce;
          resetButtonStates();
          OLED_printBruteIntro();
        }
      }
    } else if (menuState == menuAnalyzer) {
      if (buttonBack.isClick()) {
        menuState = menuMain;
        SubFiles = false;
        inDeleteConfirm = false;
        rcswitch.disableReceive();
        restoreReceiveMode();
        resetButtonStates();
        OLED_printSubGHzMenu(display, menuIndex);
      } else if (millis() - scanTimer >= 250) {
        float detectedFrequency = 0.0;
        if (ELECHOUSE_cc1101.getRssi() >= rssi_threshold) {
          detectedFrequency = frequencies[current_scan_index];
          if (detectedFrequency != last_detected_frequency) {
            last_detected_frequency = detectedFrequency;
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
        resetButtonStates();
        OLED_printSubGHzMenu(display, menuIndex);
      }
    }
  }

  if (isJamming) {
    stopJamming();
    isJamming = false;
  }
  if (bruteRfActive) {
    bruteStopTx();
  }
  SubFiles = false;
  inDeleteConfirm = false;
  restoreReceiveMode();
}

void resetButtonStates() {
  buttonUp.resetStates();
  buttonDown.resetStates();
  buttonOK.resetStates();
  buttonBack.resetStates();
}

void setupCC1101() {
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.setGDO0(CC1101_GDO0);
  ELECHOUSE_cc1101.SpiStrobe(0x30);
  delayMicroseconds(1000);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.SpiStrobe(0x36);
  delayMicroseconds(2000);
  configureCC1101();
}

void configureCC1101() {
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setMHZ(frequency);
  ELECHOUSE_cc1101.setRxBW(270.0);
  ELECHOUSE_cc1101.setDeviation(0);
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.SpiStrobe(0x36);
  delayMicroseconds(2000);
  ELECHOUSE_cc1101.SetRx();
}

void restoreReceiveMode() {
  ELECHOUSE_cc1101.SpiStrobe(0x30);
  delayMicroseconds(1000);
  ELECHOUSE_cc1101.Init();
  configureCC1101();
  rcswitch.disableReceive();
  rcswitch.enableReceive(CC1101_GDO0);
}

void read_rcswitch(tpKeyData* kd) {
  uint32_t decoded = rcswitch.getReceivedValue();
  if (decoded) {
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
  display.setCursor(1, 1);
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
  display.setCursor(1, 12);
  display.print("Code: " + st);
  st = "Type: " + getTypeName(kd->type);
  display.setCursor(1, 24);
  display.print(st);
  st = "Freq: " + String(kd->frequency) + " MHz";
  display.setCursor(1, 36);
  display.print(st);
  if (kd->bitLength > 0) {
    st = "Bits: " + String(kd->bitLength);
    display.setCursor(1, 48);
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
  display.setCursor(1, 1);
  display.print(err ? F("Error!") : F("OK"));
  display.setCursor(1, 12);
  display.print(st);
  display.display();
}

void OLED_printDeleteConfirm() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(1, 1);
  String name = fileList[fileIndex].name;
  if (name.length() > 16) name = name.substring(0, 16);
  display.print("File: " + name);
  display.setCursor(1, 24);
  display.print("Press OK to DELETE.");
  display.display();
}

bool saveKeyToSD(tpKeyData* kd) {
  if (!kd || kd->codeLenth == 0 || kd->frequency == 0.0) {
    return false;
  }
  if (!nextSignalIndexReady) {
    int maxSignalIndex = 0;
    File dir = SD.open(currentDir);
    if (dir) {
      while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory() && String(entry.name()).endsWith(".sub")) {
          String entryName = entry.name();
          int lastSlash = entryName.lastIndexOf('/');
          if (lastSlash >= 0) entryName = entryName.substring(lastSlash + 1);
          if (entryName.startsWith("Signal_") && entryName.endsWith(".sub")) {
            String numStr = entryName.substring(7, entryName.length() - 4);
            int num = numStr.toInt();
            if (num > maxSignalIndex) maxSignalIndex = num;
          }
        }
        entry.close();
      }
      dir.close();
      nextSignalIndex = maxSignalIndex + 1;
      if (nextSignalIndex < 1) nextSignalIndex = 1;
      nextSignalIndexReady = true;
    }
  }
  int fileNum = (nextSignalIndex > 0) ? nextSignalIndex : 1;
  String fileName;
  while (fileNum <= 999) {
    fileName = currentDir + "/Signal_" + String(fileNum) + ".sub";
    if (!SD.exists(fileName)) break;
    fileNum++;
  }
  if (fileNum > 999) {
    fileNum = 1;
    while (fileNum <= 999) {
      fileName = currentDir + "/Signal_" + String(fileNum) + ".sub";
      if (!SD.exists(fileName)) break;
      fileNum++;
    }
  }
  if (fileNum > 999) {
    Serial.println(F("No free Signal_*.sub slots"));
    return false;
  }
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
  Serial.print(F("Saved to "));
  Serial.println(fileName);
  nextSignalIndex = fileNum + 1;
  nextSignalIndexReady = true;
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
  return true;
}

void loadFileList() {
  fileCount = 0;
  nextSignalIndexReady = false;
  File dir = SD.open(currentDir);
  if (!dir) {
    Serial.print(F("Failed to open directory: "));
    Serial.println(currentDir);
    return;
  }
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
  dir.rewindDirectory();
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory() && String(entry.name()).endsWith(".sub")) {
      String entryName = entry.name();
      if (fileCount < MAX_FILES) {
        fileList[fileCount].name = entryName;
        fileList[fileCount].isDir = false;
        fileCount++;
      }
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

  display.setCursor(1, 1);
  display.print(currentDir);

  display.setCursor(1, 12);
  display.println(F("---------------------"));

  if (fileCount == 0) {
    display.setCursor(1, 24);
    display.println(F("No files."));
    display.display();
    return;
  }

  const int perPage = 4;
  int maxStart = (fileCount > perPage) ? (fileCount - perPage) : 0;
  int startIndex = fileIndex - 1;
  if (startIndex < 0) startIndex = 0;
  if (startIndex > maxStart) startIndex = maxStart;

  int itemsToShow = fileCount - startIndex;
  if (itemsToShow > perPage) itemsToShow = perPage;

  const int baseRow = 2;

  for (int i = 0; i < itemsToShow; i++) {
    int idx = startIndex + i;
    String name = fileList[idx].name;
    if (fileList[idx].isDir) {
    }
    if (name.length() > 18) name = name.substring(0, 18);

    int y = (i + baseRow) * 11;

    if (idx == fileIndex) {
      display.fillRect(0, y - 1, 128, 11, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }
    display.setCursor(1, y);
    display.println(name);
  }

  display.display();
}

void OLED_printBruteIntro() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.setTextWrap(false);
  display.setCursor(69, 22);
  display.print(F("to config"));
  display.setCursor(76, 13);
  display.print(F("Hold OK"));
  display.drawBitmap(77, 36, image_AntLogo_bits, 38, 22, 1);
  display.drawBitmap(0, 10, image_DolphinWait_bits, 59, 54, 1);
  display.display();
}

void OLED_printBruteConfig() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.setTextWrap(false);

  display.setCursor(1, 1);
  display.print(F("Bruteforce"));
  display.setCursor(1, 10);
  display.print(F("====================="));

  display.setCursor(1, 24);
  display.print(bruteConfigSelection == 0 ? ">" : " ");
  display.print(F("Type: "));
  String typeLabel = String(bruteTypes[bruteTypeIndex]) + "-" + String(bruteBits);
  if (bruteTypeIndex != 4) typeLabel += F("bit");
  display.print(typeLabel);

  display.setCursor(1, 36);
  display.print(bruteConfigSelection == 1 ? ">" : " ");
  display.print(F("Freq: "));
  display.print(bruteFreqLabels[bruteFreqIndex]);

  display.display();
}

void OLED_printBruteProgress(uint16_t progress, uint16_t total) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.setTextWrap(false);
  int signalDigits = (progress < 10) ? 1 : (progress < 100) ? 2 : (progress < 1000) ? 3 : 4;
  int signalX = 69;
  switch (signalDigits) {
    case 1: signalX = 78; break;
    case 2: signalX = 75; break;
    case 3: signalX = 72; break;
    default: signalX = 69; break;
  }
  display.setCursor(signalX, 24);
  char buf[18];
  snprintf(buf, sizeof(buf), "%u/%u", progress, total);
  display.print(buf);
  display.drawBitmap(77, 36, image_AntLogoON_bits, 38, 22, 1);
  display.drawBitmap(0, 10, image_DolphinWait_bits, 59, 54, 1);
  uint16_t percent = (total == 0) ? 0 : (progress * 100) / total;
  int percentDigits = (percent < 10) ? 1 : (percent < 100) ? 2 : 3;
  int percentX = -1;
  switch (percentDigits) {
    case 1: percentX = 85; break;
    case 2: percentX = 79; break;
    case 3: percentX = 73; break;
    default: percentX = -1; break;
  }
  if (percentX >= 0) {
    display.setTextSize(2);
    display.setCursor(percentX, 6);
    if (percent > 100) percent = 100;
    snprintf(buf, sizeof(buf), "%u%%", percent);
    display.print(buf);
    display.setTextSize(1);
  }
  display.display();
}

const BruteProtocol* getBruteProtocolByIndex(int idx) {
  switch (idx) {
    case 0: return &protoCame;
    case 1: return &protoNice;
    case 2: return &protoAnsonic;
    case 3: return &protoHoltek;
    case 4: return &protoChamber;
    default: return nullptr;
  }
}

bool bruteInitTx() {
  currentBruteProto = getBruteProtocolByIndex(bruteTypeIndex);
  if (!currentBruteProto) return false;
  freqIndex = bruteFreqIndex;
  frequency = bruteFreqOptions[bruteFreqIndex];
  bruteTxPin = (bruceConfig.rfModule == CC1101_SPI_MODULE) ? bruceConfigPins.CC1101_bus.io0 : bruceConfig.rfTx;
  if (!initRfModule("tx", frequency)) return false;
  pinMode(bruteTxPin, OUTPUT);
  digitalWrite(bruteTxPin, LOW);
  bruteRfActive = true;
  return true;
}

void bruteStopTx() {
  if (!bruteRfActive) return;
  digitalWrite(bruteTxPin, LOW);
  deinitRfModule();
  bruteRfActive = false;
}

void bruteSendSequence(const int* seq, size_t len) {
  if (!seq || len == 0) return;
  for (size_t i = 0; i < len; i++) {
    int duration = seq[i];
    bool levelHigh = duration > 0;
    unsigned int delayVal = (duration > 0) ? duration : -duration;
    digitalWrite(bruteTxPin, levelHigh ? HIGH : LOW);
    delayMicroseconds(delayVal);
  }
}

void bruteSendCode(uint16_t code) {
  if (!bruteRfActive || !currentBruteProto) return;
  const BruteProtocol* p = currentBruteProto;

  bruteSendSequence(p->pilot, p->pilotLen);
  for (int bit = bruteBits - 1; bit >= 0; --bit) {
    bool set = (code >> bit) & 0x1;
    bruteSendSequence(set ? p->one : p->zero, set ? p->oneLen : p->zeroLen);
  }
  bruteSendSequence(p->stop, p->stopLen);
  digitalWrite(bruteTxPin, LOW);
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
  byte modulation = 2;
  float rxBW = 270.0;
  float deviation = 0;
  float dataRate = 10;

  int rcswitch_protocol_no = 1;

  if (!initRfModule("tx", kd->frequency)) {
    Serial.println(F("Failed to initialize CC1101"));
    display.setTextColor(1);
    display.setTextWrap(false);
    display.setCursor(8, 20);
    display.print("CC1101 init failed.");
    display.setCursor(29, 32);
    display.print("ERROR: 0x001");
    display.display();
    OLED_printError(F("CC1101 init Failed."), true);
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
    Serial.print("Unsupported protocol: ");
    Serial.println(protocol);
    RCSwitch_send(key, kd->bitLength, 270, 11, 10);
    transmissionSuccess = true;
  }

  deinitRfModule();

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
  if (rep > 6) rep = 6;
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
    bool level = true;
    while (ptrtransmittimings[currenttiming]) {
      int dur = ptrtransmittimings[currenttiming];
      bool lvl;
      unsigned int t;
      if (hasNeg) {
        lvl = (dur >= 0);
        t = (unsigned int)(dur >= 0 ? dur : -dur);
      } else {
        lvl = level;
        t = (unsigned int)(dur >= 0 ? dur : -dur);
        level = !level;
      }
      digitalWrite(nTransmitterPin, lvl ? HIGH : LOW);
      delayMicroseconds(t);
      currenttiming++;
    }
    digitalWrite(nTransmitterPin, LOW);
    delayMicroseconds(8000);
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
  display.setCursor(72, 13);
  display.print(String(frequency, 2) + "MHz");
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
