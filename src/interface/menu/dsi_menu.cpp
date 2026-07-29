#include "dsi_menu.h"
#include "list_menu.h"

#include <U8g2_for_Adafruit_GFX.h>
#include <cstring>

extern const char* menuItems[];

static U8G2_FOR_ADAFRUIT_GFX dsiText;
static byte dsiAnimationFrames[MAIN_MENU_ITEM_COUNT] = {};
static byte dsiActiveItem = 0;
static unsigned long dsiAnimationTick = 0;
static bool dsiAnimationStarted = false;
static const int8_t dsiIconXOffsets[MAIN_MENU_ITEM_COUNT] = {-2, 0, 0, 0, 0, 0, 0};

static void beginDsiText(DisplayType& display, const uint8_t* font, uint16_t color) {
  dsiText.begin(display);
  dsiText.setFontMode(1);
  dsiText.setForegroundColor(color);
  dsiText.setBackgroundColor(color == SH110X_WHITE ? SH110X_BLACK : SH110X_WHITE);
  dsiText.setFont(font);
}

static void drawDsiIcon(DisplayType& display, byte item, int16_t x, int16_t y,
                        uint16_t color) {
  const MenuIcon& icon = *menuIcons[item];
  const byte frame = dsiAnimationFrames[item] % icon.frameCount;
  const uint8_t bytesPerRow = (icon.width + 7) / 8;

  for (byte row = 0; row < icon.height; ++row) {
    for (byte column = 0; column < icon.width; ++column) {
      if (icon.frames[frame][row * bytesPerRow + column / 8] & (1 << (column & 7))) {
        display.drawPixel(x + column, y + row, color);
      }
    }
  }
}

static void drawDsiCenteredLabel(DisplayType& display, const char* label,
                                 int16_t centerX, int16_t baselineY,
                                 uint16_t color) {
  beginDsiText(display, u8g2_font_helvB08_tr, color);
  char shortLabel[17];
  strncpy(shortLabel, label, sizeof(shortLabel) - 1);
  shortLabel[sizeof(shortLabel) - 1] = '\0';
  const int16_t textWidth = dsiText.getUTF8Width(shortLabel);
  dsiText.setCursor(centerX - textWidth / 2, baselineY);
  dsiText.print(shortLabel);
}

static void drawDsiStartLabel(DisplayType& display, int16_t centerX, int16_t baselineY) {
  beginDsiText(display, u8g2_font_5x7_tr, SH110X_WHITE);
  const char* parts[] = {"S", "TAR", "T"};
  const int16_t centers[] = {
    static_cast<int16_t>(centerX - 9), centerX, static_cast<int16_t>(centerX + 9)
  };
  for (byte i = 0; i < 3; ++i) {
    const int16_t textWidth = dsiText.getUTF8Width(parts[i]);
    dsiText.setCursor(centers[i] - textWidth / 2, baselineY);
    dsiText.print(parts[i]);
  }
}

static void drawDsiSlightlyRoundedFrame(DisplayType& display, int16_t x, int16_t y,
                                        int16_t width, int16_t height) {
  display.drawRoundRect(x, y, width, height, 1, SH110X_WHITE);
}

static void drawDsiBoldRoundedFrame(DisplayType& display, int16_t x, int16_t y,
                                    int16_t width, int16_t height) {
  display.fillRoundRect(x, y, width + 1, height + 1, 3, SH110X_BLACK);
  display.drawLine(x + 3, y, x + width - 3, y, SH110X_WHITE);
  display.drawLine(x + 2, y + 1, x + width - 2, y + 1, SH110X_WHITE);
  display.drawLine(x, y + 3, x, y + height - 3, SH110X_WHITE);
  display.drawLine(x + 1, y + 2, x + 1, y + height - 2, SH110X_WHITE);
  display.drawLine(x + width, y + 3, x + width, y + height - 3, SH110X_WHITE);
  display.drawLine(x + width - 1, y + 2, x + width - 1, y + height - 2, SH110X_WHITE);
  display.drawLine(x + 3, y + height, x + width - 3, y + height, SH110X_WHITE);
  display.drawLine(x + 2, y + height - 1, x + width - 2, y + height - 1, SH110X_WHITE);
  display.drawPixel(x + 2, y + 2, SH110X_WHITE);
  display.drawPixel(x + 3, y + 2, SH110X_WHITE);
  display.drawPixel(x + 2, y + 3, SH110X_WHITE);
  display.drawPixel(x + width - 2, y + 2, SH110X_WHITE);
  display.drawPixel(x + width - 3, y + 2, SH110X_WHITE);
  display.drawPixel(x + width - 2, y + 3, SH110X_WHITE);
  display.drawPixel(x + 2, y + height - 2, SH110X_WHITE);
  display.drawPixel(x + 3, y + height - 2, SH110X_WHITE);
  display.drawPixel(x + 2, y + height - 3, SH110X_WHITE);
  display.drawPixel(x + width - 2, y + height - 2, SH110X_WHITE);
  display.drawPixel(x + width - 3, y + height - 2, SH110X_WHITE);
  display.drawPixel(x + width - 2, y + height - 3, SH110X_WHITE);
}

