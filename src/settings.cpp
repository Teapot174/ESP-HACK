#include "display.h"
#include <GyverButton.h>
#include <esp_system.h>
#include "CONFIG.h"
#include "menu/settings.h"

extern DisplayType display;
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
extern void resetToFactoryDefaults();
extern void OLED_printMenu(DisplayType &display, byte menuIndex);
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

void renderColorSetting(int previousIndex = -1) {
  colorNeedRedraw = false;
  displayAnimatedMenu(display, colorOptions, COLOR_OPTION_COUNT, colorSelectionWorking, previousIndex);
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
  display.print(FIRMWARE);

  display.setTextSize(1);
  display.setCursor(5, 55);
  display.print("github.com/Teapot174");
  display.drawBitmap(11, 2, image_Teapot_bits, 63, 64, 1);
  display.display();
}

void renderStandbySetting(byte index, int previousIndex = -1) {
  standbyNeedRedraw = false;
  displayAnimatedMenu(display, standbyTimeoutLabels, STANDBY_OPTION_COUNT, index, previousIndex);
}

void handleColorDetail(bool upClick, bool downClick, bool okClick, bool backClick) {
  if (colorNeedRedraw) {
    renderColorSetting();
  }

  if (upClick || downClick) {
    byte previousIndex = colorSelectionWorking;
    colorSelectionWorking = colorSelectionWorking == 0 ? 1 : 0;
    renderColorSetting(previousIndex);
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

void handleStandbyDetail(bool upPress, bool downPress, bool okClick, bool backClick) {
  if (standbyNeedRedraw) {
    renderStandbySetting(standbySelectionIndex);
  }

  if (upPress) {
    byte previousIndex = standbySelectionIndex;
    standbySelectionIndex = (standbySelectionIndex + STANDBY_OPTION_COUNT - 1) % STANDBY_OPTION_COUNT;
    renderStandbySetting(standbySelectionIndex, previousIndex);
  }
  if (downPress) {
    byte previousIndex = standbySelectionIndex;
    standbySelectionIndex = (standbySelectionIndex + 1) % STANDBY_OPTION_COUNT;
    renderStandbySetting(standbySelectionIndex, previousIndex);
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
    ESP.restart();
  } else if (menuIndex == 3) {
    resetToFactoryDefaults();
    ESP.restart();
  } else if (menuIndex == 4) {
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

  static MenuButtonState upHeld;
  static MenuButtonState downHeld;
  bool upPress = isMenuButtonPress(BUTTON_UP, upHeld);
  bool downPress = isMenuButtonPress(BUTTON_DOWN, downHeld);
  bool upClick = buttonUp.isClick();
  bool downClick = buttonDown.isClick();
  bool okClick = buttonOK.isClick();
  bool backClick = buttonBack.isClick();

  if (currentDetail == SETTINGS_COLOR) {
    handleColorDetail(upClick, downClick, okClick, backClick);
    return;
  } else if (currentDetail == SETTINGS_STANDBY) {
    handleStandbyDetail(upPress, downPress, okClick, backClick);
    return;
  } else if (currentDetail == SETTINGS_ABOUT) {
    handleAboutDetail(okClick, backClick);
    return;
  }

  if (upPress) {
    byte previousIndex = settingsMenuIndex;
    settingsMenuIndex = (settingsMenuIndex + SETTINGS_MENU_ITEM_COUNT - 1) % SETTINGS_MENU_ITEM_COUNT;
    displaySettingsMenu(display, settingsMenuIndex, previousIndex);
  }
  if (downPress) {
    byte previousIndex = settingsMenuIndex;
    settingsMenuIndex = (settingsMenuIndex + 1) % SETTINGS_MENU_ITEM_COUNT;
    displaySettingsMenu(display, settingsMenuIndex, previousIndex);
  }
  if (okClick) {
    if (settingsMenuIndex == 0) {
      colorSelectionIndex = colorSelectionIndex == 0 ? 1 : 0;
      applyColorScheme();
      saveConfig();
      displaySettingsMenu(display, settingsMenuIndex);
    } else {
      enterSettingsDetail(settingsMenuIndex);
    }
  }
  if (backClick) {
    returnToMainMenu();
  }
}
