#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <GyverButton.h>
#include <SD.h>
#include <SPI.h>
#include "CONFIG.h"
#include "interface.h"
#include "subghz.h"
#include "wifi_menu.h"
#include "bluetooth_menu.h"
#include "infrared_menu.h"
#include "gpio_menu.h"

void handleWiFiSubmenu();
void handleBluetoothSubmenu();
void handleIRSubmenu();
void handleGPIOSubmenu();

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
GButton buttonUp(BUTTON_UP, HIGH_PULL, NORM_OPEN);
GButton buttonDown(BUTTON_DOWN, HIGH_PULL, NORM_OPEN);
GButton buttonOK(BUTTON_OK, HIGH_PULL, NORM_OPEN);
GButton buttonBack(BUTTON_BACK, HIGH_PULL, NORM_OPEN);

SPIClass sdSPI(HSPI);

#define MENU_ITEM_COUNT 5
const char* menuItems[] = {"WiFi", "Bluetooth", "SubGHz", "Infrared", "GPIO"};
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

// Режим ожидания
unsigned long lastActivityTime = 0;
const unsigned long STANDBY_TIMEOUT = 30000; // 30сек
bool inStandby = false;

int dvd_x = 0;
int dvd_y = 0;
int dvd_dx = 1;
int dvd_dy = 1;
const int DVD_SPEED = 2;
const int DVD_LOGO_WIDTH = 32;
const int DVD_LOGO_HEIGHT = 24;

void resetActivityTimer() {
  lastActivityTime = millis();
  if (inStandby) {
    inStandby = false;
    if (inMenu) {
      OLED_printMenu(display, currentMenu);
    }
  }
}

void enterStandby() {
  inStandby = true;
  dvd_x = random(0, display.width() - DVD_LOGO_WIDTH + 1);
  dvd_y = random(0, display.height() - DVD_LOGO_HEIGHT + 1);
  dvd_dx = 1;
  dvd_dy = 1;
}

void drawStandbyAnimation() {
  display.clearDisplay();
  display.drawBitmap(dvd_x, dvd_y, bitmap_dvd_logo, DVD_LOGO_WIDTH, DVD_LOGO_HEIGHT, SH110X_WHITE);
  dvd_x += dvd_dx * DVD_SPEED;
  dvd_y += dvd_dy * DVD_SPEED;
  if (dvd_x <= 0 || dvd_x + DVD_LOGO_WIDTH >= display.width()) {
    dvd_dx = -dvd_dx;
    dvd_x = (dvd_x <= 0) ? 0 : display.width() - DVD_LOGO_WIDTH;
  }
  if (dvd_y <= 0 || dvd_y + DVD_LOGO_HEIGHT >= display.height()) {
    dvd_dy = -dvd_dy;
    dvd_y = (dvd_y <= 0) ? 0 : display.height() - DVD_LOGO_HEIGHT;
  }
  display.display();
}

bool manageStandby(bool anyClick) {
  if (inStandby) {
    if (anyClick) {
      resetActivityTimer();
      return true;
    } else {
      drawStandbyAnimation();
      return true;
    }
  } else {
    if (millis() - lastActivityTime >= STANDBY_TIMEOUT) {
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
  
  if (!display.begin(OLED_ADR)) {
    Serial.println(F("SH110X allocation failed"));
    for(;;);
  }

  sdSPI.begin(SD_CLK, SD_MISO, SD_MOSI);
  sdSPI.setFrequency(4000000);
  if (!SD.begin(-1, sdSPI)) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println(F("SD-Card Init Failed"));
    display.println(F(""));
    display.println(F("ERROR: 0x000"));
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
  buttonUp.tick();
  buttonDown.tick();
  buttonOK.tick();
  buttonBack.tick();

  // Track any button press (not clicks) to keep the standby timer alive even in submenus
  bool anyPress = buttonUp.isPress() || buttonDown.isPress() || buttonOK.isPress() || buttonBack.isPress();
  if (anyPress && !inStandby) {
    resetActivityTimer();
  }

  if (manageStandby(anyPress)) return;

  if (inMenu) {
    bool upClick = buttonUp.isClick();
    bool downClick = buttonDown.isClick();
    bool okClick = buttonOK.isClick();
    bool backClick = buttonBack.isClick();

    bool anyClick = upClick || downClick || okClick || backClick;
    if (anyClick) {
      resetActivityTimer();
    }

    if (upClick) {
      currentMenu = (currentMenu - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
      OLED_printMenu(display, currentMenu);
    }
    if (downClick) {
      currentMenu = (currentMenu + 1) % MENU_ITEM_COUNT;
      OLED_printMenu(display, currentMenu);
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
        runSubGHz();
        inMenu = true;
        OLED_printMenu(display, currentMenu);
      } else if (currentMenu == 3) {
        irMenuIndex = 0;
        displayIRMenu(display, irMenuIndex);
      } else if (currentMenu == 4) {
        gpioMenuIndex = 0;
        displayGPIOMenu(display, gpioMenuIndex);
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
    }
  }
}
