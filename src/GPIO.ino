#include "menu/gpio.h"
#include "menu/subghz.h"
#include "Explorer.h"
#include "CONFIG.h"
#include "display.h"
#include <GyverButton.h>
#include <SPI.h>
#include <RF24.h>
#include <SD.h>
#include <OneWire.h>

extern DisplayType display;
extern GButton buttonUp, buttonDown, buttonOK, buttonBack;
extern bool inMenu;
extern byte currentMenu, gpioMenuIndex;
extern SPIClass sdSPI;

// NRF24
RF24 radio(CC1101_CS, CC1101_GDO0);
SPIClass *NRFSPI = &SPI;
bool inNRF24Submenu = false, inJammingMenu = false, inJammingActive = false, inNRF24Config = false;
byte nrf24MenuIndex = 0, nrf24ConfigIndex = 0, jammingModeIndex = 0;
const char* nrf24MenuItems[] = {"Jammer", "Spectrum", "Config"};
const char* jammingModes[] = {"WiFi", "BLE", "BT", "USB", "Video", "RadioCH", "ALL"};
const byte NRF24_MENU_ITEM_COUNT = 3, JAMMING_MODE_COUNT = 7;
const char* NRF24_CONFIG_PATH = "/GPIO/NRF24.cfg";

// Spectrum Analyzer
#define SPECTRUM_CHANNELS 128
uint8_t spectrumValues[SPECTRUM_CHANNELS];
bool inSpectrumAnalyzer = false;
unsigned long lastSpectrumUpdate = 0;
const unsigned long SPECTRUM_UPDATE_INTERVAL = 25; // 1000/25 ГЦ

// NRF24 pins
struct NRF24Config {
  byte cePin = GPIO_B, csnPin = GPIO_C, mosiPin = GPIO_E, misoPin = GPIO_F, sckPin = GPIO_D;
} nrf24Config;

// Pins
const byte availablePins[] = {GPIO_A, GPIO_B, GPIO_C, GPIO_D, GPIO_E, GPIO_F};
const char* pinNames[] = {"A", "B", "C", "D", "E", "F"};
const byte AVAILABLE_PINS_COUNT = 6;

// iButton pins
const byte iButtonPins[] = {GPIO_A, GPIO_B, GPIO_C, GPIO_D, GPIO_E, GPIO_F};
const char* iButtonPinNames[] = {"A", "B", "C", "D", "E", "F"};
const byte IBUTTON_PINS_COUNT = 6;

// iButton
static const byte IBUTTON_MENU_ITEM_COUNT = 2;
static const char* iButtonMenuItems[] = {"Read", "Write"};
static const char* IBUTTON_DIR = "/iButton";
static const int IBUTTON_MAX_FILES = 50;

enum IButtonState {
  IBUTTON_MENU,
  IBUTTON_READ_WAIT,
  IBUTTON_READ_DETECTED,
  IBUTTON_WRITE_BROWSE,
  IBUTTON_WRITE_WAIT
};

bool inIButtonSubmenu = false;
IButtonState iButtonState = IBUTTON_MENU;
byte iButtonMenuIndex = 0;
byte iButtonPinIndex = 0; // default A
byte iButtonPin = GPIO_A;
OneWire* iButtonWire = nullptr;
byte iButtonBuffer[8] = {0};
byte iButtonType = 0x00;
uint8_t iButtonBits = 64;
bool iButtonWasPresent = false;
bool iButtonCrcOk = false;

static const char* iButtonExts[] = {".ibtn"};
ExplorerEntry iButtonFileList[IBUTTON_MAX_FILES];
ExplorerState iButtonExplorer;
ExplorerConfig iButtonExplorerCfg = {IBUTTON_DIR, iButtonExts, 1, true, false, true, true};

// Channels NRF24
byte wifi_channels[] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 22, 24, 26, 28, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 46, 48, 50, 52, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68};
byte ble_channels[] = {1, 2, 3, 25, 26, 27, 79, 80, 81};
byte bluetooth_channels[] = {2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80};
byte usb_channels[] = {40, 50, 60};
byte video_channels[] = {70, 75, 80};
byte rc_channels[] = {1, 3, 5, 7};
byte full_channels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100};

