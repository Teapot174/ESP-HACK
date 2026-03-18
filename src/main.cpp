#include "display.h"
#include <GyverButton.h>
#include <SD.h>
#include <SPI.h>
#include "esp_wifi.h"
#include "CONFIG.h"
#include "interface.h"
#include "subghz.h"
#include "menu/wifi.h"
#include "menu/bluetooth.h"
#include "menu/infrared.h"
#include "menu/gpio.h"
#include "menu/settings.h"
#include "Explorer.h"

void handleWiFiSubmenu();
void handleBluetoothSubmenu();
void handleIRSubmenu();
void handleGPIOSubmenu();
void handleSettingsSubmenu();

DisplayType display = DisplayType(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
GButton buttonUp(BUTTON_UP, HIGH_PULL, NORM_OPEN);
GButton buttonDown(BUTTON_DOWN, HIGH_PULL, NORM_OPEN);
GButton buttonOK(BUTTON_OK, HIGH_PULL, NORM_OPEN);
GButton buttonBack(BUTTON_BACK, HIGH_PULL, NORM_OPEN);

SPIClass sdSPI(HSPI);

#define MENU_ITEM_COUNT 6
const char* menuItems[] = {"WiFi", "Bluetooth", "SubGHz", "Infrared", "GPIO", "Config"};
byte currentMenu = 0;
bool inMenu = true;

byte wifiMenuIndex = 0;
byte bluetoothMenuIndex = 0;
bool inBadBLE = false;
byte badBLEScriptIndex = 0;
bool scriptSelected = false;
byte selectedScript = 0;

byte irMenuIndex = 0;
bool inIRSelection = false;
bool inIRAttack = false;

byte gpioMenuIndex = 0;

byte settingsMenuIndex = 0;
byte colorSelectionIndex = 1;

extern const unsigned long standbyTimeoutOptionsMs[] = {0UL, 15000UL, 30000UL, 60000UL, 180000UL, 300000UL, 600000UL, 1800000UL};
const char* standbyTimeoutLabels[] = {"Disable", "15s", "30s", "1m", "3m", "5m", "10m", "30m"};
const char* standbyTimeoutConfigValues[] = {"0", "15s", "30s", "1m", "3m", "5m", "10m", "30m"};
extern const byte STANDBY_OPTION_COUNT = sizeof(standbyTimeoutOptionsMs) / sizeof(standbyTimeoutOptionsMs[0]);
const char* colorOptions[] = {"White", "Black"};
extern const byte COLOR_OPTION_COUNT = 2;
const char* CONFIG_PATH = "/esphack.cfg";
char wifiPortalName[33] = "FREE WIFI";
char bleDeviceName[33] = "ESP-BLE";

unsigned long lastActivityTime = 0;
byte standbyTimeoutIndex = 2;
unsigned long standbyTimeoutMs = standbyTimeoutOptionsMs[standbyTimeoutIndex];
bool inStandby = false;
unsigned long standbyIgnoreUntilMs = 0;

int standby_x = 0;
int standby_y = 0;
int standby_dx = 1;
int standby_dy = 1;
const int standby_SPEED = 2;
const int standby_LOGO_WIDTH = 22;
const int standby_LOGO_HEIGHT = 35;

void applyColorScheme() {
  display.invertDisplay(colorSelectionIndex == 0);
}

void saveConfig() {
  SD.remove(CONFIG_PATH);
  File cfg = SD.open(CONFIG_PATH, FILE_WRITE);
  if (!cfg) return;
  cfg.print(F("standby="));
  cfg.println(standbyTimeoutConfigValues[standbyTimeoutIndex]);
  cfg.print(F("color="));
  cfg.println(colorSelectionIndex == 0 ? F("white") : F("black"));
  cfg.print(F("WiFi_NAME="));
  cfg.println(wifiPortalName);
  cfg.print(F("BLE_NAME="));
  cfg.println(bleDeviceName);
  cfg.close();
}

void loadConfig() {
  byte parsedStandby = standbyTimeoutIndex;
  byte parsedColor = colorSelectionIndex;
  bool loaded = false;
  bool hasWifiName = false;
  bool hasBleName = false;

  File cfg = SD.open(CONFIG_PATH, FILE_READ);
  if (cfg) {
    loaded = true;
    while (cfg.available()) {
      String line = cfg.readStringUntil('\n');
      line.trim();
      if (line.startsWith("standby=")) {
        String val = line.substring(8);
        val.trim();
        val.toLowerCase();
        for (byte i = 0; i < STANDBY_OPTION_COUNT; i++) {
          String opt = standbyTimeoutConfigValues[i];
          opt.toLowerCase();
          if (opt == val) {
            parsedStandby = i;
            break;
          }
        }
      } else if (line.startsWith("color=")) {
        String val = line.substring(6);
        val.trim();
        val.toLowerCase();
        if (val == "white") parsedColor = 0;
        else parsedColor = 1;
      } else if (line.startsWith("WiFi_NAME=") || line.startsWith("wifi_name=")) {
        String val = line.substring(line.indexOf('=') + 1);
        val.trim();
        hasWifiName = true;
        if (val.length() > 0) {
          val.toCharArray(wifiPortalName, sizeof(wifiPortalName));
        }
      } else if (line.startsWith("BLE_NAME=") || line.startsWith("ble_name=")) {
        String val = line.substring(line.indexOf('=') + 1);
        val.trim();
        hasBleName = true;
        if (val.length() > 0) {
          val.toCharArray(bleDeviceName, sizeof(bleDeviceName));
        }
      }
    }
    cfg.close();
  }

  standbyTimeoutIndex = parsedStandby;
  standbyTimeoutMs = standbyTimeoutOptionsMs[standbyTimeoutIndex];
  colorSelectionIndex = parsedColor;
  applyColorScheme();

  if (!loaded || !hasWifiName || !hasBleName) {
    saveConfig();
  }
}

void resetActivityTimer() {
  lastActivityTime = millis();
  if (inStandby) {
    inStandby = false;
    standbyIgnoreUntilMs = millis() + 250;
    if (inMenu) {
      OLED_printMenu(display, currentMenu);
    }
  }
}

void returnToMainMenu() {
  buttonUp.resetStates();
  buttonDown.resetStates();
  buttonOK.resetStates();
  buttonBack.resetStates();
  inStandby = false;
  lastActivityTime = millis();
  standbyIgnoreUntilMs = millis() + 250;
  inMenu = true;
  OLED_printMenu(display, currentMenu);
}

static bool isMainMenuButtonPress(uint8_t pin, MenuButtonState &state) {
  const bool pressed = digitalRead(pin) == LOW;
  const unsigned long now = millis();
  const unsigned long initialDelayMs = 250;
  const unsigned long repeatDelayMs = 250;

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

void enterStandby() {
  inStandby = true;
  standby_x = random(0, display.width() - standby_LOGO_WIDTH + 1);
  standby_y = random(0, display.height() - standby_LOGO_HEIGHT + 1);
  standby_dx = 1;
  standby_dy = 1;
}

void drawStandbyAnimation() {
  display.clearDisplay();
  display.drawBitmap(standby_x, standby_y, bitmap_Standby_logo, standby_LOGO_WIDTH, standby_LOGO_HEIGHT, SH110X_WHITE);
  standby_x += standby_dx * standby_SPEED;
  standby_y += standby_dy * standby_SPEED;
  if (standby_x <= 0 || standby_x + standby_LOGO_WIDTH >= display.width()) {
    standby_dx = -standby_dx;
    standby_x = (standby_x <= 0) ? 0 : display.width() - standby_LOGO_WIDTH;
  }
  if (standby_y <= 0 || standby_y + standby_LOGO_HEIGHT >= display.height()) {
    standby_dy = -standby_dy;
    standby_y = (standby_y <= 0) ? 0 : display.height() - standby_LOGO_HEIGHT;
  }
  display.display();
}

bool manageStandby(bool anyClick) {
  if (standbyTimeoutMs == 0) {
    if (inStandby) inStandby = false;
    return false;
  }

  if (inStandby) {
    if (anyClick) {
      resetActivityTimer();
      return true;
    } else {
      drawStandbyAnimation();
      return true;
    }
  } else {
    if (millis() - lastActivityTime >= standbyTimeoutMs) {
      enterStandby();
      drawStandbyAnimation();
      return true;
    }
    return false;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  buttonUp.setDebounce(50);
  buttonUp.setTimeout(500);
  buttonUp.setClickTimeout(300);
  buttonUp.setStepTimeout(200);
  buttonDown.setDebounce(50);
  buttonDown.setTimeout(500);
  buttonDown.setClickTimeout(300);
  buttonDown.setStepTimeout(200);
  buttonOK.setDebounce(50);
  buttonOK.setTimeout(500);
  buttonOK.setClickTimeout(300);
  buttonOK.setStepTimeout(200);
  buttonBack.setDebounce(50);
  buttonBack.setTimeout(500);
  buttonBack.setClickTimeout(300);
  buttonBack.setStepTimeout(200);
  buttonUp.setTickMode(AUTO);
  buttonDown.setTickMode(AUTO);

  Wire.begin(OLED_SDA, OLED_SCL);
  
  #if DISPLAY_TYPE == DISPLAY_SSD1306
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADR)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  #else
  if (!display.begin(OLED_ADR)) {
    Serial.println(F("SH110X allocation failed"));
    for(;;);
  }
  #endif
  display.clearDisplay();
  display.display();

  sdSPI.begin(SD_CLK, SD_MISO, SD_MOSI);
  sdSPI.setFrequency(4000000);
  if (!SD.begin(SD_CS, sdSPI)) {
    display.clearDisplay();
    display.drawBitmap(72, 16, image_DolphinCommon_bits, 56, 48, 1);
    display.drawBitmap(25, 28, image_MicroSD_bits, 32, 28, 1);
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(3, 3);
    display.print("SD Card init failed.");
    display.setCursor(3, 14);
    display.print("ERROR: 0x000");
    display.display();
    for(;;);
  }

  if (!SD.exists("/WiFi")) SD.mkdir("/WiFi");
  if (!SD.exists("/WiFi/Wardriving")) SD.mkdir("/WiFi/Wardriving");
  if (!SD.exists("/WiFi/Portals")) SD.mkdir("/WiFi/Portals");
  if (!SD.exists("/BadKB")) SD.mkdir("/BadKB");
  if (!SD.exists("/SubGHz")) SD.mkdir("/SubGHz");
  if (!SD.exists("/Infrared")) SD.mkdir("/Infrared");
  if (!SD.exists("/GPIO")) SD.mkdir("/GPIO");
  if (!SD.exists("/iButton")) SD.mkdir("/iButton");

  loadConfig();

  OLED_printLogo(display);

  unsigned long logoStart = millis();
  while (millis() - logoStart < 1500) {
    buttonUp.tick();
    buttonDown.tick();
    buttonOK.tick();
    buttonBack.tick();
    if (buttonUp.isClick() || buttonDown.isClick() || buttonOK.isClick() || buttonBack.isClick()) {
      break;
    }
    delay(10);
  }

  display.clearDisplay();
  display.display();

  OLED_printMenu(display, currentMenu);
  lastActivityTime = millis();
}

void loop() {
  static MenuButtonState upHeld;
  static MenuButtonState downHeld;
  buttonUp.tick();
  buttonDown.tick();
  buttonOK.tick();
  buttonBack.tick();

  bool anyPress = buttonUp.isPress() || buttonDown.isPress() || buttonOK.isPress() || buttonBack.isPress();
  if (anyPress && !inStandby) {
    resetActivityTimer();
  }

  if (inMenu && manageStandby(anyPress)) return;

  if (millis() < standbyIgnoreUntilMs) {
    buttonUp.isClick();
    buttonDown.isClick();
    buttonOK.isClick();
    buttonBack.isClick();
    return;
  }

  if (inMenu) {
    bool upPress = isMainMenuButtonPress(BUTTON_UP, upHeld);
    bool downPress = isMainMenuButtonPress(BUTTON_DOWN, downHeld);
    bool okClick = buttonOK.isClick();
    bool backClick = buttonBack.isClick();

    bool anyClick = upPress || downPress || okClick || backClick;
    if (anyClick) {
      resetActivityTimer();
    }

    if (upPress) {
      byte previousIndex = currentMenu;
      currentMenu = (currentMenu - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
      OLED_printMenuAnimated(display, currentMenu, previousIndex);
    }
    if (downPress) {
      byte previousIndex = currentMenu;
      currentMenu = (currentMenu + 1) % MENU_ITEM_COUNT;
      OLED_printMenuAnimated(display, currentMenu, previousIndex);
    }
    if (okClick) {
      inMenu = false;
      if (currentMenu == 0) {
        wifiMenuIndex = 0;
        displayWiFiMenu(display, wifiMenuIndex);
      } else if (currentMenu == 1) {
        bluetoothMenuIndex = 0;
        displayBluetoothMenu(display, bluetoothMenuIndex);
      } else if (currentMenu == 2) {
        inMenu = false;
        runSubGHz();
        returnToMainMenu();
      } else if (currentMenu == 3) {
        irMenuIndex = 0;
        displayIRMenu(display, irMenuIndex);
      } else if (currentMenu == 4) {
        gpioMenuIndex = 0;
        displayGPIOMenu(display, gpioMenuIndex);
      } else if (currentMenu == 5) {
        settingsMenuIndex = 0;
        displaySettingsMenu(display, settingsMenuIndex);
      }
    }
  } else {
    if (currentMenu == 0) {
      handleWiFiSubmenu();
    } else if (currentMenu == 1) {
      handleBluetoothSubmenu();
    } else if (currentMenu == 3) {
      handleIRSubmenu();
    } else if (currentMenu == 4) {
      handleGPIOSubmenu();
    } else if (currentMenu == 5) {
      handleSettingsSubmenu();
    }
  }
}
