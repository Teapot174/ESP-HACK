#ifndef WIFI_MENU_H
#define WIFI_MENU_H

#include "display.h"
#include "CONFIG.h"

#define WIFI_MENU_ITEM_COUNT 5
static const char* wifiMenuItems[] = {"Deauther", "Beacon", "Portal", "Wardrvng", "Packets"};

inline void displayWiFiMenu(DisplayType &display, byte menuIndex, int previousIndex = -1) {
  displayAnimatedMenu(display, wifiMenuItems, WIFI_MENU_ITEM_COUNT, menuIndex, previousIndex);
}

#endif
