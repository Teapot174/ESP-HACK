#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "CONFIG.h"

#if DISPLAY_TYPE == DISPLAY_SH1106
  #include <Adafruit_SH110X.h>
  typedef Adafruit_SH1106G DisplayType;
#elif DISPLAY_TYPE == DISPLAY_SSD1306
  #include <Adafruit_SSD1306.h>
  typedef Adafruit_SSD1306 DisplayType;
  #ifndef SH110X_WHITE
    #define SH110X_WHITE SSD1306_WHITE
  #endif
  #ifndef SH110X_BLACK
    #define SH110X_BLACK SSD1306_BLACK
  #endif
#else
  #error "ERROR. Use DISPLAY_SH1106 or DISPLAY_SSD1306."
#endif

inline void drawCenteredMenuLabel(DisplayType &display, const char *text, uint8_t textSize, int16_t y) {
  int16_t x1, y1;
  uint16_t w, h;

  display.setTextSize(textSize);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(text);
}

inline void drawRollingMenuLabel(DisplayType &display, const char *text, int16_t y, uint8_t textSize = 1) {
  drawCenteredMenuLabel(display, text, textSize, y);
}

inline int16_t interpolateMenuY(int16_t startY, int16_t endY, uint8_t step, uint8_t steps) {
  if (steps == 0) return endY;

  const int32_t t = (static_cast<int32_t>(step) * 256) / steps;
  const int32_t eased = (t < 128)
    ? (2 * t * t) / 256
    : 256 - (2 * (256 - t) * (256 - t)) / 256;

  return startY + ((endY - startY) * eased) / 256;
}

struct MenuButtonState {
  bool wasPressed = false;
  unsigned long nextRepeatAt = 0;
};

void returnToMainMenu();

inline bool isMenuButtonPress(uint8_t pin, MenuButtonState &state) {
  const bool pressed = digitalRead(pin) == LOW;
  const unsigned long now = millis();
  const unsigned long initialDelayMs = 250;
  const unsigned long repeatDelayMs = 90;

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

inline void drawMenuArrows(DisplayType &display) {
  display.setTextSize(1);
  display.setCursor(2, 30);
  display.print(">");
  display.setCursor(120, 30);
  display.print("<");
}

inline void renderThreeItemMenu(DisplayType &display, const char *prevText, const char *currentText, const char *nextText) {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);

  const int16_t topY = 7;
  const int16_t centerY = 25;
  const int16_t bottomY = 50;

  drawCenteredMenuLabel(display, currentText, 2, centerY);
  drawCenteredMenuLabel(display, prevText, 1, topY);
  drawCenteredMenuLabel(display, nextText, 1, bottomY);
  drawMenuArrows(display);

  display.display();
}

inline void animateThreeItemMenuTransition(DisplayType &display, const char *fromPrev, const char *fromCurrent,
                                           const char *fromNext, const char *toPrev, const char *toCurrent,
                                           const char *toNext, bool movingDown) {
  (void)toCurrent;

  const uint8_t steps = 8;
  const uint8_t frameDelayMs = 6;

  const int16_t topY = 7;
  const int16_t centerY = 25;
  const int16_t bottomY = 50;
  const int16_t offscreenTopY = -11;
  const int16_t offscreenBottomY = 68;
  const uint8_t shrinkStep = (steps * 2) / 5;
  const uint8_t growStep = (steps * 3) / 5;

  for (uint8_t step = 1; step <= steps; ++step) {
    const int16_t fromPrevY = movingDown
      ? interpolateMenuY(topY, offscreenTopY, step, steps)
      : interpolateMenuY(topY, centerY, step, steps);
    const int16_t fromCurrentY = movingDown
      ? interpolateMenuY(centerY, topY, step, steps)
      : interpolateMenuY(centerY, bottomY, step, steps);
    const int16_t fromNextY = movingDown
      ? interpolateMenuY(bottomY, centerY, step, steps)
      : interpolateMenuY(bottomY, offscreenBottomY, step, steps);
    const int16_t incomingY = movingDown
      ? interpolateMenuY(offscreenBottomY, bottomY, step, steps)
      : interpolateMenuY(offscreenTopY, topY, step, steps);

    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);

    const uint8_t outgoingSize = step <= shrinkStep ? 2 : 1;
    const uint8_t incomingCenterSize = step >= growStep ? 2 : 1;

    if (movingDown) {
      drawRollingMenuLabel(display, fromPrev, fromPrevY, 1);
      drawRollingMenuLabel(display, fromCurrent, fromCurrentY, outgoingSize);
      drawRollingMenuLabel(display, fromNext, fromNextY, incomingCenterSize);
      drawRollingMenuLabel(display, toNext, incomingY, 1);
    } else {
      drawRollingMenuLabel(display, toPrev, incomingY, 1);
      drawRollingMenuLabel(display, fromPrev, fromPrevY, incomingCenterSize);
      drawRollingMenuLabel(display, fromCurrent, fromCurrentY, outgoingSize);
      drawRollingMenuLabel(display, fromNext, fromNextY, 1);
    }
    drawMenuArrows(display);
    display.display();
    delay(frameDelayMs);
  }

  renderThreeItemMenu(display, toPrev, toCurrent, toNext);
}

inline void displayAnimatedMenu(DisplayType &display, const char *const items[], byte itemCount, byte menuIndex,
                                int previousIndex = -1) {
  const byte prev = (menuIndex + itemCount - 1) % itemCount;
  const byte next = (menuIndex + 1) % itemCount;

  if (previousIndex < 0 || previousIndex >= itemCount || previousIndex == menuIndex) {
    renderThreeItemMenu(display, items[prev], items[menuIndex], items[next]);
    return;
  }

  const byte previous = static_cast<byte>(previousIndex);
  const byte previousPrev = (previous + itemCount - 1) % itemCount;
  const byte previousNext = (previous + 1) % itemCount;

  if (menuIndex == previousNext) {
    animateThreeItemMenuTransition(display, items[previousPrev], items[previous], items[previousNext],
                                   items[prev], items[menuIndex], items[next], true);
    return;
  }

  if (menuIndex == previousPrev) {
    animateThreeItemMenuTransition(display, items[previousPrev], items[previous], items[previousNext],
                                   items[prev], items[menuIndex], items[next], false);
    return;
  }

  renderThreeItemMenu(display, items[prev], items[menuIndex], items[next]);
}

#endif
