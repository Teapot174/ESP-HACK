#ifndef BLUETOOTH_MENU_H
#define BLUETOOTH_MENU_H

#include <Adafruit_SH110X.h>
#include "CONFIG.h"

#define BLUETOOTH_MENU_ITEM_COUNT 4
static const char* bluetoothMenuItems[] = {"IOS-SPM", "ANDR-SPM", "WIN-SPM", "BadBLE"};

void displayBluetoothMenu(Adafruit_SH1106G &display, byte menuIndex) {
  display.clearDisplay();
  
  auto centerText = [](const char* text, int textSize) {
    return (128 - strlen(text) * (textSize == 2 ? 12 : 6)) / 2;
  };

  byte next = (menuIndex + 1) % BLUETOOTH_MENU_ITEM_COUNT;
  byte prev = (menuIndex + BLUETOOTH_MENU_ITEM_COUNT - 1) % BLUETOOTH_MENU_ITEM_COUNT;

  display.setTextSize(2);
  display.setCursor(centerText(bluetoothMenuItems[menuIndex], 2), 25);
  display.print(bluetoothMenuItems[menuIndex]);
  
  display.setTextSize(1);
  display.setCursor(centerText(bluetoothMenuItems[next], 1), 50);
  display.print(bluetoothMenuItems[next]);
  display.setCursor(centerText(bluetoothMenuItems[prev], 1), 7);
  display.print(bluetoothMenuItems[prev]);
  
  display.setCursor(2, 30);
  display.print(">");
  display.setCursor(120, 30);
  display.print("<");
  
  display.display();
}

#endif
