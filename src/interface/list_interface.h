#ifndef LIST_INTERFACE_H
#define LIST_INTERFACE_H

#include "display.h"

unsigned long getListInterfaceMenuInitialRepeatDelay();
unsigned long getListInterfaceMenuRepeatDelay();
unsigned long getInterfaceSubmenuRepeatDelay(bool isListRootSubmenu);

void displayListInterfaceMenu(DisplayType& display, byte menuIndex, int previousIndex = -1);
void resetListInterfaceAnimation(byte menuIndex);
void displayListInterfaceSubmenu(DisplayType& display, const char* const items[], byte itemCount,
                                 byte menuIndex, int previousIndex = -1);

#endif
