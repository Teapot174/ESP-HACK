#ifndef WII_MENU_H
#define WII_MENU_H

#include "display.h"

void displayWiiMenu(DisplayType& display, byte menuIndex, int previousIndex = -1);
void resetWiiMenuAnimation(byte menuIndex);

#endif
