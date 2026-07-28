#ifndef WIFI_MENU_H
#define WIFI_MENU_H

#include "display.h"
#include "CONFIG.h"
#include "interface.h"

#define WIFI_MENU_ITEM_COUNT 5
static const char* wifiMenuItemsOriginal[] = {"Deauth", "Beacon", "Portal", "Wardrvng", "Packets"};
static const char* wifiMenuItemsList[] = {"Deauth", "Beacon", "Evil Portal", "Wardriving", "Packets"};

inline void displayWiFiMenu(DisplayType &display, byte menuIndex, int previousIndex = -1) {
  const char* const* items = submenu == 1 ? wifiMenuItemsList : wifiMenuItemsOriginal;
  displayInterfaceSubmenu(display, items, WIFI_MENU_ITEM_COUNT, menuIndex, previousIndex);
}

#endif