void runSpectrumAnalyzer() {
  if (!inSpectrumAnalyzer) return;
  
  unsigned long currentTime = millis();
  if (currentTime - lastSpectrumUpdate < SPECTRUM_UPDATE_INTERVAL) {
    return;
  }
  lastSpectrumUpdate = currentTime;
  
  for (int i = 0; i < SPECTRUM_CHANNELS; i++) {
    radio.setChannel(i);
    bool carrier = radio.testCarrier();
    
    if (carrier) {
      if (spectrumValues[i] < 100) spectrumValues[i] += 8; 
    } else {
      if (spectrumValues[i] > 0) spectrumValues[i] -= 2;
    }
  }
  
  display.clearDisplay();
  
  uint8_t maxVal = 0;
  for (int i = 0; i < SPECTRUM_CHANNELS; i++) {
    if (spectrumValues[i] > maxVal) maxVal = spectrumValues[i];
  }
  
  if (maxVal > 0) {
    for (int i = 0; i < SPECTRUM_CHANNELS; i++) {
      int barWidth = 128 / SPECTRUM_CHANNELS;
      int x = i * barWidth;
      
      int height = map(spectrumValues[i], 0, maxVal, 0, 50);
      
      if (height > 0) {
        for (int h = 0; h < height; h++) {
          int y = 63 - h;
          display.drawPixel(x, y, SH110X_WHITE);
          if (barWidth > 1) display.drawPixel(x + 1, y, SH110X_WHITE);
        }
      }
    }
  }
  
  // Spectrum freq
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(3, 1);
  display.print(F("2.4"));
  display.setCursor(51, 1);
  display.print(F("2.45"));
  display.setCursor(108, 1);
  display.print(F("2.5"));
  
  display.display();
}

void saveNRF24Config() {
  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println(F("SD init failed"));
    return;
  }
  if (!SD.exists("/GPIO")) SD.mkdir("/GPIO");
  SD.remove(NRF24_CONFIG_PATH);
  File file = SD.open(NRF24_CONFIG_PATH, FILE_WRITE);
  if (file) {
    const char* labels[] = {"CE=", "CSN=", "MOSI=", "MISO=", "SCK="};
    byte* pins[] = {&nrf24Config.cePin, &nrf24Config.csnPin, &nrf24Config.mosiPin, &nrf24Config.misoPin, &nrf24Config.sckPin};
    for (byte i = 0; i < 5; i++) {
      for (byte j = 0; j < AVAILABLE_PINS_COUNT; j++) {
        if (*pins[i] == availablePins[j]) {
          file.print(labels[i]);
          file.println(pinNames[j]);
          break;
        }
      }
    }
    file.close();
    Serial.println(F("NRF24 config saved"));
  } else {
    Serial.println(F("Error saving NRF24 config"));
  }
}

bool loadNRF24ConfigFromFile(const char* path) {
  File file = SD.open(path, FILE_READ);
  if (!file) return false;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    byte* pin = nullptr;
    byte offset = 0;
    if (line.startsWith("CE=")) { pin = &nrf24Config.cePin; offset = 3; }
    else if (line.startsWith("CSN=")) { pin = &nrf24Config.csnPin; offset = 4; }
    else if (line.startsWith("MOSI=")) { pin = &nrf24Config.mosiPin; offset = 5; }
    else if (line.startsWith("MISO=")) { pin = &nrf24Config.misoPin; offset = 5; }
    else if (line.startsWith("SCK=")) { pin = &nrf24Config.sckPin; offset = 4; }
    if (pin) {
      String pinName = line.substring(offset);
      for (byte i = 0; i < AVAILABLE_PINS_COUNT; i++) {
        if (pinName == pinNames[i]) {
          *pin = availablePins[i];
          break;
        }
      }
    }
  }
  file.close();
  Serial.print(F("NRF24 config loaded from "));
  Serial.println(path);
  return true;
}

void loadNRF24Config() {
  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println(F("SD init failed"));
    return;
  }
  if (loadNRF24ConfigFromFile(NRF24_CONFIG_PATH)) return;
  Serial.println(F("No NRF24 config, using defaults"));
}

