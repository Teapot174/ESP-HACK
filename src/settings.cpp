#include "display.h"
#include <GyverButton.h>
#include <esp_system.h>
#include <SD.h>
#include <Update.h>
#include "CONFIG.h"
#include "Explorer.h"
#include "interface/interface.h"
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
extern bool saveBootLogoPath(const String& path);
extern char bootLogoPath[];

enum SettingsDetail : byte { SETTINGS_NONE, SETTINGS_INTERFACE, SETTINGS_COLOR, SETTINGS_STANDBY, SETTINGS_UPDATE, SETTINGS_ABOUT, SETTINGS_BOOT };
enum InterfaceDetail : byte { INTERFACE_ROOT, INTERFACE_MENU, INTERFACE_BOOT };

static SettingsDetail currentDetail = SETTINGS_NONE;
static InterfaceDetail interfaceDetail = INTERFACE_ROOT;
static bool interfaceMenuOpen = false;
static byte interfaceMenuIndex = 0;
static byte interfaceMenuWorking = 0;
static byte standbySelectionIndex = 0;
static byte colorSelectionWorking = 0;
static bool colorNeedRedraw = true;
static bool standbyNeedRedraw = true;
static bool aboutNeedRedraw = true;

static const char* interfaceItems[] = {"Menu", "Submenu", "Color", "Standby", "Boot Logo"};
static const char* interfaceListItems[] = {"Menu", "Submenu", "Color", "Standby", "Boot Logo"};
static const byte INTERFACE_ITEM_COUNT = sizeof(interfaceItems) / sizeof(interfaceItems[0]);
static const SubmenuItems interfaceSubmenuItems = {interfaceItems, interfaceListItems, INTERFACE_ITEM_COUNT};
static const byte MENU_STYLE_ITEM_COUNT = 4;
static const char* menuStyleItems[] = {"Pages", "List", "Wii", "DSi"};
static const SubmenuItems menuStyleSubmenuItems = {
  menuStyleItems, menuStyleItems, MENU_STYLE_ITEM_COUNT
};

static void renderInterfaceMenu(int previousIndex = -1) {
  displaySubmenu(display, interfaceSubmenuItems, interfaceMenuIndex, previousIndex);
}

static void exitInterfaceDetail(bool keepSelectedItem = false) {
  currentDetail = SETTINGS_INTERFACE;
  interfaceDetail = INTERFACE_ROOT;
  if (!keepSelectedItem) interfaceMenuIndex = 0;
  renderInterfaceMenu();
}

void Interface() {
  interfaceMenuOpen = true;
  currentDetail = SETTINGS_INTERFACE;
  interfaceDetail = INTERFACE_ROOT;
  interfaceMenuIndex = 0;
  renderInterfaceMenu();
}

