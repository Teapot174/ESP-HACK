#ifndef SUBMENU_H
#define SUBMENU_H

#include "display.h"
#include "original_submenu.h"
#include "list_submenu.h"

struct SubmenuItems {
  const char* const* original;
  const char* const* list;
  byte count;
};

inline const char* const* getSubmenuItems(const SubmenuItems& items) {
  extern byte submenu;
  return submenu == 1 ? items.list : items.original;
}

inline void displaySubmenu(DisplayType& display, const SubmenuItems& items,
                           byte menuIndex, int previousIndex = -1) {
  extern byte submenu;
  const char* const* selectedItems = getSubmenuItems(items);
  if (submenu == 1) {
    displayListSubmenu(display, selectedItems, items.count, menuIndex, previousIndex);
  } else {
    displayOriginalSubmenu(display, selectedItems, items.count, menuIndex, previousIndex);
  }
}

inline void displaySubmenu(DisplayType& display, const char* const items[], byte itemCount,
                           byte menuIndex, int previousIndex = -1) {
  const SubmenuItems sameItems = {items, items, itemCount};
  displaySubmenu(display, sameItems, menuIndex, previousIndex);
}

#endif
