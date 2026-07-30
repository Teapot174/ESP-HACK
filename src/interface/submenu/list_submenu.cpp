#include "list_submenu.h"

void displayListSubmenuImpl(DisplayType& display, const char* const items[], byte itemCount,
                            byte menuIndex, int previousIndex);

void displayListSubmenu(DisplayType& display, const char* const items[], byte itemCount,
                        byte menuIndex, int previousIndex) {
  displayListSubmenuImpl(display, items, itemCount, menuIndex, previousIndex);
}