static int hexDigitValue(int character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

bool OLED_printBootLogo(DisplayType &display) {
  const size_t bitmapSize = (SCREEN_WIDTH * SCREEN_HEIGHT) / 8;
  static uint8_t bitmap[bitmapSize];

  // No saved path or an unavailable SD file: the caller draws the built-in logo.
  if (bootLogoPath[0] == '\0') return false;
  File logoFile = SD.open(bootLogoPath, FILE_READ);
  if (!logoFile || logoFile.isDirectory()) {
    if (logoFile) logoFile.close();
    return false;
  }

  size_t byteCount = 0;
  while (logoFile.available() && byteCount < bitmapSize) {
    int character = logoFile.read();
    if (character != '0' || logoFile.peek() != 'x') continue;
    logoFile.read();
    int high = hexDigitValue(logoFile.read());
    int low = hexDigitValue(logoFile.read());
    if (high >= 0 && low >= 0) {
      bitmap[byteCount++] = static_cast<uint8_t>((high << 4) | low);
    }
  }
  logoFile.close();

  if (byteCount != bitmapSize) return false;
  display.clearDisplay();
  display.drawBitmap(0, 0, bitmap, SCREEN_WIDTH, SCREEN_HEIGHT, SH110X_WHITE);
  display.display();
  return true;
}

static const char* updateExts[] = {".bin"};
static const int UPDATE_MAX_FILES = 50;
static ExplorerEntry updateFileList[UPDATE_MAX_FILES];
static ExplorerState updateExplorer;
static ExplorerConfig updateExplorerCfg = {"/", updateExts, 1, false, false, true, false};

static const char* bootLogoExts[] = {".txt"};
static const int BOOT_LOGO_MAX_FILES = 50;
static const char* DEFAULT_BOOT_LOGO_PATH = "/bootlogo/esphack.txt";
static ExplorerEntry bootLogoFileList[BOOT_LOGO_MAX_FILES];
static ExplorerState bootLogoExplorer;
static ExplorerConfig bootLogoExplorerCfg = {"/bootlogo", bootLogoExts, 1, true, true, true, true};

void ensureDefaultBootLogoFile() {
  if (!SD.exists("/bootlogo") && !SD.mkdir("/bootlogo")) return;

  if (!SD.exists(DEFAULT_BOOT_LOGO_PATH)) {
    File logoFile = SD.open(DEFAULT_BOOT_LOGO_PATH, FILE_WRITE);
    if (!logoFile) return;

    const size_t bitmapSize = (SCREEN_WIDTH * SCREEN_HEIGHT) / 8;
    for (size_t i = 0; i < bitmapSize; i++) {
      uint8_t value = pgm_read_byte(image_ESPHACK_bits + i);
      logoFile.print(F("0x"));
      if (value < 0x10) logoFile.print('0');
      logoFile.print(value, HEX);
      if (i + 1 < bitmapSize) logoFile.print(F(", "));
      if (i % 16 == 15 || i + 1 == bitmapSize) logoFile.println();
    }
    logoFile.close();
  }

  if (bootLogoPath[0] == '\0') {
    saveBootLogoPath(DEFAULT_BOOT_LOGO_PATH);
  }
}

void Boot() {
  if (!ensureSDReadyInteractive(true)) {
    renderInterfaceMenu();
    return;
  }
  ensureDefaultBootLogoFile();
  interfaceDetail = INTERFACE_BOOT;
  currentDetail = SETTINGS_BOOT;
  ExplorerInit(bootLogoExplorer, bootLogoFileList, BOOT_LOGO_MAX_FILES, bootLogoExplorerCfg);
  ExplorerLoad(bootLogoExplorer, bootLogoExplorerCfg);
  ExplorerDraw(bootLogoExplorer, display);
}

static String selectedExplorerPath(const ExplorerState& explorer) {
  if (explorer.currentDir.endsWith("/")) return explorer.currentDir + explorer.selectedFile;
  return explorer.currentDir + "/" + explorer.selectedFile;
}

static void showBootLogoSaveResult(bool ok) {
  if (ok && OLED_printBootLogo(display)) {
    delay(1000);
    return;
  }

  OLED_printLogo(display);
  delay(1000);
}

static void handleBootLogoDetail(bool upClick, bool downClick, bool okClick, bool backClick) {
  ExplorerAction action = ExplorerHandle(
    bootLogoExplorer,
    bootLogoExplorerCfg,
    display,
    upClick,
    downClick,
    okClick,
    backClick,
    buttonBack.isHolded()
  );

  if (action == EXPLORER_SELECT_FILE) {
    bool saved = saveBootLogoPath(selectedExplorerPath(bootLogoExplorer));
    showBootLogoSaveResult(saved);
    if (saved) {
      currentDetail = SETTINGS_INTERFACE;
      interfaceDetail = INTERFACE_ROOT;
      interfaceMenuIndex = INTERFACE_ITEM_COUNT - 1;
      renderInterfaceMenu();
    }
    else ExplorerDraw(bootLogoExplorer, display);
  } else if (action == EXPLORER_EXIT) {
    exitInterfaceDetail(true);
  }
}

void exitSettingsDetail() {
  currentDetail = SETTINGS_NONE;
  if (interfaceMenuOpen) {
    currentDetail = SETTINGS_INTERFACE;
    interfaceDetail = INTERFACE_ROOT;
    renderInterfaceMenu();
  } else {
    displaySettingsMenu(display, settingsMenuIndex);
  }
  colorNeedRedraw = true;
  standbyNeedRedraw = true;
  aboutNeedRedraw = true;
}

void renderColorSetting(int previousIndex = -1) {
  colorNeedRedraw = false;
  displaySubmenu(display, colorOptions, COLOR_OPTION_COUNT, colorSelectionWorking, previousIndex);
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
  displaySubmenu(display, standbyTimeoutLabels, STANDBY_OPTION_COUNT, index, previousIndex);
}

static void drawUpdateProgress(uint8_t progress) {
  display.fillRect(14, 36, progress, 10, 1);
  display.drawRect(14, 36, 100, 10, 1);
  display.display();
}

static bool flashFirmwareFromSD(const String& fileName) {
  static const size_t APP_OFFSET = 0x10000;
  static const size_t BOOTLOADER_OFFSET = 0x1000;
  static const size_t PARTITION_TABLE_OFFSET = 0x8000;

  String path = String("/") + fileName;
  File firmware = SD.open(path, FILE_READ);
  if (!firmware || firmware.isDirectory() || firmware.size() <= APP_OFFSET) {
    if (firmware) firmware.close();
    return false;
  }

  // Accept only a merged flash image intended for offset 0x0.  Its bootloader,
  // partition table, and application start at the standard ESP32 offsets.
  uint8_t marker = 0;
  if (!firmware.seek(BOOTLOADER_OFFSET) || firmware.read(&marker, 1) != 1 || marker != 0xE9 ||
      !firmware.seek(PARTITION_TABLE_OFFSET) || firmware.read(&marker, 1) != 1 || marker != 0xAA ||
      !firmware.seek(APP_OFFSET) || firmware.read(&marker, 1) != 1 || marker != 0xE9 ||
      !firmware.seek(APP_OFFSET)) {
    firmware.close();
    return false;
  }

  display.clearDisplay();
  display.setTextColor(1);
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setCursor(32, 18);
  display.print("Flashing...");
  display.drawRect(14, 36, 100, 10, 1);
  display.display();

  const size_t totalSize = firmware.size() - APP_OFFSET;
  if (!Update.begin(totalSize, U_FLASH)) {
    firmware.close();
    return false;
  }

  uint8_t buffer[4096];
  size_t writtenTotal = 0;
  uint8_t shownProgress = 0;
  while (firmware.available()) {
    size_t bytesRead = firmware.read(buffer, sizeof(buffer));
    if (bytesRead == 0 || Update.write(buffer, bytesRead) != bytesRead) {
      Update.abort();
      firmware.close();
      return false;
    }

    writtenTotal += bytesRead;
    uint8_t progress = (writtenTotal * 100UL) / totalSize;
    if (progress != shownProgress) {
      drawUpdateProgress(progress);
      shownProgress = progress;
    }
    yield();
  }
  firmware.close();

  if (!Update.end(true) || !Update.isFinished()) {
    return false;
  }

  drawUpdateProgress(100);
  delay(250);
  ESP.restart();
  return true;
}

static void handleUpdateDetail(bool upClick, bool downClick, bool okClick, bool backClick) {
  ExplorerAction action = ExplorerHandle(
    updateExplorer,
    updateExplorerCfg,
    display,
    upClick,
    downClick,
    okClick,
    backClick,
    buttonBack.isHolded()
  );

  if (action == EXPLORER_SELECT_FILE) {
    if (!flashFirmwareFromSD(updateExplorer.selectedFile)) {
      display.clearDisplay();
      display.setTextColor(1);
      display.setTextSize(1);
      display.setCursor(20, 28);
      display.print(F("Update failed"));
      display.display();
      delay(1200);
      ExplorerDraw(updateExplorer, display);
    }
  } else if (action == EXPLORER_EXIT) {
    exitSettingsDetail();
  }
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
    Interface();
  } else if (menuIndex == 1) {
    ESP.restart();
  } else if (menuIndex == 2) {
    resetToFactoryDefaults();
    ESP.restart();
  } else if (menuIndex == 3) {
    if (!ensureSDReadyInteractive(true)) {
      displaySettingsMenu(display, settingsMenuIndex);
      return;
    }
    currentDetail = SETTINGS_UPDATE;
    ExplorerInit(updateExplorer, updateFileList, UPDATE_MAX_FILES, updateExplorerCfg);
    ExplorerLoad(updateExplorer, updateExplorerCfg);
    ExplorerDraw(updateExplorer, display);
  } else if (menuIndex == 4) {
    currentDetail = SETTINGS_ABOUT;
    aboutNeedRedraw = true;
    renderAboutSetting();
  }
}

