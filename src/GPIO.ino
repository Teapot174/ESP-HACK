#include "gpio_menu.h"
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

// NRF24 variables
RF24 radio(CC1101_CS, CC1101_GDO0);
SPIClass *NRFSPI = &SPI;
bool inNRF24Submenu = false, inJammingMenu = false, inJammingActive = false, inNRF24Config = false;
byte nrf24MenuIndex = 0, nrf24ConfigIndex = 0, jammingModeIndex = 0;
const char* nrf24MenuItems[] = {"Jammer", "Spectrum", "Config"};
const char* jammingModes[] = {"WiFi", "BLE", "BT", "USB", "Video", "RadioCH", "ALL"};
const byte NRF24_MENU_ITEM_COUNT = 3, JAMMING_MODE_COUNT = 7;

// NRF24 pin configuration
struct NRF24Config {
  byte cePin = GPIO_A, csnPin = GPIO_B, mosiPin = GPIO_C, misoPin = GPIO_D, sckPin = GPIO_E;
} nrf24Config;

// Available pins
const byte availablePins[] = {GPIO_A, GPIO_B, GPIO_C, GPIO_D, GPIO_E, GPIO_F};
const char* pinNames[] = {"A", "B", "C", "D", "E", "F"};
const byte AVAILABLE_PINS_COUNT = 6;

// Channel arrays for jamming
byte wifi_channels[] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 22, 24, 26, 28, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 46, 48, 50, 52, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68};
byte ble_channels[] = {1, 2, 3, 25, 26, 27, 79, 80, 81};
byte bluetooth_channels[] = {2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80};
byte usb_channels[] = {40, 50, 60};
byte video_channels[] = {70, 75, 80};
byte rc_channels[] = {1, 3, 5, 7};
byte full_channels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100};

void saveNRF24Config() {
  if (!SD.begin(SD_CLK)) {
    Serial.println(F("SD init failed"));
    return;
  }
  if (!SD.exists("/GPIO")) SD.mkdir("/GPIO");
  File file = SD.open("/GPIO/nrf24.conf", FILE_WRITE);
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

void loadNRF24Config() {
  if (!SD.begin(SD_CLK)) {
    Serial.println(F("SD init failed"));
    return;
  }
  File file = SD.open("/GPIO/nrf24.conf", FILE_READ);
  if (file) {
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
    Serial.println(F("NRF24 config loaded"));
  } else {
    Serial.println(F("No NRF24 config, using defaults"));
  }
}

void displayNRF24Menu() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 8);
  for (byte i = 0; i < NRF24_MENU_ITEM_COUNT; i++) {
    display.println(i == nrf24MenuIndex ? F("> ") : F("  "));
    display.setCursor(20, display.getCursorY() - 16);
    display.println(nrf24MenuItems[i]);
  }
  display.display();
}

void displayNRF24Config() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
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
  display.setCursor(123 - 10, modeY); // Adjusted for right arrow
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
  display.setCursor(0, 0);
  display.println(F("NRF24 Init Failed\n\nERROR: 0x002"));
  display.display();
}

void displaySpectrum() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  // Frequency markers
  display.setCursor(3, 1);
  display.print(F("2.4"));
  display.setCursor(51, 1);
  display.print(F("2.45"));
  display.setCursor(108, 1);
  display.print(F("2.5"));
  // Spectrum bars (channels 0-127 mapped to 128 pixels)
  for (byte ch = 0; ch < 128; ch++) {
    radio.setChannel(ch);
    bool carrier = radio.testCarrier();
    int height = carrier ? 50 : 10; // 50px for signal, 10px for no signal
    display.fillRect(ch * 2, 63 - height, 1, height, SH110X_WHITE); // 1px bar, 1px gap
  }
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
      Serial.println(F("Jamming stopped, back to Jamming menu"));
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
    byte pinNum[] = {3, 4, 7, 5, 6};
    byte currentPinIndex = 0;
    for (byte i = 0; i < AVAILABLE_PINS_COUNT; i++) {
      if (*configPins[nrf24ConfigIndex] == availablePins[i]) {
        currentPinIndex = i;
        break;
      }
    }
    *configPins[nrf24ConfigIndex] = availablePins[(currentPinIndex + 1) % AVAILABLE_PINS_COUNT];
    saveNRF24Config();
    displayNRF24Config();
    Serial.printf("Pin %d to %s\n", pinNum[nrf24ConfigIndex], pinNames[currentPinIndex]);
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
    Serial.println(F("Jamming menu Up"));
  }
  if (buttonDown.isClick()) {
    jammingModeIndex = (jammingModeIndex + 1) % JAMMING_MODE_COUNT;
    displayJammingMenu();
    Serial.println(F("Jamming menu Down"));
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
      Serial.println(F("Jamming stopped, back to Jamming menu"));
    } else {
      inJammingMenu = false;
      nrf24MenuIndex = 0;
      displayNRF24Menu();
      Serial.println(F("Back to NRF24 menu"));
    }
  }
}

