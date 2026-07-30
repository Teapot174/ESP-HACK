#include "roll_submenu.h"

void displayRollSubmenu(DisplayType& display, const char* const items[], byte itemCount,
                        byte menuIndex, int previousIndex) {
  displayAnimatedMenu(display, items, itemCount, menuIndex, previousIndex);
}
