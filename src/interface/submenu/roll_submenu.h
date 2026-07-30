#ifndef ROLL_SUBMENU_H
#define ROLL_SUBMENU_H

#include "display.h"

void displayRollSubmenu(DisplayType& display, const char* const items[], byte itemCount,
                        byte menuIndex, int previousIndex = -1);

#endif
