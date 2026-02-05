#ifndef DISPLAY_H
#define DISPLAY_H

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

#endif