void displayNRF24Menu() {
  display.clearDisplay();
  auto centerText = [](const char* text, int textSize) {
    return (128 - strlen(text) * (textSize == 2 ? 12 : 6)) / 2;
  };

  byte next = (nrf24MenuIndex + 1) % NRF24_MENU_ITEM_COUNT;
  byte prev = (nrf24MenuIndex + NRF24_MENU_ITEM_COUNT - 1) % NRF24_MENU_ITEM_COUNT;

  display.setTextSize(2);
  display.setCursor(centerText(nrf24MenuItems[nrf24MenuIndex], 2), 25);
  display.print(nrf24MenuItems[nrf24MenuIndex]);

  display.setTextSize(1);
  display.setCursor(centerText(nrf24MenuItems[next], 1), 50);
  display.print(nrf24MenuItems[next]);
  display.setCursor(centerText(nrf24MenuItems[prev], 1), 7);
  display.print(nrf24MenuItems[prev]);

  display.setCursor(2, 30);
  display.print(">");
  display.setCursor(120, 30);
  display.print("<");
  display.display();
}

void displayNRF24Config() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(1, 1);
  display.println(F(" Config"));
  display.println(F("====================="));
  const char* labels[] = {"3(CE):   ", "4(CSN):  ", "5(SCK):  ", "6(MOSI): ", "7(MISO): "};
  byte* pins[] = {&nrf24Config.cePin, &nrf24Config.csnPin, &nrf24Config.sckPin, &nrf24Config.mosiPin, &nrf24Config.misoPin};
  for (byte i = 0; i < 5; i++) {
    display.print(i == nrf24ConfigIndex ? F("> ") : F("  "));
    display.print(labels[i]);
    for (byte j = 0; j < AVAILABLE_PINS_COUNT; j++) {
      if (*pins[i] == availablePins[j]) {
        display.println(pinNames[j]);
        break;
      }
    }
  }
  display.display();
}

void displayJammingMenu() {
  display.clearDisplay();
  display.setTextColor(1);
  display.setTextSize(2);
  display.setTextWrap(false);
  int16_t modeWidth = strlen(jammingModes[jammingModeIndex]) * 12;
  int16_t modeX = (128 - modeWidth) / 2, modeY = (64 - 16) / 2;
  display.setCursor(modeX, modeY);
  display.print(jammingModes[jammingModeIndex]);
  display.setCursor(3, modeY);
  display.print(F("<"));
  display.setCursor(123 - 10, modeY);
  display.print(F(">"));
  display.display();
}

void displayJammingActive() {
  display.clearDisplay();
  display.drawBitmap(0, 12, image_Scanning_short_bits, 96, 52, 1);
  display.setTextColor(1);
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setCursor(66, 5);
  display.print(F("Jamming..."));
  display.display();
}

void displayNRF24Error() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(11, 20);
  display.print("NRF24 init failed.");
  display.setCursor(29, 32);
  display.print("ERROR: 0x002");
  display.display();
}

bool initializeNRF24() {
  loadNRF24Config();
  pinMode(nrf24Config.csnPin, OUTPUT);
  digitalWrite(nrf24Config.csnPin, HIGH);
  pinMode(nrf24Config.cePin, OUTPUT);
  digitalWrite(nrf24Config.cePin, LOW);
  NRFSPI->begin((int8_t)nrf24Config.sckPin, (int8_t)nrf24Config.misoPin, (int8_t)nrf24Config.mosiPin, (int8_t)nrf24Config.csnPin);
  delay(10);
  if (radio.begin(NRFSPI, (rf24_gpio_pin_t)nrf24Config.cePin, (rf24_gpio_pin_t)nrf24Config.csnPin)) {
    Serial.println(F("NRF24 initialized"));
    return true;
  }
  Serial.println(F("NRF24 init failed"));
  displayNRF24Error();
  return false;
}

void startNRFJamming() {
  radio.setPALevel(RF24_PA_MAX);
  if (!radio.setDataRate(RF24_2MBPS)) Serial.println(F("Data rate fail"));
  radio.setAddressWidth(3);
  radio.setPayloadSize(2);
  radio.startConstCarrier(RF24_PA_MAX, 45);
  byte* channels;
  byte channel_count;
  switch (jammingModeIndex) {
    case 0: channels = wifi_channels; channel_count = sizeof(wifi_channels); Serial.println(F("WiFi jamming")); break;
    case 1: channels = ble_channels; channel_count = sizeof(ble_channels); Serial.println(F("BLE jamming")); break;
    case 2: channels = bluetooth_channels; channel_count = sizeof(bluetooth_channels); Serial.println(F("Bluetooth jamming")); break;
    case 3: channels = usb_channels; channel_count = sizeof(usb_channels); Serial.println(F("USB jamming")); break;
    case 4: channels = video_channels; channel_count = sizeof(video_channels); Serial.println(F("Video jamming")); break;
    case 5: channels = rc_channels; channel_count = sizeof(rc_channels); Serial.println(F("RadioCH jamming")); break;
    case 6: channels = full_channels; channel_count = sizeof(full_channels); Serial.println(F("Full jamming")); break;
  }
  int ptr_hop = 0;
  while (inJammingActive) {
    radio.setChannel(channels[ptr_hop]);
    delay(1);
    ptr_hop = (ptr_hop + 1) % channel_count;
    buttonBack.tick();
    if (buttonBack.isClick()) {
      radio.stopConstCarrier();
      radio.powerDown();
      inJammingActive = false;
      displayJammingMenu();
      break;
    }
  }
}

