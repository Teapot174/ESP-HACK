#ifndef SUBGHZ_H
#define SUBGHZ_H

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <GyverButton.h>
#include <EEPROM.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include "CONFIG.h"

extern Adafruit_SH1106G display;
extern GButton buttonUp;
extern GButton buttonDown;
extern GButton buttonOK;
extern GButton buttonBack;

enum emKeys {
  kUnknown,
  kP12bt,
  k12bt,
  k24bt,
  k64bt,
  kKeeLoq,
  kANmotors64,
  kPrinceton,
  kRcSwitch,
  kStarLine,
  kCAME,
  kNICE,
  kHOLTEK,
  kANSONIC,
  kCHAMBERLAIN,
  kLINEAR
};
enum emMenuState { menuMain, menuReceive, menuTransmit, menuAnalyzer, menuJammer, menuBruteforce, menuBruteConfig, menuBruteRun };

struct tpKeyData {
  byte keyID[9];
  int zero[2];
  int one[2];
  int prePulse[2];
  int startPause[2];
  int midlePause[2];
  byte prePulseLenth;
  byte codeLenth;
  byte firstDataIdx;
  emKeys type;
  float frequency;
  int te;
  char rawData[96];
  int bitLength;
  char preset[8];
};

// Bruteforce protocol
struct BruteProtocol {
  const int* zero;
  size_t zeroLen;
  const int* one;
  size_t oneLen;
  const int* pilot;
  size_t pilotLen;
  const int* stop;
  size_t stopLen;
};

void runSubGHz();

#endif