static void handleInterfaceMenu(bool upPress, bool downPress, bool okClick, bool backClick) {
  if (interfaceDetail == INTERFACE_MENU) {
    if (upPress) {
      byte previousIndex = interfaceMenuWorking;
      interfaceMenuWorking = (interfaceMenuWorking + MENU_STYLE_ITEM_COUNT - 1) % MENU_STYLE_ITEM_COUNT;
      displaySubmenu(display, menuStyleSubmenuItems, interfaceMenuWorking, previousIndex);
    }
    if (downPress) {
      byte previousIndex = interfaceMenuWorking;
      interfaceMenuWorking = (interfaceMenuWorking + 1) % MENU_STYLE_ITEM_COUNT;
      displaySubmenu(display, menuStyleSubmenuItems, interfaceMenuWorking, previousIndex);
    }
    if (okClick) {
      if (interfaceMenuWorking < MENU_STYLE_ITEM_COUNT) {
        menu = interfaceMenuWorking;
        saveConfig();
      }
      exitInterfaceDetail();
    }
    if (backClick) exitInterfaceDetail();
    return;
  }

  if (upPress) {
    byte previousIndex = interfaceMenuIndex;
    interfaceMenuIndex = (interfaceMenuIndex + INTERFACE_ITEM_COUNT - 1) % INTERFACE_ITEM_COUNT;
    renderInterfaceMenu(previousIndex);
  }
  if (downPress) {
    byte previousIndex = interfaceMenuIndex;
    interfaceMenuIndex = (interfaceMenuIndex + 1) % INTERFACE_ITEM_COUNT;
    renderInterfaceMenu(previousIndex);
  }
  if (okClick) {
    if (interfaceMenuIndex == 0) {
      interfaceDetail = INTERFACE_MENU;
      interfaceMenuWorking = menu;
      displaySubmenu(display, menuStyleSubmenuItems, interfaceMenuWorking);
    } else if (interfaceMenuIndex == 1) {
      submenu = submenu == 0 ? 1 : 0;
      saveConfig();
      renderInterfaceMenu();
    } else if (interfaceMenuIndex == 2) {
      colorSelectionIndex = colorSelectionIndex == 0 ? 1 : 0;
      applyColorScheme();
      saveConfig();
      renderInterfaceMenu();
    } else if (interfaceMenuIndex == 3) {
      currentDetail = SETTINGS_STANDBY;
      standbySelectionIndex = standbyTimeoutIndex;
      standbyNeedRedraw = true;
      renderStandbySetting(standbySelectionIndex);
    } else if (interfaceMenuIndex == 4) {
      Boot();
    }
  }
  if (backClick) {
    interfaceMenuOpen = false;
    currentDetail = SETTINGS_NONE;
    displaySettingsMenu(display, settingsMenuIndex);
  }
}

