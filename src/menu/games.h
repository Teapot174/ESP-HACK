#ifndef GAMES_MENU_H
#define GAMES_MENU_H

#include "display.h"
#include "interface/interface.h"

#define GAMES_MENU_ITEM_COUNT 5
static const char* gamesMenuItems[] = {"Snake", "Bird", "Bricks", "Pong", "Doom"};

inline void displayGamesMenu(DisplayType &display, byte menuIndex, int previousIndex = -1) {
  displaySubmenu(display, gamesMenuItems, GAMES_MENU_ITEM_COUNT, menuIndex, previousIndex);
}

#endif