void handleNRF24Config() {
  buttonUp.tick(); buttonDown.tick(); buttonOK.tick(); buttonBack.tick();
  if (buttonUp.isClick()) {
    nrf24ConfigIndex = (nrf24ConfigIndex - 1 + 5) % 5;
    displayNRF24Config();
  }
  if (buttonDown.isClick()) {
    nrf24ConfigIndex = (nrf24ConfigIndex + 1) % 5;
    displayNRF24Config();
  }
  if (buttonOK.isClick()) {
    byte* configPins[] = {&nrf24Config.cePin, &nrf24Config.csnPin, &nrf24Config.sckPin, &nrf24Config.mosiPin, &nrf24Config.misoPin};
    byte pinNum[] = {3, 4, 5, 6, 7};
    byte currentPinIndex = 0;
    for (byte i = 0; i < AVAILABLE_PINS_COUNT; i++) {
      if (*configPins[nrf24ConfigIndex] == availablePins[i]) {
        currentPinIndex = i;
        break;
      }
    }
    byte newPinIndex = (currentPinIndex + 1) % AVAILABLE_PINS_COUNT;
    *configPins[nrf24ConfigIndex] = availablePins[newPinIndex];
    saveNRF24Config();
    displayNRF24Config();
    Serial.printf("Pin %d to %s\n", pinNum[nrf24ConfigIndex], pinNames[newPinIndex]);
  }
  if (buttonBack.isClick()) {
    saveNRF24Config();
    inNRF24Config = false;
    nrf24MenuIndex = 2;
    displayNRF24Menu();
  }
}

void handleJammingMenu() {
  buttonUp.tick(); buttonDown.tick(); buttonOK.tick(); buttonBack.tick();
  if (buttonUp.isClick()) {
    jammingModeIndex = (jammingModeIndex - 1 + JAMMING_MODE_COUNT) % JAMMING_MODE_COUNT;
    displayJammingMenu();
  }
  if (buttonDown.isClick()) {
    jammingModeIndex = (jammingModeIndex + 1) % JAMMING_MODE_COUNT;
    displayJammingMenu();
  }
  if (buttonOK.isClick()) {
    Serial.printf("Selected mode: %s\n", jammingModes[jammingModeIndex]);
    if (initializeNRF24()) {
      inJammingActive = true;
      displayJammingActive();
      startNRFJamming();
    }
  }
  if (buttonBack.isClick()) {
    if (inJammingActive) {
      radio.stopConstCarrier();
      radio.powerDown();
      inJammingActive = false;
      displayJammingMenu();
    } else {
      inJammingMenu = false;
      nrf24MenuIndex = 0;
      displayNRF24Menu();
    }
  }
}

