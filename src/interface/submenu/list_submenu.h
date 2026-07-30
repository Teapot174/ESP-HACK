#ifndef LIST_SUBMENU_H
#define LIST_SUBMENU_H

#include "display.h"

void displayListSubmenu(DisplayType& display, const char* const items[], byte itemCount,
                        byte menuIndex, int previousIndex = -1);

#endif
