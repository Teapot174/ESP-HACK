#ifndef BLUETOOTH_MENU_H
#define BLUETOOTH_MENU_H

#include "display.h"
#include "CONFIG.h"

#define BLUETOOTH_MENU_ITEM_COUNT 5
static const char* bluetoothMenuItems[] = {"IOS-SPM", "ANDR-SPM", "WIN-SPM", "BadBLE", "Mouse"};

inline void displayBluetoothMenu(DisplayType &display, byte menuIndex, int previousIndex = -1) {
  displayAnimatedMenu(display, bluetoothMenuItems, BLUETOOTH_MENU_ITEM_COUNT, menuIndex, previousIndex);
}

#endif