void handleNRF24Submenu() {
  if (inSpectrumAnalyzer) {
    runSpectrumAnalyzer();
    buttonBack.tick();
    buttonUp.tick();
    if (buttonUp.isClick()) {
      inSpectrumAnalyzer = false;
      radio.stopListening();
      radio.powerDown();
      memset(spectrumValues, 0, sizeof(spectrumValues));
      displayNRF24Menu();
    }
    if (buttonBack.isClick()) {
      inSpectrumAnalyzer = false;
      radio.stopListening();
      radio.powerDown();
      memset(spectrumValues, 0, sizeof(spectrumValues));
      displayNRF24Menu();
    }
    return;
  }
  if (inJammingMenu || inJammingActive) return handleJammingMenu();
  if (inNRF24Config) return handleNRF24Config();
  buttonUp.tick(); buttonDown.tick(); buttonOK.tick(); buttonBack.tick();
  if (buttonUp.isClick()) {
    nrf24MenuIndex = (nrf24MenuIndex - 1 + NRF24_MENU_ITEM_COUNT) % NRF24_MENU_ITEM_COUNT;
    displayNRF24Menu();
  }
  if (buttonDown.isClick()) {
    nrf24MenuIndex = (nrf24MenuIndex + 1) % NRF24_MENU_ITEM_COUNT;
    displayNRF24Menu();
  }
  if (buttonOK.isClick()) {
    Serial.printf("Selected: %s\n", nrf24MenuItems[nrf24MenuIndex]);
    switch (nrf24MenuIndex) {
      case 0:
        inJammingMenu = true;
        inJammingActive = false;
        jammingModeIndex = 0;
        loadNRF24Config();
        displayJammingMenu();
        break;
      case 1:
        loadNRF24Config();
        if (initializeNRF24()) {
          inSpectrumAnalyzer = true;
          memset(spectrumValues, 0, sizeof(spectrumValues));
          radio.startListening();
          radio.setAutoAck(false);
          radio.setPALevel(RF24_PA_MAX);
          radio.setDataRate(RF24_2MBPS);
          radio.setCRCLength(RF24_CRC_DISABLED);
          lastSpectrumUpdate = millis();
        }
        break;
      case 2:
        inNRF24Config = true;
        nrf24ConfigIndex = 0;
        loadNRF24Config();
        displayNRF24Config();
        break;
    }
  }
  if (buttonBack.isClick()) {
    inNRF24Submenu = inJammingMenu = inJammingActive = inSpectrumAnalyzer = false;
    displayGPIOMenu(display, gpioMenuIndex);
  }
}

String formatIButtonCode(const byte* data) {
  char code[17];
  for (int i = 0; i < 8; i++) {
    snprintf(code + (i * 2), sizeof(code) - (i * 2), "%02X", data[i]);
  }
  code[16] = '\0';
  return String(code);
}

void initIButtonWire() {
  byte pin = iButtonPins[iButtonPinIndex];
  if (iButtonWire == nullptr || iButtonPin != pin) {
    if (iButtonWire != nullptr) {
      delete iButtonWire;
      iButtonWire = nullptr;
    }
    iButtonPin = pin;
    iButtonWire = new OneWire(iButtonPin);
  }
}

bool detectIButton() {
  if (iButtonWire == nullptr) return false;
  return iButtonWire->reset() != 0;
}

void write_byte_rw1990(byte data, byte pin) {
  for (int data_bit = 0; data_bit < 8; data_bit++) {
    if (data & 1) {
      digitalWrite(pin, LOW);
      pinMode(pin, OUTPUT);
      delayMicroseconds(60);
      pinMode(pin, INPUT);
      digitalWrite(pin, HIGH);
    } else {
      digitalWrite(pin, LOW);
      pinMode(pin, OUTPUT);
      pinMode(pin, INPUT);
      digitalWrite(pin, HIGH);
    }
    delay(10);
    data = data >> 1;
  }
}

void readIButtonKey() {
  if (iButtonWire == nullptr) return;
  iButtonWire->write(0x33);
  iButtonWire->read_bytes(iButtonBuffer, 8);
  iButtonType = iButtonBuffer[0];
  iButtonBits = 64;
  iButtonCrcOk = (OneWire::crc8(iButtonBuffer, 7) == iButtonBuffer[7]);
}

void writeIButtonKey() {
  if (iButtonWire == nullptr) return;
  iButtonWire->skip();
  iButtonWire->reset();
  iButtonWire->write(0x33);

  iButtonWire->skip();
  iButtonWire->reset();
  iButtonWire->write(0x3C);
  delay(50);

  iButtonWire->skip();
  iButtonWire->reset();
  iButtonWire->write(0xD1);
  delay(50);

  digitalWrite(iButtonPin, LOW);
  pinMode(iButtonPin, OUTPUT);
  delayMicroseconds(60);
  pinMode(iButtonPin, INPUT);
  digitalWrite(iButtonPin, HIGH);
  delay(10);

  iButtonWire->skip();
  iButtonWire->reset();
  iButtonWire->write(0xD5);
  delay(50);

  for (byte i = 0; i < 8; i++) {
    write_byte_rw1990(iButtonBuffer[i], iButtonPin);
    delayMicroseconds(25);
  }

  iButtonWire->reset();
  iButtonWire->skip();
  iButtonWire->write(0xD1);
  delayMicroseconds(16);
  iButtonWire->reset();
}

