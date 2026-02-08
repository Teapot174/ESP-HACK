#ifndef ESP_HACK_EXPLORER_H
#define ESP_HACK_EXPLORER_H

#include "display.h"
#include <SD.h>

struct ExplorerEntry {
  String name;
  bool isDir;
};

struct ExplorerConfig {
  const char* rootDir;
  const char* const* extensions;
  uint8_t extCount;
  bool includeDirs;
  bool createRoot;
  bool skipHidden;
  bool allowDelete;
};

struct ExplorerState {
  ExplorerEntry* list = nullptr;
  int maxFiles = 0;
  int count = 0;
  int index = 0;
  String currentDir = "";
  bool inExplorer = false;
  bool inDeleteConfirm = false;
  String selectedFile = "";
};

enum ExplorerAction {
  EXPLORER_NONE = 0,
  EXPLORER_ENTER_DIR,
  EXPLORER_SELECT_FILE,
  EXPLORER_EXIT,
  EXPLORER_DELETE_PROMPT,
  EXPLORER_DELETED,
  EXPLORER_DELETE_FAILED,
  EXPLORER_DELETE_CANCEL
};

void ExplorerInit(ExplorerState& state, ExplorerEntry* buffer, int bufferSize, const ExplorerConfig& cfg);
void ExplorerLoad(ExplorerState& state, const ExplorerConfig& cfg);
void ExplorerDraw(const ExplorerState& state, DisplayType& display);
void ExplorerDrawDeleteConfirm(const ExplorerState& state, DisplayType& display);
void ExplorerDrawSaveResult(DisplayType& display);
ExplorerAction ExplorerHandle(ExplorerState& state, const ExplorerConfig& cfg, DisplayType& display,
                              bool upClick, bool downClick, bool okClick, bool backClick, bool backHold);

#endif
