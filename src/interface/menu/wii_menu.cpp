#include "list_menu.h"
#include "wii_menu.h"

#include <U8g2_for_Adafruit_GFX.h>
#include <cstring>

static const char* const wiiMenuLabels[] = {
  "WiFi", "BLE", "SubGHz", "Infrared", "GPIO", "Games", "Settings"
};
static const int8_t wiiIconXOffsets[] = {0, 1, 0, 0, 0, 0, 0};
static const int8_t wiiLabelXOffsets[] = {0, 0, 0, 0, 0, 0, 0};

static U8G2_FOR_ADAFRUIT_GFX wiiText;
static byte wiiAnimationFrames[MAIN_MENU_ITEM_COUNT] = {};
static byte wiiActiveItem = 0;
static unsigned long wiiAnimationTick = 0;
static bool wiiAnimationStarted = false;

static void beginWiiText(DisplayType& display, const uint8_t* font) {
  wiiText.begin(display);
  wiiText.setFontMode(1);
  wiiText.setForegroundColor(SH110X_WHITE);
  wiiText.setBackgroundColor(SH110X_BLACK);
  wiiText.setFont(font);
}

static void drawXbm(DisplayType& display, int16_t x, int16_t y,
                    uint8_t width, uint8_t height, const uint8_t* bitmap,
                    uint16_t color = SH110X_WHITE) {
  const uint8_t bytesPerRow = (width + 7) / 8;
  for (byte row = 0; row < height; ++row) {
    for (byte column = 0; column < width; ++column) {
      if (bitmap[row * bytesPerRow + column / 8] & (1 << (column & 7))) {
        display.drawPixel(x + column, y + row, color);
      }
    }
  }
}

static void drawWiiIcon(DisplayType& display, byte item, int16_t x, int16_t y,
                        uint16_t color = SH110X_WHITE) {
  const MenuIcon& icon = *menuIcons[item];
  const byte frame = wiiAnimationFrames[item] % icon.frameCount;
  const int16_t centeredX = x + 7 - icon.width / 2;
  const int16_t centeredY = y + 7 - icon.height / 2;
  drawXbm(display, centeredX, centeredY, icon.width, icon.height, icon.frames[frame], color);
}

static void drawCenteredWiiLabel(DisplayType& display, const char* label,
                                 int16_t centerX, int16_t baselineY,
                                 uint16_t color) {
  beginWiiText(display, u8g2_font_haxrcorp4089_tr);
  wiiText.setForegroundColor(color);
  wiiText.setBackgroundColor(color == SH110X_WHITE ? SH110X_BLACK : SH110X_WHITE);

  char shortLabel[9];
  strncpy(shortLabel, label, sizeof(shortLabel) - 1);
  shortLabel[sizeof(shortLabel) - 1] = '\0';
  const int16_t textWidth = wiiText.getUTF8Width(shortLabel);
  wiiText.setCursor(centerX - textWidth / 2, baselineY);
  wiiText.print(shortLabel);
}

static void drawMomentumFrame(DisplayType& display, int16_t x, int16_t y,
                              int16_t width, int16_t height,
                              uint16_t color = SH110X_WHITE) {
  display.drawLine(x + 2, y, x + width - 2, y, color);
  display.drawLine(x + 1, y + height - 1, x + width, y + height - 1, color);
  display.drawLine(x + 2, y + height, x + width - 1, y + height, color);
  display.drawLine(x, y + 2, x, y + height - 2, color);
  display.drawLine(x + width - 1, y + 1, x + width - 1, y + height - 2, color);
  display.drawLine(x + width, y + 2, x + width, y + height - 2, color);
  display.drawPixel(x + 1, y + 1, color);
}

static void fillMomentumSlightlyRoundedBox(DisplayType& display, int16_t x, int16_t y,
                                           int16_t width, int16_t height,
                                           uint16_t color = SH110X_WHITE) {
  display.fillRect(x + 1, y, width - 2, height, color);
  display.fillRect(x, y + 1, width, height - 2, color);
}

static byte getWiiWindowStart(byte menuIndex) {
  if (MAIN_MENU_ITEM_COUNT > 6 && menuIndex >= 4) {
    if (menuIndex >= MAIN_MENU_ITEM_COUNT - 2 + (MAIN_MENU_ITEM_COUNT % 2)) {
      return menuIndex - (menuIndex % 2) - 4;
    }
    return menuIndex - (menuIndex % 2) - 2;
  }
  return 0;
}

static void renderWiiMenu(DisplayType& display, byte menuIndex) {
  display.clearDisplay();

  const byte windowStart = getWiiWindowStart(menuIndex);
  for (byte slot = 0; slot < 6; ++slot) {
    const byte itemIndex = windowStart + slot;
    if (itemIndex >= MAIN_MENU_ITEM_COUNT) continue;

    const int16_t x = (slot / 2) * 43 + 1;
    const int16_t y = (slot % 2) * 32;
    const bool selected = itemIndex == menuIndex;
    const uint16_t foreground = selected ? SH110X_BLACK : SH110X_WHITE;

    if (selected) {
      fillMomentumSlightlyRoundedBox(display, x, y, 40, 30);
    }

    drawWiiIcon(display, itemIndex, x + 13 + wiiIconXOffsets[itemIndex], y + 3, foreground);
    drawCenteredWiiLabel(
      display,
      wiiMenuLabels[itemIndex],
      x + 20 + wiiLabelXOffsets[itemIndex],
      y + 26,
      foreground);

    if (!selected) {
      drawMomentumFrame(display, x, y, 40, 30);
    }
  }

  display.display();
}

void displayWiiMenu(DisplayType& display, byte menuIndex, int previousIndex) {
  const unsigned long now = millis();
  if (!wiiAnimationStarted) {
    wiiAnimationStarted = true;
    wiiActiveItem = menuIndex;
    wiiAnimationTick = now;
  } else if (wiiActiveItem != menuIndex) {
    wiiAnimationFrames[wiiActiveItem] = 0;
    wiiAnimationFrames[menuIndex] = 0;
    wiiActiveItem = menuIndex;
    wiiAnimationTick = now;
  } else if (now - wiiAnimationTick >= 333) {
    wiiAnimationTick = now;
    wiiAnimationFrames[wiiActiveItem] =
      (wiiAnimationFrames[wiiActiveItem] + 1) % menuIcons[wiiActiveItem]->frameCount;
  }

  if (previousIndex >= 0 && previousIndex < MAIN_MENU_ITEM_COUNT && previousIndex != menuIndex) {
    const uint8_t steps = 4;
    for (uint8_t step = 0; step < steps; ++step) {
      renderWiiMenu(display, menuIndex);
      delay(2);
    }
    return;
  }

  renderWiiMenu(display, menuIndex);
}

void resetWiiMenuAnimation(byte menuIndex) {
  if (menuIndex >= MAIN_MENU_ITEM_COUNT) return;
  wiiAnimationFrames[menuIndex] = 0;
  wiiActiveItem = menuIndex;
  wiiAnimationTick = millis();
  wiiAnimationStarted = true;
}
