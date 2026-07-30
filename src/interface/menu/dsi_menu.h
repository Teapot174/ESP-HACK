#ifndef DSI_MENU_H
#define DSI_MENU_H

#include "display.h"

void displayDsiMenu(DisplayType& display, byte menuIndex, int previousIndex = -1);
void resetDsiMenuAnimation(byte menuIndex);

#endif