void displayIButtonMenu() {
  display.clearDisplay();

  auto centerText = [](const char* text, int textSize) {
    return (128 - strlen(text) * (textSize == 2 ? 12 : 6)) / 2;
  };

  byte next = (iButtonMenuIndex + 1) % IBUTTON_MENU_ITEM_COUNT;
  byte prev = (iButtonMenuIndex + IBUTTON_MENU_ITEM_COUNT - 1) % IBUTTON_MENU_ITEM_COUNT;

  display.setTextSize(2);
  display.setCursor(centerText(iButtonMenuItems[iButtonMenuIndex], 2), 25);
  display.print(iButtonMenuItems[iButtonMenuIndex]);

  display.setTextSize(1);
  display.setCursor(centerText(iButtonMenuItems[next], 1), 50);
  display.print(iButtonMenuItems[next]);
  display.setCursor(centerText(iButtonMenuItems[prev], 1), 7);
  display.print(iButtonMenuItems[prev]);

  display.setCursor(2, 30);
  display.print(">");
  display.setCursor(120, 30);
  display.print("<");
  display.display();
}

void displayIButtonReadWaiting() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.setTextWrap(false);
  display.setCursor(3, 3);
  display.print("Waiting iButton...");
  display.drawBitmap(78, 19, image_iButtonKey_bits, 49, 44, 1);
  display.setCursor(5, 19);
  display.print("Press UP/DOWN");
  display.setCursor(5, 29);
  display.print("to change pin");
  display.setCursor(86, 42);
  display.print(iButtonPinNames[iButtonPinIndex]);
  display.display();
}

void displayIButtonDetected() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.setTextWrap(false);
  display.setCursor(2, 2);
  display.print("iButton detected:");
  display.setCursor(5, 14);
  display.print("Code:");
  display.setCursor(5, 26);
  display.print("Type:");
  display.setCursor(5, 38);
  display.print("Bits:");
  display.setCursor(17, 52);
  if (iButtonCrcOk) {
    display.print("Hold OK to save.");
  } else {
    display.setCursor(48, 53);
    display.print("CRC ERROR!");
  }

  String code = formatIButtonCode(iButtonBuffer);
  if (code.length() > 15) code = code.substring(0, 15);
  display.setCursor(35, 14);
  display.print(code);
  display.setCursor(35, 26);
  display.print("0x");
  if (iButtonType < 0x10) display.print("0");
  display.print(iButtonType, HEX);
  display.setCursor(35, 38);
  display.print(iButtonBits);
  display.display();
}

void displayIButtonWriteWaiting() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.setTextWrap(false);
  display.setCursor(2, 2);
  String name = iButtonExplorer.selectedFile.length() > 13 ? iButtonExplorer.selectedFile.substring(0, 13) : iButtonExplorer.selectedFile;
  display.print("Signal: " + name);
  display.setCursor(5, 14);
  display.print("Code:");
  display.setCursor(5, 26);
  display.print("Type:");
  display.setCursor(5, 38);
  display.print("Bits:");
  display.setCursor(11, 53);
  display.print("Waiting iButton...");

  String code = formatIButtonCode(iButtonBuffer);
  if (code.length() > 15) code = code.substring(0, 15);
  display.setCursor(35, 14);
  display.print(code);
  display.setCursor(35, 26);
  display.print("0x");
  if (iButtonType < 0x10) display.print("0");
  display.print(iButtonType, HEX);
  display.setCursor(35, 38);
  display.print(iButtonBits);
  display.display();
}


bool saveIButtonToSD() {
  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println(F("SD init failed"));
    return false;
  }
  if (!SD.exists(IBUTTON_DIR)) SD.mkdir(IBUTTON_DIR);
  int index = 1;
  String filePath;
  while (true) {
    filePath = String(IBUTTON_DIR) + "/iButton_" + String(index) + ".ibtn";
    if (!SD.exists(filePath)) break;
    index++;
  }
  File file = SD.open(filePath, FILE_WRITE);
  if (!file) {
    Serial.print(F("Failed to create file: "));
    Serial.println(filePath);
    return false;
  }
  file.println(F("Filetype: iButton Key File"));
  file.println(F("Version: 1"));
  file.print(F("Code: "));
  for (int i = 0; i < 8; i++) {
    if (iButtonBuffer[i] < 0x10) file.print("0");
    file.print(iButtonBuffer[i], HEX);
    if (i < 7) file.print(" ");
  }
  file.println();
  file.print(F("Type: 0x"));
  if (iButtonType < 0x10) file.print("0");
  file.println(iButtonType, HEX);
  file.print(F("Bits: "));
  file.println(iButtonBits);
  file.close();
  Serial.print(F("Saved iButton to "));
  Serial.println(filePath);
  return true;
}

