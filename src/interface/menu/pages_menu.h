#ifndef PAGES_MENU_H
#define PAGES_MENU_H

#include "display.h"

void displayPagesMenu(DisplayType& display, byte menuIndex);
void displayPagesMenuAnimated(DisplayType& display, byte menuIndex, int previousIndex = -1);

#endif
