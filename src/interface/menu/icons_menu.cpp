#include "icons_menu.h"
#include "display.h"

// Стиль «Icons»: вертикальный список, у каждого пункта нарисованная иконка.
// Иконки рисуются примитивами, без отдельных битмапов — 12x12 точек.

extern const char* menuItems[];
static constexpr byte MENU_COUNT = 7;
static constexpr int16_t ROW_H = 12;
static constexpr byte ROWS_VISIBLE = 5;
static constexpr int16_t ICON_BOX = 12;

static void drawMenuIcon(DisplayType &display, byte index, int16_t x, int16_t y, uint16_t color) {
  switch (index) {
    case 0:  // WiFi: точка и две дуги
      display.fillRect(x + 5, y + 9, 2, 2, color);
      display.drawLine(x + 3, y + 7, x + 6, y + 4, color);
      display.drawLine(x + 6, y + 4, x + 9, y + 7, color);
      display.drawLine(x + 1, y + 6, x + 6, y + 1, color);
      display.drawLine(x + 6, y + 1, x + 11, y + 6, color);
      break;
    case 1:  // Bluetooth: вертикаль и две диагонали
      display.drawFastVLine(x + 6, y + 1, 11, color);
      display.drawLine(x + 6, y + 1, x + 9, y + 3, color);
      display.drawLine(x + 9, y + 3, x + 3, y + 9, color);
      display.drawLine(x + 6, y + 11, x + 9, y + 9, color);
      display.drawLine(x + 9, y + 9, x + 3, y + 3, color);
      break;
    case 2:  // SubGHz: штырь и волны
      display.drawFastVLine(x + 5, y + 4, 8, color);
      display.drawLine(x + 3, y + 3, x + 5, y + 1, color);
      display.drawLine(x + 5, y + 1, x + 7, y + 3, color);
      display.drawFastVLine(x + 9, y + 5, 2, color);
      display.drawFastVLine(x + 11, y + 7, 2, color);
      break;
    case 3:  // Infrared: диод и лучи
      display.fillTriangle(x + 4, y + 5, x + 9, y + 2, x + 9, y + 8, color);
      display.drawFastHLine(x + 1, y + 2, 3, color);
      display.drawFastHLine(x + 1, y + 5, 2, color);
      display.drawFastHLine(x + 1, y + 8, 3, color);
      break;
    case 4:  // GPIO: чип с ножками
      display.drawRect(x + 3, y + 3, 6, 6, color);
      for (byte i = 0; i < 3; i++) {
        display.drawFastHLine(x + 1, y + 4 + i * 2, 2, color);
        display.drawFastHLine(x + 9, y + 4 + i * 2, 2, color);
      }
      display.drawFastVLine(x + 6, y + 1, 2, color);
      display.drawFastVLine(x + 6, y + 9, 2, color);
      break;
    case 5:  // Games: крестовина
      display.fillRect(x + 4, y + 1, 4, 10, color);
      display.fillRect(x + 1, y + 4, 10, 4, color);
      break;
    default:  // Settings: шестерёнка
      display.drawCircle(x + 6, y + 6, 3, color);
      display.drawFastVLine(x + 6, y + 1, 2, color);
      display.drawFastVLine(x + 6, y + 9, 2, color);
      display.drawFastHLine(x + 1, y + 6, 2, color);
      display.drawFastHLine(x + 9, y + 6, 2, color);
      break;
  }
}

void displayIconsMenu(DisplayType &display, byte menuIndex, int previousIndex) {
  (void)previousIndex;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextWrap(false);

  int16_t first = (int16_t)menuIndex - 2;
  if (first > (int16_t)MENU_COUNT - ROWS_VISIBLE) first = MENU_COUNT - ROWS_VISIBLE;
  if (first < 0) first = 0;

  for (byte row = 0; row < ROWS_VISIBLE; row++) {
    byte item = (byte)(first + row);
    if (item >= MENU_COUNT) break;

    int16_t y = row * ROW_H;
    bool selected = (item == menuIndex);
    uint16_t color = SH110X_WHITE;

    if (selected) {
      display.fillRect(0, y, SCREEN_WIDTH, ROW_H, SH110X_WHITE);
      color = SH110X_BLACK;
    }

    drawMenuIcon(display, item, 3, y + 1, color);

    display.setTextColor(color);
    display.setCursor(ICON_BOX + 7, y + 3);
    display.print(menuItems[item]);
  }

  display.setTextColor(SH110X_WHITE);
  display.display();
}

void resetIconsMenuAnimation(byte menuIndex) {
  (void)menuIndex;
}