bool loadIButtonFromSD(const String& fileName) {
  File file = SD.open(iButtonExplorer.currentDir + "/" + fileName, FILE_READ);
  if (!file) {
    Serial.print(F("Failed to open file: "));
    Serial.println(fileName);
    return false;
  }
  bool hasCode = false;
  String line;
  while (file.available()) {
    line = file.readStringUntil('\n');
    line.trim();
    if (line.startsWith("Code:") || line.startsWith("Key:")) {
      String codeStr = line.substring(line.indexOf(':') + 1);
      codeStr.trim();
      codeStr.replace(" ", "");
      codeStr.replace(":", "");
      if (codeStr.length() < 16) {
        file.close();
        return false;
      }
      for (int i = 0; i < 8; i++) {
        String byteStr = codeStr.substring(i * 2, i * 2 + 2);
        iButtonBuffer[i] = strtol(byteStr.c_str(), nullptr, 16);
      }
      iButtonType = iButtonBuffer[0];
      iButtonBits = 64;
      hasCode = true;
    } else if (line.startsWith("Type:")) {
      String typeStr = line.substring(5);
      typeStr.trim();
      if (typeStr.startsWith("0x") || typeStr.startsWith("0X")) typeStr = typeStr.substring(2);
      iButtonType = strtol(typeStr.c_str(), nullptr, 16);
    } else if (line.startsWith("Bits:")) {
      iButtonBits = line.substring(5).toInt();
    }
  }
  file.close();
  return hasCode;
}