void handleSettingsSubmenu() {
  buttonUp.tick();
  buttonDown.tick();
  buttonOK.tick();
  buttonBack.tick();

  static MenuButtonState upHeld;
  static MenuButtonState downHeld;
  const bool isListSettingsSubmenu =
    submenu == 1 && currentDetail != SETTINGS_UPDATE && currentDetail != SETTINGS_ABOUT;
  const unsigned long repeatDelayMs =
    getMenuSubmenuRepeatDelay(isListSettingsSubmenu);
  bool upPress = isMenuButtonPress(BUTTON_UP, upHeld, repeatDelayMs);
  bool downPress = isMenuButtonPress(BUTTON_DOWN, downHeld, repeatDelayMs);
  bool upClick = buttonUp.isClick();
  bool downClick = buttonDown.isClick();
  bool okClick = buttonOK.isClick();
  bool backClick = buttonBack.isClick();

  if (currentDetail == SETTINGS_INTERFACE) {
    handleInterfaceMenu(upPress, downPress, okClick, backClick);
    return;
  } else if (currentDetail == SETTINGS_BOOT) {
    handleBootLogoDetail(upClick, downClick, okClick, backClick);
    return;
  } else if (currentDetail == SETTINGS_COLOR) {
    handleColorDetail(upClick, downClick, okClick, backClick);
    return;
  } else if (currentDetail == SETTINGS_STANDBY) {
    handleStandbyDetail(upPress, downPress, okClick, backClick);
    return;
  } else if (currentDetail == SETTINGS_UPDATE) {
    handleUpdateDetail(upClick, downClick, okClick, backClick);
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
    enterSettingsDetail(settingsMenuIndex);
  }
  if (backClick) {
    interfaceMenuOpen = false;
    returnToMainMenu();
  }
}