void handleNRF24Submenu() {
  if (inJammingMenu || inJammingActive) return handleJammingMenu();
  if (inNRF24Config) return handleNRF24Config();
  buttonUp.tick(); buttonDown.tick(); buttonOK.tick(); buttonBack.tick();
  if (buttonUp.isClick()) {
    nrf24MenuIndex = (nrf24MenuIndex - 1 + NRF24_MENU_ITEM_COUNT) % NRF24_MENU_ITEM_COUNT;
    displayNRF24Menu();
    Serial.println(F("NRF24 menu Up"));
  }
  if (buttonDown.isClick()) {
    nrf24MenuIndex = (nrf24MenuIndex + 1) % NRF24_MENU_ITEM_COUNT;
    displayNRF24Menu();
    Serial.println(F("NRF24 menu Down"));
  }
  if (buttonOK.isClick()) {
    Serial.printf("Selected: %s\n", nrf24MenuItems[nrf24MenuIndex]);
    switch (nrf24MenuIndex) {
      case 0: // Jammer
        inJammingMenu = true;
        inJammingActive = false;
        jammingModeIndex = 0;
        loadNRF24Config();
        displayJammingMenu();
        break;
      case 1: // Spectrum
        loadNRF24Config();
        if (initializeNRF24()) {
          displaySpectrum();
          while (!buttonBack.isClick()) {
            buttonBack.tick();
            displaySpectrum();
          }
          radio.powerDown();
        }
        break;
      case 2: // Config
        inNRF24Config = true;
        nrf24ConfigIndex = 0;
        loadNRF24Config();
        displayNRF24Config();
        break;
    }
  }
  if (buttonBack.isClick()) {
    inNRF24Submenu = inJammingMenu = inJammingActive = false;
    displayGPIOMenu(display, gpioMenuIndex);
    Serial.println(F("Back to GPIO menu"));
  }
}

void handleGPIOSubmenu() {
  if (inNRF24Submenu || inNRF24Config || inJammingMenu || inJammingActive) return handleNRF24Submenu();
  buttonUp.tick(); buttonDown.tick(); buttonOK.tick(); buttonBack.tick();
  if (buttonUp.isClick()) {
    gpioMenuIndex = (gpioMenuIndex - 1 + GPIO_MENU_ITEM_COUNT) % GPIO_MENU_ITEM_COUNT;
    displayGPIOMenu(display, gpioMenuIndex);
    Serial.println(F("GPIO menu Up"));
  }
  if (buttonDown.isClick()) {
    gpioMenuIndex = (gpioMenuIndex + 1) % GPIO_MENU_ITEM_COUNT;
    displayGPIOMenu(display, gpioMenuIndex);
    Serial.println(F("GPIO menu Down"));
  }
  if (buttonOK.isClick()) {
    Serial.printf("GPIO option: %s\n", gpioMenuItems[gpioMenuIndex]);
    switch (gpioMenuIndex) {
      case 0: // iButton
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println(F("iButton Selected"));
        display.display();
        break;
      case 1: // NRF24
        inNRF24Submenu = true;
        nrf24MenuIndex = 0;
        loadNRF24Config();
        displayNRF24Menu();
        break;
      case 2: // Serial
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println(F("Serial Selected"));
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
