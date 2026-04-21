#ifndef GAMES_MENU_H
#define GAMES_MENU_H

#include "display.h"

#define GAMES_MENU_ITEM_COUNT 4
static const char* gamesMenuItems[] = {"Snake", "Bird", "Tetris", "Pong"};

inline void displayGamesMenu(DisplayType &display, byte menuIndex, int previousIndex = -1) {
  displayAnimatedMenu(display, gamesMenuItems, GAMES_MENU_ITEM_COUNT, menuIndex, previousIndex);
}

#endif