void handleIButtonSubmenu() {
  buttonUp.tick(); buttonDown.tick(); buttonOK.tick(); buttonBack.tick();

  if (iButtonState == IBUTTON_MENU) {
    if (buttonUp.isClick()) {
      iButtonMenuIndex = (iButtonMenuIndex - 1 + IBUTTON_MENU_ITEM_COUNT) % IBUTTON_MENU_ITEM_COUNT;
      displayIButtonMenu();
    }
    if (buttonDown.isClick()) {
      iButtonMenuIndex = (iButtonMenuIndex + 1) % IBUTTON_MENU_ITEM_COUNT;
      displayIButtonMenu();
    }
    if (buttonOK.isClick()) {
      if (iButtonMenuIndex == 0) {
        iButtonState = IBUTTON_READ_WAIT;
        initIButtonWire();
        displayIButtonReadWaiting();
      } else if (iButtonMenuIndex == 1) {
        if (!SD.begin(SD_CS, sdSPI)) {
          display.clearDisplay();
          display.setTextSize(1);
          display.setCursor(1, 1);
          display.println(F("SD init failed"));
          display.display();
          delay(1000);
          displayIButtonMenu();
        } else {
          if (!SD.exists(IBUTTON_DIR)) SD.mkdir(IBUTTON_DIR);
          ExplorerInit(iButtonExplorer, iButtonFileList, IBUTTON_MAX_FILES, iButtonExplorerCfg);
          ExplorerLoad(iButtonExplorer, iButtonExplorerCfg);
          iButtonState = IBUTTON_WRITE_BROWSE;
          ExplorerDraw(iButtonExplorer, display);
        }
      }
    }
    if (buttonBack.isClick()) {
      inIButtonSubmenu = false;
      display.setTextColor(SH110X_WHITE);
      displayGPIOMenu(display, gpioMenuIndex);
    }
    return;
  }

  if (iButtonState == IBUTTON_READ_WAIT) {
    if (buttonUp.isClick()) {
      iButtonPinIndex = (iButtonPinIndex - 1 + IBUTTON_PINS_COUNT) % IBUTTON_PINS_COUNT;
      initIButtonWire();
      displayIButtonReadWaiting();
    }
    if (buttonDown.isClick()) {
      iButtonPinIndex = (iButtonPinIndex + 1) % IBUTTON_PINS_COUNT;
      initIButtonWire();
      displayIButtonReadWaiting();
    }
    if (buttonBack.isClick()) {
      iButtonState = IBUTTON_MENU;
      displayIButtonMenu();
      return;
    }
    if (detectIButton()) {
      readIButtonKey();
      displayIButtonDetected();
      iButtonState = IBUTTON_READ_DETECTED;
    }
    return;
  }

  if (iButtonState == IBUTTON_READ_DETECTED) {
    if (buttonOK.isClick()) {
      if (!iButtonCrcOk) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(10, 16);
        display.print("CRC ERROR!");
        display.display();
        delay(1000);
        iButtonState = IBUTTON_READ_WAIT;
        displayIButtonReadWaiting();
        return;
      }
      if (saveIButtonToSD()) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(6, 16);
        display.print("Saved");
        display.display();
        delay(1000);
      }
      iButtonState = IBUTTON_READ_WAIT;
      displayIButtonReadWaiting();
    }
    if (buttonBack.isClick()) {
      iButtonState = IBUTTON_MENU;
      displayIButtonMenu();
    }
    return;
  }

  if (iButtonState == IBUTTON_WRITE_BROWSE) {
    ExplorerAction action = ExplorerHandle(
      iButtonExplorer,
      iButtonExplorerCfg,
      display,
      buttonUp.isClick(),
      buttonDown.isClick(),
      buttonOK.isClick(),
      buttonBack.isClick(),
      buttonBack.isHolded()
    );
    if (action == EXPLORER_SELECT_FILE) {
      if (loadIButtonFromSD(iButtonExplorer.selectedFile)) {
        initIButtonWire();
        iButtonWasPresent = false;
        iButtonState = IBUTTON_WRITE_WAIT;
        displayIButtonWriteWaiting();
      } else {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(1, 1);
        display.println(F("Load failed"));
        display.display();
        delay(1000);
        ExplorerDraw(iButtonExplorer, display);
      }
    } else if (action == EXPLORER_EXIT) {
      inIButtonSubmenu = false;
      display.setTextColor(SH110X_WHITE);
      displayGPIOMenu(display, gpioMenuIndex);
    }
    return;
  }

  if (iButtonState == IBUTTON_WRITE_WAIT) {
    if (buttonBack.isClick()) {
      iButtonState = IBUTTON_WRITE_BROWSE;
      ExplorerDraw(iButtonExplorer, display);
      return;
    }
    bool present = detectIButton();
    if (present && !iButtonWasPresent) {
      writeIButtonKey();
      display.clearDisplay();
      display.drawBitmap(0, 9, image_iButtonDolphinSuccess_bits, 92, 55, 1);
      display.setTextColor(1);
      display.setTextWrap(false);
      display.setCursor(53, 3);
      display.print("Successfully");
      display.setCursor(79, 13);
      display.print("written");
      display.display();
      delay(1000);
      displayIButtonWriteWaiting();
      iButtonWasPresent = true;
    } else if (!present) {
      iButtonWasPresent = false;
    }
  }
}

void handleGPIOSubmenu() {
  if (inNRF24Submenu || inNRF24Config || inJammingMenu || inJammingActive || inSpectrumAnalyzer) return handleNRF24Submenu();
  if (inIButtonSubmenu) return handleIButtonSubmenu();
  buttonUp.tick(); buttonDown.tick(); buttonOK.tick(); buttonBack.tick();
  if (buttonUp.isClick()) {
    gpioMenuIndex = (gpioMenuIndex - 1 + GPIO_MENU_ITEM_COUNT) % GPIO_MENU_ITEM_COUNT;
    displayGPIOMenu(display, gpioMenuIndex);
  }
  if (buttonDown.isClick()) {
    gpioMenuIndex = (gpioMenuIndex + 1) % GPIO_MENU_ITEM_COUNT;
    displayGPIOMenu(display, gpioMenuIndex);
  }
  if (buttonOK.isClick()) {
    Serial.printf("GPIO option: %s\n", gpioMenuItems[gpioMenuIndex]);
    switch (gpioMenuIndex) {
      case 0:
        inIButtonSubmenu = true;
        iButtonState = IBUTTON_MENU;
        iButtonMenuIndex = 0;
        displayIButtonMenu();
        break;
      case 1:
        inNRF24Submenu = true;
        nrf24MenuIndex = 0;
        loadNRF24Config();
        displayNRF24Menu();
        break;
      case 2:
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(1, 1);
        display.println(F("In development..."));
        display.display();
        break;
    }
  }
  if (buttonBack.isClick()) {
    inMenu = true;
    OLED_printMenu(display, currentMenu);
    Serial.println(F("Back to main menu"));
  }
}
