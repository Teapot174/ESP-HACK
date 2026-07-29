#ifndef WIFI_MENU_H
#define WIFI_MENU_H

#include "display.h"
#include "CONFIG.h"
#include "interface/interface.h"

#define WIFI_MENU_ITEM_COUNT 5
static const char* wifiMenuItemsOriginal[] = {"Deauth", "Beacon", "Portal", "Wardrvng", "Packets"};
static const char* wifiMenuItemsList[] = {"Deauth", "Beacon", "Evil Portal", "Wardriving", "Packets"};
static const SubmenuItems wifiSubmenuItems = {
  wifiMenuItemsOriginal, wifiMenuItemsList, WIFI_MENU_ITEM_COUNT
};

inline void displayWiFiMenu(DisplayType &display, byte menuIndex, int previousIndex = -1) {
  displaySubmenu(display, wifiSubmenuItems, menuIndex, previousIndex);
}

#endif
