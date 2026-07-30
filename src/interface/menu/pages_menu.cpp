#include "../interface.h"
#include "pages_menu.h"

static constexpr int16_t MAIN_MENU_DOT_START_Y = 6;
static constexpr int16_t MAIN_MENU_DOT_STEP_Y = 8;

static void drawMainMenuDots(DisplayType& display) {
  for (byte i = 0; i < MENU_ITEM_COUNT; i++) {
    display.drawBitmap(122, MAIN_MENU_DOT_START_Y + (i * MAIN_MENU_DOT_STEP_Y), image_DOT_bits, 4, 4, 1);
  }
}

static int16_t getMainMenuDotY(byte menuIndex) {
  return MAIN_MENU_DOT_START_Y + (menuIndex * MAIN_MENU_DOT_STEP_Y);
}

static void drawMainMenuSelectedDot(DisplayType& display, int16_t y) {
  display.drawBitmap(122, y, image_DOTsel_bits, 4, 4, 1);
}

static void drawMainMenuPage(DisplayType& display, byte menuIndex, int16_t yOffset = 0) {
  if (menuIndex == 0) {
    display.drawBitmap(6, 17 + yOffset, image_WiFi_bits, 39, 31, 1);
    display.setTextColor(1);
    display.setTextSize(2);
    display.setTextWrap(false);
    display.setCursor(59, 25 + yOffset);
    display.print("WiFi");
  } else if (menuIndex == 1) {
    display.drawBitmap(15, 13 + yOffset, image_Bluetooth_bits, 22, 38, 1);
    display.setTextColor(1);
    display.setTextSize(2);
    display.setTextWrap(false);
    display.setCursor(62, 25 + yOffset);
    display.print("BLE");
  } else if (menuIndex == 2) {
    display.drawBitmap(5, 14 + yOffset, image_SubGHz_bits, 39, 39, 1);
    display.setTextColor(1);
    display.setTextSize(2);
    display.setTextWrap(false);
    display.setCursor(48, 25 + yOffset);
    display.print("SubGHz");
  } else if (menuIndex == 3) {
    display.drawBitmap(8, 15 + yOffset, image_IR_bits, 34, 35, 1);
    display.setTextColor(1);
    display.setTextSize(2);
    display.setTextWrap(false);
    display.setCursor(69, 25 + yOffset);
    display.print("IR");
  } else if (menuIndex == 4) {
    display.drawBitmap(7, 12 + yOffset, image_GPIO_bits, 38, 40, 1);
    display.setTextColor(1);
    display.setTextSize(2);
    display.setTextWrap(false);
    display.setCursor(59, 25 + yOffset);
    display.print("GPIO");
  } else if (menuIndex == 5) {
    display.drawBitmap(7, 16 + yOffset, image_Games_bits, 32, 32, 1);
    display.setTextColor(1);
    display.setTextSize(2);
    display.setTextWrap(false);
    display.setCursor(52, 25 + yOffset);
    display.print("Games");
  } else if (menuIndex == 6) {
    display.drawBitmap(7, 16 + yOffset, image_Config_bits, 32, 32, 1);
    display.setTextColor(1);
    display.setTextSize(2);
    display.setTextWrap(false);
    display.setCursor(46, 25 + yOffset);
    display.print("Config");
  }
}

void displayPagesMenu(DisplayType& display, byte menuIndex) {
  display.clearDisplay();
  drawMainMenuDots(display);
  drawMainMenuPage(display, menuIndex);
  drawMainMenuSelectedDot(display, getMainMenuDotY(menuIndex));
  display.display();
}

void displayPagesMenuAnimated(DisplayType& display, byte menuIndex, int previousIndex) {
  if (previousIndex < 0 || previousIndex >= MENU_ITEM_COUNT || previousIndex == menuIndex) {
    displayPagesMenu(display, menuIndex);
    return;
  }

  const uint8_t steps = 10;
  const uint8_t frameDelayMs = 2;
  const bool movingDown = menuIndex == ((previousIndex + 1) % MENU_ITEM_COUNT);

  for (uint8_t step = 1; step <= steps; ++step) {
    const int16_t outgoingY = movingDown
      ? interpolateMenuY(0, -SCREEN_HEIGHT, step, steps)
      : interpolateMenuY(0, SCREEN_HEIGHT, step, steps);
    const int16_t incomingY = movingDown
      ? interpolateMenuY(SCREEN_HEIGHT, 0, step, steps)
      : interpolateMenuY(-SCREEN_HEIGHT, 0, step, steps);
    const int16_t selectedDotY = interpolateMenuY(getMainMenuDotY(previousIndex), getMainMenuDotY(menuIndex), step, steps);

    display.clearDisplay();
    drawMainMenuDots(display);
    drawMainMenuPage(display, previousIndex, outgoingY);
    drawMainMenuPage(display, menuIndex, incomingY);
    drawMainMenuSelectedDot(display, selectedDotY);
    display.display();
    delay(frameDelayMs);
  }

  displayPagesMenu(display, menuIndex);
}
