#include "menu/gpio.h"
#include "CONFIG.h"
#include <Adafruit_SH110X.h>
#include <GyverButton.h>
#include <SPI.h>
#include <RF24.h>
#include <SD.h>

extern Adafruit_SH1106G display;
extern GButton buttonUp, buttonDown, buttonOK, buttonBack;
extern bool inMenu;
extern byte currentMenu, gpioMenuIndex;

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
  if (!SD.begin(SD_CLK)) {
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
  if (!SD.begin(SD_CLK)) {
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

void handleGPIOSubmenu() {
  if (inNRF24Submenu || inNRF24Config || inJammingMenu || inJammingActive || inSpectrumAnalyzer) return handleNRF24Submenu();
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
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(1, 1);
        display.println(F("In development..."));
        display.display();
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