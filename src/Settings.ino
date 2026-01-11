#include <Adafruit_SH110X.h>
#include <GyverButton.h>
#include "CONFIG.h"
#include "menu/settings.h"

extern Adafruit_SH1106G display;
extern GButton buttonUp;
extern GButton buttonDown;
extern GButton buttonOK;
extern GButton buttonBack;
extern bool inMenu;
extern byte currentMenu;
extern byte settingsMenuIndex;
extern byte standbyTimeoutIndex;
extern unsigned long standbyTimeoutMs;
extern byte colorSelectionIndex;
extern const unsigned long standbyTimeoutOptionsMs[];
extern const char* standbyTimeoutLabels[];
extern const byte STANDBY_OPTION_COUNT;
extern const char* colorOptions[];
extern const byte COLOR_OPTION_COUNT;
extern void applyColorScheme();
extern void saveConfig();
extern void OLED_printMenu(Adafruit_SH1106G &display, byte menuIndex);
extern void resetActivityTimer();

enum SettingsDetail : byte { SETTINGS_NONE, SETTINGS_COLOR, SETTINGS_STANDBY, SETTINGS_ABOUT };

static SettingsDetail currentDetail = SETTINGS_NONE;
static byte standbySelectionIndex = 0;
static byte colorSelectionWorking = 0;
static bool colorNeedRedraw = true;
static bool standbyNeedRedraw = true;
static bool aboutNeedRedraw = true;

void exitSettingsDetail() {
  currentDetail = SETTINGS_NONE;
  displaySettingsMenu(display, settingsMenuIndex);
  colorNeedRedraw = true;
  standbyNeedRedraw = true;
  aboutNeedRedraw = true;
}

void renderColorSetting() {
  colorNeedRedraw = false;
  byte index = colorSelectionWorking;
  byte next = (index + 1) % COLOR_OPTION_COUNT;
  byte prev = (index + COLOR_OPTION_COUNT - 1) % COLOR_OPTION_COUNT;

  int16_t x1, y1;
  uint16_t w, h;

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(1);
  display.getTextBounds(colorOptions[prev], 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 7);
  display.print(colorOptions[prev]);

  display.setTextSize(2);
  display.getTextBounds(colorOptions[index], 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 25);
  display.print(colorOptions[index]);

  display.setTextSize(1);
  display.getTextBounds(colorOptions[next], 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 50);
  display.print(colorOptions[next]);

  display.setCursor(2, 30);
  display.print(">");
  display.setCursor(120, 30);
  display.print("<");

  display.display();
}

void renderAboutSetting() {
  aboutNeedRedraw = false;
  display.clearDisplay();
  display.setTextColor(1);
  display.setTextSize(2);
  display.setTextWrap(false);
  display.setCursor(5, 5);
  display.print("ESP-HACK");

  display.setCursor(77, 24);
  display.print("v0.5");

  display.setTextSize(1);
  display.setCursor(5, 55);
  display.print("github.com/Teapot174");
  display.drawBitmap(11, 2, image_Teapot_bits, 63, 64, 1);
  display.display();
}

void renderStandbySetting(byte index) {
  standbyNeedRedraw = false;
  byte next = (index + 1) % STANDBY_OPTION_COUNT;
  byte prev = (index + STANDBY_OPTION_COUNT - 1) % STANDBY_OPTION_COUNT;

  int16_t x1, y1;
  uint16_t w, h;

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(1);
  display.getTextBounds(standbyTimeoutLabels[prev], 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 7);
  display.print(standbyTimeoutLabels[prev]);

  display.setTextSize(2);
  display.getTextBounds(standbyTimeoutLabels[index], 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 25);
  display.print(standbyTimeoutLabels[index]);

  display.setTextSize(1);
  display.getTextBounds(standbyTimeoutLabels[next], 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 50);
  display.print(standbyTimeoutLabels[next]);

  display.setCursor(2, 30);
  display.print(">");
  display.setCursor(120, 30);
  display.print("<");

  display.display();
}

