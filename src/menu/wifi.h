#ifndef WIFI_MENU_H
#define WIFI_MENU_H

#include <Adafruit_SH110X.h>
#include "CONFIG.h"

#define WIFI_MENU_ITEM_COUNT 4
static const char* wifiMenuItems[] = {"Deauther", "Beacon", "Portal", "Wardrvng"};

void displayWiFiMenu(Adafruit_SH1106G &display, byte menuIndex) {
  display.clearDisplay();
  
  auto centerText = [](const char* text, int textSize) {
    return (128 - strlen(text) * (textSize == 2 ? 12 : 6)) / 2;
  };

  byte next = (menuIndex + 1) % WIFI_MENU_ITEM_COUNT;
  byte prev = (menuIndex + WIFI_MENU_ITEM_COUNT - 1) % WIFI_MENU_ITEM_COUNT;

  display.setTextSize(2);
  display.setCursor(centerText(wifiMenuItems[menuIndex], 2), 25);
  display.print(wifiMenuItems[menuIndex]);
  
  display.setTextSize(1);
  display.setCursor(centerText(wifiMenuItems[next], 1), 50);
  display.print(wifiMenuItems[next]);
  display.setCursor(centerText(wifiMenuItems[prev], 1), 7);
  display.print(wifiMenuItems[prev]);
  
  display.setCursor(2, 30);
  display.print(">");
  display.setCursor(120, 30);
  display.print("<");
  
  display.display();
}

#endif