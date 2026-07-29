#include "original_submenu.h"

void displayOriginalSubmenu(DisplayType& display, const char* const items[], byte itemCount,
                            byte menuIndex, int previousIndex) {
  displayAnimatedMenu(display, items, itemCount, menuIndex, previousIndex);
}