void handleColorDetail(bool upClick, bool downClick, bool okClick, bool backClick) {
  if (colorNeedRedraw) {
    renderColorSetting();
  }

  if (upClick || downClick) {
    colorSelectionWorking = colorSelectionWorking == 0 ? 1 : 0;
    colorNeedRedraw = true;
  }

  if (okClick) {
    colorSelectionIndex = colorSelectionWorking;
    applyColorScheme();
    saveConfig();
    exitSettingsDetail();
    return;
  }

  if (backClick) {
    exitSettingsDetail();
    return;
  }
}

void handleAboutDetail(bool okClick, bool backClick) {
  if (aboutNeedRedraw) {
    renderAboutSetting();
  }

  if (backClick) {
    exitSettingsDetail();
  }
}

void handleStandbyDetail(bool upClick, bool downClick, bool okClick, bool backClick) {
  if (standbyNeedRedraw) {
    renderStandbySetting(standbySelectionIndex);
  }

  if (upClick) {
    standbySelectionIndex = (standbySelectionIndex + STANDBY_OPTION_COUNT - 1) % STANDBY_OPTION_COUNT;
    standbyNeedRedraw = true;
  }
  if (downClick) {
    standbySelectionIndex = (standbySelectionIndex + 1) % STANDBY_OPTION_COUNT;
    standbyNeedRedraw = true;
  }
  if (okClick) {
    standbyTimeoutIndex = standbySelectionIndex;
    standbyTimeoutMs = standbyTimeoutOptionsMs[standbyTimeoutIndex];
    resetActivityTimer();
    saveConfig();
    exitSettingsDetail();
    return;
  }
  if (backClick) {
    exitSettingsDetail();
    return;
  }
}

void enterSettingsDetail(byte menuIndex) {
  if (menuIndex == 0) {
    currentDetail = SETTINGS_COLOR;
    colorNeedRedraw = true;
    colorSelectionWorking = colorSelectionIndex;
    renderColorSetting();
  } else if (menuIndex == 1) {
    currentDetail = SETTINGS_STANDBY;
    standbySelectionIndex = standbyTimeoutIndex;
    standbyNeedRedraw = true;
    renderStandbySetting(standbySelectionIndex);
  } else if (menuIndex == 2) {
    currentDetail = SETTINGS_ABOUT;
    aboutNeedRedraw = true;
    renderAboutSetting();
  }
}

void handleSettingsSubmenu() {
  buttonUp.tick();
  buttonDown.tick();
  buttonOK.tick();
  buttonBack.tick();

  bool upClick = buttonUp.isClick();
  bool downClick = buttonDown.isClick();
  bool okClick = buttonOK.isClick();
  bool backClick = buttonBack.isClick();

  if (currentDetail == SETTINGS_COLOR) {
    handleColorDetail(upClick, downClick, okClick, backClick);
    return;
  } else if (currentDetail == SETTINGS_STANDBY) {
    handleStandbyDetail(upClick, downClick, okClick, backClick);
    return;
  } else if (currentDetail == SETTINGS_ABOUT) {
    handleAboutDetail(okClick, backClick);
    return;
  }

  if (upClick) {
    settingsMenuIndex = (settingsMenuIndex + SETTINGS_MENU_ITEM_COUNT - 1) % SETTINGS_MENU_ITEM_COUNT;
    displaySettingsMenu(display, settingsMenuIndex);
  }
  if (downClick) {
    settingsMenuIndex = (settingsMenuIndex + 1) % SETTINGS_MENU_ITEM_COUNT;
    displaySettingsMenu(display, settingsMenuIndex);
  }
  if (okClick) {
    enterSettingsDetail(settingsMenuIndex);
  }
  if (backClick) {
    inMenu = true;
    OLED_printMenu(display, currentMenu);
  }
}