static void drawDsiHeader(DisplayType& display) {
  display.drawRoundRect(0, 0, 128, 18, 3, SH110X_WHITE);
}

static void drawDsiHeaderArrow(DisplayType& display) {
  display.drawLine(60, 18, 64, 26, SH110X_WHITE);
  display.drawLine(64, 26, 68, 18, SH110X_WHITE);
  display.drawLine(60, 17, 68, 17, SH110X_BLACK);
  display.fillRect(62, 21, 5, 2, SH110X_BLACK);
}

static int16_t getDsiScrollbarX(byte position, byte total) {
  if (!total) return 0;
  return static_cast<int16_t>(static_cast<float>(128) * position / total);
}

static void drawDsiScrollbar(DisplayType& display, byte position, byte total,
                             int16_t thumbX = -1) {
  for (byte x = 0; x < 128; x += 2) display.drawPixel(x, 62, SH110X_WHITE);
  if (total) {
    const int16_t blockWidth = max<int16_t>(1, 128 / total);
    if (thumbX < 0) thumbX = getDsiScrollbarX(position, total);
    display.fillRect(thumbX, 61, blockWidth, 3, SH110X_WHITE);
  }
}

static void renderDsiMenu(DisplayType& display, byte menuIndex, int16_t scrollbarX = -1) {
  display.clearDisplay();
  drawDsiHeader(display);
  const int16_t centerX = 64;
  const int16_t centerY = 36;

  for (int8_t offset = -2; offset <= 2; ++offset) {
    const byte item = (menuIndex + MAIN_MENU_ITEM_COUNT + offset) % MAIN_MENU_ITEM_COUNT;
    const bool selected = offset == 0;
    const int16_t width = selected ? 30 : 24;
    const int16_t height = selected ? 30 : 26;
    const int16_t itemX = centerX + (selected ? 0 : (width + 6) * offset);
    const int16_t itemY = centerY + (selected ? 0 : 2);
    const int16_t frameX = itemX - width / 2;
    const int16_t frameY = itemY - height / 2;

    if (selected) {
      drawDsiBoldRoundedFrame(display, frameX, frameY, width, height + 5);
      drawDsiCenteredLabel(display, menuItems[item], centerX, frameY - 8, SH110X_WHITE);
      drawDsiStartLabel(display, centerX, itemY + height / 2 + 1);
    } else {
      drawDsiSlightlyRoundedFrame(display, frameX, frameY, width, height);
    }

    drawDsiIcon(display, item, itemX - 7 + dsiIconXOffsets[item], itemY - 7,
                SH110X_WHITE);
  }

  drawDsiHeaderArrow(display);
  drawDsiScrollbar(display, menuIndex, MAIN_MENU_ITEM_COUNT, scrollbarX);
  display.display();
}

void displayDsiMenu(DisplayType& display, byte menuIndex, int previousIndex) {
  const unsigned long now = millis();
  if (!dsiAnimationStarted) {
    dsiAnimationStarted = true;
    dsiActiveItem = menuIndex;
    dsiAnimationTick = now;
  } else if (dsiActiveItem != menuIndex) {
    dsiAnimationFrames[dsiActiveItem] = 0;
    dsiAnimationFrames[menuIndex] = 0;
    dsiActiveItem = menuIndex;
    dsiAnimationTick = now;
  } else if (now - dsiAnimationTick >= 333) {
    dsiAnimationTick = now;
    dsiAnimationFrames[dsiActiveItem] =
      (dsiAnimationFrames[dsiActiveItem] + 1) % menuIcons[dsiActiveItem]->frameCount;
  }

  if (previousIndex >= 0 && previousIndex < MAIN_MENU_ITEM_COUNT && previousIndex != menuIndex) {
    const uint8_t steps = 10;
    const int16_t startX = getDsiScrollbarX(previousIndex, MAIN_MENU_ITEM_COUNT);
    const int16_t endX = getDsiScrollbarX(menuIndex, MAIN_MENU_ITEM_COUNT);
    for (uint8_t step = 1; step <= steps; ++step) {
      renderDsiMenu(
        display,
        menuIndex,
        interpolateMenuY(startX, endX, step, steps));
    }
    return;
  }

  renderDsiMenu(display, menuIndex);
}

void resetDsiMenuAnimation(byte menuIndex) {
  if (menuIndex >= MAIN_MENU_ITEM_COUNT) return;
  dsiAnimationFrames[menuIndex] = 0;
  dsiActiveItem = menuIndex;
  dsiAnimationTick = millis();
  dsiAnimationStarted = true;
}
