#include "display.h"
#include <GyverButton.h>
#include <IRremoteESP8266.h>
#include <SD.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "ir_remote.h"
#include "CONFIG.h"
#include "interface/interface.h"
#include "menu/infrared.h"
#include "menu/subghz.h"
#include "Explorer.h"

struct IrCode {
  uint8_t timer_val;
  uint8_t numpairs;
  uint8_t bitcompression;
  const uint16_t* times;
  const uint8_t* codes;
};

template <typename T, size_t N>
constexpr size_t NUM_ELEM(const T (&)[N]) { return N; }

static inline constexpr uint8_t freq_to_timerval(uint32_t hz) {
  return hz / 1000;
}

enum TvbgRegion : uint8_t { TVBG_REGION_EU = 0, TVBG_REGION_NA = 1 };

#include "tvbgcodes.h"

extern DisplayType display;
extern GButton buttonUp;
extern GButton buttonDown;
extern GButton buttonOK;
extern GButton buttonBack;
extern byte currentMenu;
extern bool inMenu;
extern byte irMenuIndex;
extern void OLED_printMenu(DisplayType &display, byte menuIndex);

bool isAbortSendPressed();
void drawSendingScreen(int progress);

bool inIRMenu = false;

enum AppState { MENU, IR_SELECTION, SENDING_IR, IR_FILE_EXPLORER, IR_READING, IR_DELETE_CONFIRM, IR_SIGNAL_SUBMENU, IR_UNIVERSAL_MENU };
constexpr byte IR_MENU_SEND = 0;
constexpr byte IR_MENU_READ = 1;
constexpr byte IR_MENU_TV_OFF = 2;
constexpr byte IR_MENU_REMOTE = 3;
AppState state = MENU;

volatile bool irAbortRequested = false;
volatile bool irOkReleasePending = false;
volatile bool irBackReleasePending = false;
volatile uint32_t irOkPressedAtUs = 0;
volatile uint32_t irBackPressedAtUs = 0;
void IRAM_ATTR onIrAbort() {
  irAbortRequested = true;
}
void IRAM_ATTR onIrOkEdge() {
  const uint32_t now = micros();
  if (digitalRead(BUTTON_OK) == LOW) {
    irOkPressedAtUs = now;
  } else if (irOkPressedAtUs != 0) {
    if (now - irOkPressedAtUs <= 300000UL) irOkReleasePending = true;
    irOkPressedAtUs = 0;
    irAbortRequested = true;
  }
}
void IRAM_ATTR onIrBackEdge() {
  const uint32_t now = micros();
  if (digitalRead(BUTTON_BACK) == LOW) {
    irBackPressedAtUs = now;
  } else if (irBackPressedAtUs != 0) {
    if (now - irBackPressedAtUs <= 300000UL) irBackReleasePending = true;
    irBackPressedAtUs = 0;
    irAbortRequested = true;
  }
}
bool irSuppressInput = false;
unsigned long irSuppressStart = 0;

void startInputSuppress() {
  irSuppressInput = true;
  irSuppressStart = millis();
}

bool handleInputSuppress() {
  if (!irSuppressInput) return false;
  if (digitalRead(BUTTON_OK) == LOW || digitalRead(BUTTON_BACK) == LOW) {
    return true;
  }
  if (millis() - irSuppressStart < 60) {
    return true;
  }
  irSuppressInput = false;
  (void)buttonOK.isClick();
  (void)buttonBack.isClick();
  return true;
}

extern const IrCode* const NApowerCodes[];
extern const IrCode* const EUpowerCodes[];
extern uint8_t num_NAcodes, num_EUcodes;
const unsigned long TVBG_CODE_DELAY_MS = 50;

struct TvbgState {
  TvbgRegion region = TVBG_REGION_EU;
  uint16_t regionIndex = 0;
  uint16_t totalSent = 0;
  uint16_t totalCount = 0;
  unsigned long lastSendTime = 0;
  uint8_t bitsLeft = 0;
  uint8_t bits = 0;
  uint8_t codePtr = 0;
  int lastProgress = -1;
  const IrCode* powerCode = nullptr;
};

TvbgState tvbg;
uint16_t tvbgRawData[300];

#define MAX_FILES 50
static const char* irExts[] = {".ir"};
ExplorerEntry irFileList[MAX_FILES];
ExplorerState irExplorer;
ExplorerConfig irExplorerCfg = {"/infrared", irExts, 1, true, false, true, true};

static const char* universalRemoteItems[] = {
  "TVs", "Audio", "Projector", "LEDs", "Fans", "ACs"
};
static const char* universalRemoteFiles[] = {
  "tv.ir", "audio.ir", "projectors.ir", "leds.ir", "fans.ir", "ac.ir"
};
static const char* universalActions[][8] = {
  {"Power", "Mute", "Vol_up", "Ch_next", "Vol_dn", "Ch_prev", nullptr, nullptr},
  {"Power", "Mute", "Play", "Pause", "Prev", "Next", "Vol_dn", "Vol_up"},
  {"Power", "Mute", "Vol_up", "Vol_dn", "Play", "Pause", nullptr, nullptr},
  {"Power_on", "Power_off", "Brightness_up", "Brightness_dn", "Red", "Green", "Blue", "White"},
  {"Power", "Mode", "Speed_up", "Speed_dn", "Rotate", "Timer", nullptr, nullptr},
  {"Off", "Dh", "Cool_hi", "Heat_hi", "Cool_lo", "Heat_lo", nullptr, nullptr}
};
static const byte universalActionCounts[] = {6, 8, 6, 8, 6, 6};
struct UniversalButtonLayout {
  int16_t x;
  int16_t y;
};
static const UniversalButtonLayout universalButtonLayouts[][8] = {
  {{6, 18}, {40, 18}, {38, 51}, {3, 51}, {38, 91}, {3, 91}},
  {{6, 13}, {39, 13}, {6, 42}, {6, 71}, {6, 101}, {39, 101}, {37, 77}, {37, 43}},
  {{6, 24}, {39, 24}, {37, 55}, {37, 89}, {6, 58}, {6, 87}},
  {{10, 12}, {35, 12}, {10, 42}, {35, 42}, {10, 74}, {35, 74}, {10, 99}, {35, 99}},
  {{6, 24}, {39, 24}, {37, 55}, {37, 89}, {6, 58}, {6, 87}},
  {{6, 15}, {39, 15}, {3, 49}, {37, 49}, {3, 100}, {37, 100}}
};
static const char* universalPanelTitles[] = {"TV", "Audio player", "Projector", "LEDs", "Fan remote", "AC"};
static const int16_t universalTitleX[] = {25, 1, 10, 20, 5, 24};
static const int16_t universalTitleY[] = {10, 10, 11, 9, 11, 10};
constexpr byte UNIVERSAL_REMOTE_ITEM_COUNT = 6;

#define MAX_SIGNALS 20
String irSignalList[MAX_SIGNALS];
int irSignalCount = 0;
int irSignalIndex = 0;
String irSelectedSignal = "";

#define MAX_UNIVERSAL_SIGNAL_INDICES 1000
static uint32_t universalSignalOffsets[MAX_UNIVERSAL_SIGNAL_INDICES];
static uint16_t universalSignalCount = 0;
static uint16_t universalSignalPosition = 0;
static bool universalActive = false;
static byte universalCategory = 0;
static bool universalPaused = false;
static bool irRemoteMode = false;
static byte universalSelectedAction = 0;
static bool universalLoadError = false;
static bool universalLoadCanceled = false;
static unsigned long universalBackIgnoreUntil = 0;

IRsend irsend(IR_TRANSMITTER);
IRrecv irrecv(IR_RECIVER, 1024, 100);
decode_results results;

static U8G2_FOR_ADAFRUIT_GFX universalText;
static bool universalPortrait = false;
static CompressIcon* universalIconDecoder = nullptr;

static void setUniversalPortrait(bool enabled) {
  if (universalPortrait == enabled) return;
  universalPortrait = enabled;
  // The Flipper vertical view is physically opposite to Adafruit rotation 1.
  display.setRotation(enabled ? 3 : 0);
  if (enabled) {
    if (!universalIconDecoder) universalIconDecoder = compress_icon_alloc(4096);
    universalText.begin(display);
    universalText.setFont(u8g2_font_haxrcorp4089_tr);
    universalText.setFontMode(1);
    universalText.setForegroundColor(SH110X_WHITE);
    universalText.setBackgroundColor(SH110X_BLACK);
  }
}

// Flipper icon assets are decoded as XBM (LSB-first), while Adafruit
// drawBitmap() consumes MSB-first data. Draw the decoded rows as XBM.
static void drawUniversalXbm(
    int16_t x,
    int16_t y,
    const uint8_t* bitmap,
    uint16_t width,
    uint16_t height,
    uint16_t color = SH110X_WHITE) {
  if (!bitmap) return;
  const uint16_t rowBytes = (width + 7) / 8;
  for (uint16_t row = 0; row < height; ++row) {
    const uint8_t* rowData = bitmap + row * rowBytes;
    for (uint16_t column = 0; column < width; ++column) {
      if (rowData[column >> 3] & (1U << (column & 7))) {
        display.drawPixel(x + column, y + row, color);
      }
    }
  }
}

static void drawUniversalDecodedIcon(
    int16_t x, int16_t y, const Icon& icon, uint16_t color = SH110X_WHITE) {
  if (!universalIconDecoder) return;
  uint8_t* bitmap = nullptr;
  compress_icon_decode(universalIconDecoder, icon.frames[0], &bitmap);
  drawUniversalXbm(x, y, bitmap, icon.width, icon.height, color);
}

static void drawUniversalGlyph(const String& action, int16_t x, int16_t y, bool selected) {
  const Icon* icon = nullptr;
  if (action == "Power" || action == "Power_on") icon = selected ? &I_power_hover_19x20 : &I_power_19x20;
  else if (action == "Power_off" || action == "Off") icon = selected ? &I_off_hover_19x20 : &I_off_19x20;
  else if (action == "Mute") icon = selected ? &I_mute_hover_19x20 : &I_mute_19x20;
  else if (action == "Vol_up" || action == "Speed_up") icon = selected ? &I_volup_hover_24x21 : &I_volup_24x21;
  else if (action == "Vol_dn" || action == "Speed_dn") icon = selected ? &I_voldown_hover_24x21 : &I_voldown_24x21;
  else if (action == "Ch_next") icon = selected ? &I_ch_up_hover_24x21 : &I_ch_up_24x21;
  else if (action == "Ch_prev") icon = selected ? &I_ch_down_hover_24x21 : &I_ch_down_24x21;
  else if (action == "Play") icon = selected ? &I_play_hover_19x20 : &I_play_19x20;
  else if (action == "Pause") icon = selected ? &I_pause_hover_19x20 : &I_pause_19x20;
  else if (action == "Prev") icon = selected ? &I_prev_hover_19x20 : &I_prev_19x20;
  else if (action == "Next") icon = selected ? &I_next_hover_19x20 : &I_next_19x20;
  else if (action == "Brightness_up") icon = selected ? &I_plus_hover_19x20 : &I_plus_19x20;
  else if (action == "Brightness_dn") icon = selected ? &I_minus_hover_19x20 : &I_minus_19x20;
  else if (action == "Red") icon = selected ? &I_red_hover_19x20 : &I_red_19x20;
  else if (action == "Green") icon = selected ? &I_green_hover_19x20 : &I_green_19x20;
  else if (action == "Blue") icon = selected ? &I_blue_hover_19x20 : &I_blue_19x20;
  else if (action == "White") icon = selected ? &I_white_hover_19x20 : &I_white_19x20;
  else if (action == "Mode") icon = selected ? &I_mode_hover_19x20 : &I_mode_19x20;
  else if (action == "Rotate") icon = selected ? &I_rotate_hover_19x20 : &I_rotate_19x20;
  else if (action == "Timer") icon = selected ? &I_timer_hover_19x20 : &I_timer_19x20;
  else if (action == "Dh") icon = selected ? &I_dry_hover_19x20 : &I_dry_19x20;
  else if (action == "Cool_hi" || action == "Heat_hi") icon = selected ? &I_max_hover_24x23 : &I_max_24x23;
  else if (action == "Cool_lo" || action == "Heat_lo") icon = selected ? &I_celsius_hover_24x23 : &I_celsius_24x23;
  if (icon && universalIconDecoder) {
    drawUniversalDecodedIcon(x, y, *icon);
    return;
  }
  const uint16_t color = selected ? SH110X_BLACK : SH110X_WHITE;
  display.setTextColor(color);
  if (action == "Power" || action == "Power_on" || action == "Power_off" || action == "Off") {
    display.drawCircle(x + 9, y + 10, 7, color);
    display.drawLine(x + 9, y + 1, x + 9, y + 10, color);
    return;
  }
  if (action == "Mute") {
    display.fillRect(x + 1, y + 7, 5, 7, color);
    display.fillTriangle(x + 6, y + 7, x + 11, y + 3, x + 11, y + 17, color);
    display.drawLine(x + 14, y + 5, x + 18, y + 15, color);
    display.drawLine(x + 18, y + 5, x + 14, y + 15, color);
    return;
  }
  if (action == "Vol_up" || action == "Speed_up" || action == "Brightness_up" || action == "Cool_hi" || action == "Heat_hi") {
    display.drawLine(x + 9, y + 17, x + 9, y + 3, color);
    display.drawLine(x + 9, y + 3, x + 3, y + 9, color);
    display.drawLine(x + 9, y + 3, x + 15, y + 9, color);
    return;
  }
  if (action == "Vol_dn" || action == "Speed_dn" || action == "Brightness_dn" || action == "Cool_lo" || action == "Heat_lo") {
    display.drawLine(x + 9, y + 3, x + 9, y + 17, color);
    display.drawLine(x + 9, y + 17, x + 3, y + 11, color);
    display.drawLine(x + 9, y + 17, x + 15, y + 11, color);
    return;
  }
  if (action == "Ch_next" || action == "Next" || action == "Play") {
    display.fillTriangle(x + 4, y + 3, x + 16, y + 10, x + 4, y + 17, color);
    return;
  }
  if (action == "Ch_prev" || action == "Prev") {
    display.fillTriangle(x + 16, y + 3, x + 4, y + 10, x + 16, y + 17, color);
    return;
  }
  if (action == "Pause") {
    display.fillRect(x + 4, y + 3, 4, 14, color);
    display.fillRect(x + 12, y + 3, 4, 14, color);
    return;
  }
  if (action == "Plus" || action == "Red" || action == "Green" || action == "Blue" || action == "White") {
    display.drawRect(x + 2, y + 2, 15, 15, color);
    display.drawLine(x + 9, y + 4, x + 9, y + 15, color);
    display.drawLine(x + 4, y + 9, x + 14, y + 9, color);
    return;
  }
  display.drawRect(x + 2, y + 2, 15, 15, color);
}

static void drawUniversalIcon(int16_t x, int16_t y, const Icon& icon) {
  drawUniversalDecodedIcon(x, y, icon);
}

bool readSignal = false;
String strDeviceContent = "";
int signalsRead = 0;
uint16_t* rawcode = nullptr;
uint16_t raw_data_len = 0;
#define IR_FREQUENCY 38000
#define DUTY_CYCLE 0.330000
unsigned long customSendLastTime = 0;
uint8_t customSendRepeatCount = 0;
const unsigned long CUSTOM_SEND_REPEAT_DELAY_MS = 120;
const uint8_t CUSTOM_SEND_REPEAT_LIMIT = 2;

void initIR() {
  irsend.begin();
  pinMode(IR_RECIVER, INPUT);
  static bool abortIsrAttached = false;
  if (!abortIsrAttached) {
    attachInterrupt(digitalPinToInterrupt(BUTTON_OK), onIrOkEdge, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BUTTON_BACK), onIrBackEdge, CHANGE);
    abortIsrAttached = true;
  }
}

String uint32ToString(uint32_t value) {
  char buffer[12] = {0};
  snprintf(
    buffer,
    sizeof(buffer),
    "%02X %02X %02X %02X",
    value & 0xFF,
    (value >> 8) & 0xFF,
    (value >> 16) & 0xFF,
    (value >> 24) & 0xFF
  );
  return String(buffer);
}

String uint64ToByteString(uint64_t value) {
  char buffer[24] = {0};
  snprintf(
    buffer,
    sizeof(buffer),
    "%02X %02X %02X %02X %02X %02X %02X %02X",
    static_cast<unsigned int>(value & 0xFF),
    static_cast<unsigned int>((value >> 8) & 0xFF),
    static_cast<unsigned int>((value >> 16) & 0xFF),
    static_cast<unsigned int>((value >> 24) & 0xFF),
    static_cast<unsigned int>((value >> 32) & 0xFF),
    static_cast<unsigned int>((value >> 40) & 0xFF),
    static_cast<unsigned int>((value >> 48) & 0xFF),
    static_cast<unsigned int>((value >> 56) & 0xFF)
  );
  return String(buffer);
}

uint64_t parseHexBytesToUint64LE(String str) {
  str.trim();
  uint64_t value = 0;
  int byteIndex = 0;

  while (str.length() > 0 && byteIndex < 8) {
    int spaceIndex = str.indexOf(' ');
    String byteStr;
    if (spaceIndex == -1) {
      byteStr = str;
      str = "";
    } else {
      byteStr = str.substring(0, spaceIndex);
      str = str.substring(spaceIndex + 1);
    }
    byteStr.trim();
    if (byteStr.length() > 0) {
      value |= (static_cast<uint64_t>(strtoul(byteStr.c_str(), nullptr, 16)) & 0xFF)
               << (byteIndex * 8);
      byteIndex++;
    }
  }

  return value;
}

String normalizeProtocolName(String protocol) {
  protocol.trim();
  const String repeatSuffix = F(" (Repeat)");
  if (protocol.endsWith(repeatSuffix)) {
    protocol.remove(protocol.length() - repeatSuffix.length());
    protocol.trim();
  }
  return protocol;
}

void resetTvbgState() {
  tvbg = TvbgState{};
  tvbg.totalCount = static_cast<uint16_t>(num_EUcodes) + static_cast<uint16_t>(num_NAcodes);
}

uint8_t tvbgReadBits(uint8_t count) {
  if (tvbg.powerCode == nullptr) {
    return 0;
  }
  uint8_t value = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (tvbg.bitsLeft == 0) {
      tvbg.bits = tvbg.powerCode->codes[tvbg.codePtr++];
      tvbg.bitsLeft = 8;
    }
    tvbg.bitsLeft--;
    value |= (((tvbg.bits >> tvbg.bitsLeft) & 1) << (count - 1 - i));
  }
  return value;
}

bool sendNextTvbgCode() {
  if (isAbortSendPressed()) {
    startInputSuppress();
    return false;
  }
  while (true) {
    const IrCode* const* codes = (tvbg.region == TVBG_REGION_EU) ? EUpowerCodes : NApowerCodes;
    const uint8_t regionCount = (tvbg.region == TVBG_REGION_EU) ? num_EUcodes : num_NAcodes;

    if (tvbg.regionIndex >= regionCount) {
      if (tvbg.region == TVBG_REGION_EU) {
        tvbg.region = TVBG_REGION_NA;
        tvbg.regionIndex = 0;
        continue;
      }
      return false;
    }

    tvbg.powerCode = codes[tvbg.regionIndex++];
    const uint8_t freq = tvbg.powerCode->timer_val;
    const uint8_t numpairs = tvbg.powerCode->numpairs;
    const uint8_t bitcompression = tvbg.powerCode->bitcompression;

    tvbg.bitsLeft = 0;
    tvbg.codePtr = 0;
    for (uint8_t k = 0; k < numpairs; k++) {
      if (isAbortSendPressed()) {
        startInputSuppress();
        return false;
      }
      const uint16_t ti = (tvbgReadBits(bitcompression)) * 2;
      const uint16_t ontime = tvbg.powerCode->times[ti];
      const uint16_t offtime = tvbg.powerCode->times[ti + 1];
      tvbgRawData[k * 2] = ontime * 10;
      tvbgRawData[(k * 2) + 1] = offtime * 10;
      yield();
    }

    if (isAbortSendPressed()) {
      startInputSuppress();
      return false;
    }
    irsend.sendRaw(tvbgRawData, (numpairs * 2), freq);
    tvbg.bitsLeft = 0;
    tvbg.totalSent++;
    return true;
  }
}

bool isAbortSendPressed() {
  if (irAbortRequested) return true;
  if (buttonOK.isClick() || buttonBack.isClick()) {
    irAbortRequested = true;
    return true;
  }
  return false;
}

void displayIRSelection(byte menuIndex, String signalName = "") {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setTextSize(1);
  display.setCursor(62, 43);
  display.print(F("Press OK."));
  
  if (menuIndex == IR_MENU_SEND || menuIndex == IR_MENU_TV_OFF) {
    display.drawBitmap(14, 12, image_Power_bits, 38, 40, SH110X_WHITE);
  }
  if (menuIndex == IR_MENU_SEND && signalName != "") {
    display.setCursor(66, 26);
    display.print(signalName.length() > 10 ? signalName.substring(0, 10) : signalName);
  }
  display.display();
}

void drawSendingScreen(int progress) {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  
  display.setTextSize(2);
  if (irMenuIndex == IR_MENU_TV_OFF) {
    const String progressText = String(progress) + F("%");
    int16_t boundsX;
    int16_t boundsY;
    uint16_t progressWidth;
    uint16_t progressHeight;
    display.getTextBounds(progressText, 0, 0, &boundsX, &boundsY, &progressWidth, &progressHeight);

    display.setTextSize(1);
    uint16_t attackWidth;
    uint16_t attackHeight;
    display.getTextBounds(F("TVBG Attack"), 0, 0, &boundsX, &boundsY, &attackWidth, &attackHeight);
    const int16_t attackCenterX = 58 + static_cast<int16_t>(attackWidth) / 2;
    display.setTextSize(2);
    display.setCursor(attackCenterX - static_cast<int16_t>(progressWidth) / 2, 23);
    display.print(progressText);
  }
  
  if (irMenuIndex == IR_MENU_SEND || irMenuIndex == IR_MENU_TV_OFF || universalActive) {
    display.drawBitmap(14, 12, image_Power_hvr_bits, 38, 40, SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(58, 43);
    display.print(universalActive ? F("Universal...") : (irMenuIndex == IR_MENU_SEND ? F("Sending...") : F("TVBG Attack")));
  }
  
  display.display();
}

static void displayUniversalRemoteMenu(int previousIndex = -1) {
  setUniversalPortrait(false);
  displaySubmenu(display, universalRemoteItems, UNIVERSAL_REMOTE_ITEM_COUNT, irSignalIndex, previousIndex);
}

static void displayOriginalRemoteMenu(int previousIndex = -1) {
  setUniversalPortrait(false);
  // Original submenu uses the stock animated vertical submenu renderer.
  displaySubmenu(display, universalRemoteItems, UNIVERSAL_REMOTE_ITEM_COUNT, irSignalIndex, previousIndex);
}

static void drawCategoryMenu(int previousIndex = -1) {
  if (submenu == 1) displayUniversalRemoteMenu(previousIndex);
  else displayOriginalRemoteMenu(previousIndex);
}

static void drawUniversalRemoteScreen(bool flush = true) {
  setUniversalPortrait(true);
  display.clearDisplay();
  universalText.setForegroundColor(SH110X_WHITE);
  universalText.setFont(u8g2_font_helvB08_tr);
  universalText.setFontMode(1);
  universalText.setCursor(universalTitleX[universalCategory], universalTitleY[universalCategory]);
  universalText.print(universalPanelTitles[universalCategory]);
  universalText.setFont(u8g2_font_haxrcorp4089_tr);
  universalText.setFontMode(1);

  const byte count = universalActionCounts[universalCategory];
  for (byte i = 0; i < count; ++i) {
    const int16_t x = universalButtonLayouts[universalCategory][i].x;
    const int16_t y = universalButtonLayouts[universalCategory][i].y;
    const bool selected = i == irSignalIndex;
    drawUniversalGlyph(universalActions[universalCategory][i], x, y, selected);
  }
  switch (universalCategory) {
    case 0:
      drawUniversalIcon(4, 40, I_power_text_24x5);
      drawUniversalIcon(40, 40, I_mute_text_19x5);
      drawUniversalIcon(0, 64, I_ch_text_31x34);
      drawUniversalIcon(35, 64, I_vol_tv_text_29x34);
      break;
    case 1:
      drawUniversalIcon(4, 35, I_power_text_24x5);
      drawUniversalIcon(39, 35, I_mute_text_19x5);
      drawUniversalIcon(6, 64, I_play_text_19x5);
      drawUniversalIcon(4, 93, I_pause_text_23x5);
      drawUniversalIcon(6, 123, I_prev_text_19x5);
      drawUniversalIcon(39, 123, I_next_text_19x6);
      drawUniversalIcon(34, 56, I_vol_ac_text_30x30);
      break;
    case 2:
      drawUniversalIcon(4, 46, I_power_text_24x5);
      drawUniversalIcon(39, 46, I_mute_text_19x5);
      drawUniversalIcon(6, 80, I_play_text_19x5);
      drawUniversalIcon(4, 109, I_pause_text_23x5);
      drawUniversalIcon(34, 68, I_vol_ac_text_30x30);
      break;
    case 3:
      drawUniversalIcon(15, 34, I_on_text_9x5);
      drawUniversalIcon(38, 34, I_off_text_12x5);
      drawUniversalIcon(12, 64, I_brightness_text_40x5);
      drawUniversalIcon(19, 121, I_color_text_24x5);
      break;
    case 4:
      drawUniversalIcon(4, 46, I_power_text_24x5);
      drawUniversalIcon(39, 46, I_mode_text_20x5);
      drawUniversalIcon(4, 80, I_rotate_text_24x5);
      drawUniversalIcon(4, 109, I_timer_text_23x5);
      drawUniversalIcon(34, 68, I_speed_text_30x30);
      break;
    case 5:
      drawUniversalIcon(10, 37, I_off_text_12x5);
      drawUniversalIcon(41, 37, I_dry_text_15x5);
      drawUniversalIcon(0, 60, I_cool_30x51);
      drawUniversalIcon(34, 60, I_heat_30x51);
      break;
  }
  universalText.setForegroundColor(SH110X_WHITE);
  if (flush) display.display();
}

static void drawUniversalBoldRoundedFrame(int16_t x, int16_t y, int16_t width, int16_t height) {
  display.fillRect(x + 2, y + 2, width - 3, height - 3, SH110X_WHITE);
  display.drawLine(x + 3, y, x + width - 3, y, SH110X_BLACK);
  display.drawLine(x + 2, y + 1, x + width - 2, y + 1, SH110X_BLACK);
  display.drawLine(x, y + 3, x, y + height - 3, SH110X_BLACK);
  display.drawLine(x + 1, y + 2, x + 1, y + height - 2, SH110X_BLACK);
  display.drawLine(x + width, y + 3, x + width, y + height - 3, SH110X_BLACK);
  display.drawLine(x + width - 1, y + 2, x + width - 1, y + height - 2, SH110X_BLACK);
  display.drawLine(x + 3, y + height, x + width - 3, y + height, SH110X_BLACK);
  display.drawLine(x + 2, y + height - 1, x + width - 2, y + height - 1, SH110X_BLACK);
  display.fillRect(x + 2, y + 2, 2, 2, SH110X_BLACK);
  display.fillRect(x + width - 3, y + 2, 2, 2, SH110X_BLACK);
  display.fillRect(x + 2, y + height - 3, 2, 2, SH110X_BLACK);
  display.fillRect(x + width - 3, y + height - 3, 2, 2, SH110X_BLACK);
}

static void drawUniversalCenteredText(const char* text, int16_t centerX, int16_t baselineY) {
  universalText.setCursor(centerX - universalText.getUTF8Width(text) / 2, baselineY);
  universalText.print(text);
}

static void drawUniversalDecodedIconScaled(
    int16_t x, int16_t y, const Icon& icon, uint8_t scale, uint16_t color) {
  if (!universalIconDecoder || scale == 0) return;
  uint8_t* bitmap = nullptr;
  compress_icon_decode(universalIconDecoder, icon.frames[0], &bitmap);
  const uint16_t rowBytes = (icon.width + 7) / 8;
  for (uint16_t row = 0; row < icon.height; ++row) {
    for (uint16_t column = 0; column < icon.width; ++column) {
      if (bitmap[row * rowBytes + (column >> 3)] & (1U << (column & 7))) {
        display.fillRect(x + column * scale, y + row * scale, scale, scale, color);
      }
    }
  }
}

static void drawUniversalSendingScreen(int progress) {
  // The progress popup is drawn over the button panel.
  drawUniversalRemoteScreen(false);
  universalText.setFont(u8g2_font_haxrcorp4089_tr);
  universalText.setFontMode(1);
  universalText.setForegroundColor(SH110X_BLACK);
  universalText.setBackgroundColor(SH110X_WHITE);
  const int16_t x = 0;
  const int16_t y = 25;
  const int16_t width = 63;
  // Keep only a few pixels below the lower send/resume bitmap.
  const int16_t height = 72;
  drawUniversalBoldRoundedFrame(x, y, width, height);

  drawUniversalCenteredText(universalPaused ? "Paused" : "Sending...", x + 32, y + 12);

  // Progress bar geometry: 56x9 at (4,44).
  const int16_t barX = x + 4;
  const int16_t barY = y + 19;
  const int16_t barWidth = width - 7;
  display.fillRect(barX + 1, barY + 1, barWidth - 2, 7, SH110X_WHITE);
  display.drawRoundRect(barX, barY, barWidth, 9, 3, SH110X_BLACK);
  display.fillRect(barX + 1, barY + 1, map(progress, 0, 100, 0, barWidth - 2), 7, SH110X_BLACK);

  char progressText[16];
  if (universalLoadError) {
    snprintf(progressText, sizeof(progressText), "error");
  } else if (universalSignalCount == 0) {
    snprintf(progressText, sizeof(progressText), "loading...");
  } else {
    const uint16_t sentCount = min<uint16_t>(universalSignalPosition, universalSignalCount);
    snprintf(progressText, sizeof(progressText), "%u/%u", sentCount, universalSignalCount);
  }
  // The popup is 64 pixels wide in the rotated 64x128 view. U8g2 measures
  // the actual glyph width, so 32 keeps 1-, 2- and 3-digit counters centered.
  drawUniversalCenteredText(progressText, x + 32, y + 40);

  const int16_t buttonsX = x + (universalPaused ? 10 : 14);
  const int16_t buttonsY = y + 50;
  // Stop and pause are intentionally swapped: OK is the first row, Back the second.
  drawUniversalDecodedIcon(buttonsX + 1, buttonsY, I_Ok_btn_9x9, SH110X_BLACK);
  universalText.setCursor(buttonsX + 14, buttonsY + 8);
  universalText.print(universalPaused ? "send" : "pause");
  drawUniversalDecodedIcon(buttonsX, buttonsY + 10, I_Pin_back_arrow_10x8, SH110X_BLACK);
  universalText.setCursor(buttonsX + 14, buttonsY + 17);
  universalText.print(universalPaused ? "resume" : "stop");
  universalText.setForegroundColor(SH110X_WHITE);
  display.display();
}

static bool loadUniversalSignalNames(const String& fileName) {
  setUniversalPortrait(true);
  irExplorer.currentDir = "/infrared/assets";
  irSignalCount = 0;
  File file = SD.open(irExplorer.currentDir + "/" + fileName, FILE_READ);
  if (!file) return false;

  int signalIndex = -1;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (!line.startsWith("name:")) continue;
    signalIndex++;
    String name = line.substring(5);
    name.trim();
    for (byte i = 0; i < universalActionCounts[universalCategory]; ++i) {
      if (name == universalActions[universalCategory][i]) {
        irSignalList[i] = name;
        break;
      }
    }
  }
  file.close();
  irExplorer.selectedFile = fileName;
  irSignalCount = universalActionCounts[universalCategory];
  return true;
}

static bool collectUniversalSignalIndices(const String& action) {
  universalSignalCount = 0;
  universalSignalPosition = 0;
  universalLoadError = false;
  universalLoadCanceled = false;
  File file = SD.open(irExplorer.currentDir + "/" + universalRemoteFiles[universalCategory], FILE_READ);
  if (!file) {
    universalLoadError = true;
    return false;
  }

  int signalIndex = -1;
  while (file.available()) {
    // SD indexing can take long enough for a BACK press to arrive between
    // normal loop iterations. Abort loading immediately; do not turn that
    // release into Pause or a signal selection event.
    if (irBackReleasePending) {
      irBackReleasePending = false;
      file.close();
      universalSignalCount = 0;
      universalSignalPosition = 0;
      universalLoadCanceled = true;
      return false;
    }
    const uint32_t lineOffset = file.position();
    String line = file.readStringUntil('\n');
    line.trim();
    if (!line.startsWith("name:")) continue;
    signalIndex++;
    String name = line.substring(5);
    name.trim();
    if (name == action && universalSignalCount < MAX_UNIVERSAL_SIGNAL_INDICES) {
      universalSignalOffsets[universalSignalCount++] = lineOffset;
    }
  }
  file.close();
  return universalSignalCount > 0;
}

void drawReadingScreen() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setTextSize(1);
  
  if (!readSignal) {
    display.setCursor(5, 53);
    display.print(F("Waiting signal..."));
    display.drawBitmap(0, 12, image_InfraredLearnShort_bits, 128, 31, SH110X_WHITE);
  } else {
    display.setCursor(3, 3);
    display.println(F("Signal:"));
    uint8_t dataY = display.getCursorY() + 3;

    String code;
    if (results.decode_type == UNKNOWN) {
      rawcode = resultToRawArray(&results);
      raw_data_len = getCorrectedRawLength(&results);
      code = "";
      for (uint16_t i = 0; i < raw_data_len && i < 4; i++) {
        code += String(rawcode[i]);
        if (i < raw_data_len - 1 && i < 3) code += " ";
      }
      if (raw_data_len > 4) code += "...";
      delete[] rawcode;
      rawcode = nullptr;
    } else {
      code = uint32ToString(results.address);
    }
    display.setCursor(3, dataY);
    display.println("Code: " + (code.length() > 16 ? code.substring(0, 16) : code));

    String signalType = (results.decode_type == UNKNOWN ? "RAW" : typeToString(results.decode_type, results.repeat));
    dataY = display.getCursorY() + 2;
    display.setCursor(3, dataY);
    display.println("Type: " + (signalType.length() > 16 ? signalType.substring(0, 16) : signalType));

    dataY = display.getCursorY() + 2;
    display.setCursor(3, dataY);
    display.println("Bits: " + String(results.bits));

    display.setCursor(17, 52);
    display.print(F("Hold OK to save."));
  }
  
  display.display();
}

void drawSaveConfirm() {
  display.clearDisplay();
  display.drawBitmap(16, 6, image_DolphinSaved_bits, 92, 58, SH110X_WHITE);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(6, 16);
  display.print(F("Saved"));
  display.display();
}

static void ensureIrExplorerDir() {
  if (irExplorer.currentDir.length() == 0) {
    irExplorer.currentDir = irExplorerCfg.rootDir;
  }
}

static int infraredNumberFromName(String name) {
  int lastSlash = name.lastIndexOf('/');
  if (lastSlash >= 0) name = name.substring(lastSlash + 1);
  if (!(name.startsWith("Infrared_") || name.startsWith("infrared_")) || !name.endsWith(".ir")) {
    return 0;
  }

  String numStr = name.substring(9, name.length() - 3);
  if (numStr.length() == 0) {
    return 0;
  }
  for (uint16_t i = 0; i < numStr.length(); i++) {
    if (!isDigit(numStr[i])) {
      return 0;
    }
  }

  return numStr.toInt();
}

static int nextInfraredFileIndex() {
  ensureIrExplorerDir();

  int maxInfraredIndex = 0;
  File dir = SD.open(irExplorer.currentDir);
  if (dir) {
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;
      if (!entry.isDirectory() && String(entry.name()).endsWith(".ir")) {
        int num = infraredNumberFromName(entry.name());
        if (num > maxInfraredIndex) {
          maxInfraredIndex = num;
        }
      }
      entry.close();
    }
    dir.close();
  }

  return maxInfraredIndex + 1;
}

static bool irSignalIsNavButtonPress(uint8_t pin, MenuButtonState& state) {
  const bool pressed = digitalRead(pin) == LOW;
  const unsigned long now = millis();
  const unsigned long initialDelayMs = 125;
  const unsigned long repeatDelayMs = 125;

  if (!pressed) {
    state.wasPressed = false;
    state.nextRepeatAt = 0;
    return false;
  }

  if (!state.wasPressed) {
    state.wasPressed = true;
    state.nextRepeatAt = now + initialDelayMs;
    return true;
  }

  if (now >= state.nextRepeatAt) {
    state.nextRepeatAt = now + repeatDelayMs;
    return true;
  }

  return false;
}

static bool irButtonReleased(uint8_t pin, bool& wasDown, unsigned long& downAt) {
  const bool isDown = digitalRead(pin) == LOW;
  if (isDown && !wasDown) downAt = millis();
  const bool released = !isDown && wasDown && (millis() - downAt <= 300);
  wasDown = isDown;
  return released;
}

static bool irSelectedSignalNeedsScroll() {
  return irSignalCount > 0 && irSignalIndex >= 0 && irSignalIndex < irSignalCount &&
         irSignalList[irSignalIndex].length() > 20;
}

static void printIrSignalName(const String& name, int16_t x, int16_t y, bool selected) {
  static String marqueeText = "";
  static unsigned long marqueeStartedAt = 0;

  const int visibleChars = 20;
  if (!selected || name.length() <= visibleChars) {
    if (selected) {
      marqueeText = "";
      marqueeStartedAt = 0;
    }
    display.setCursor(x, y);
    display.print(name.length() > visibleChars ? name.substring(0, visibleChars) : name);
    return;
  }

  if (marqueeText != name) {
    marqueeText = name;
    marqueeStartedAt = millis();
  }

  String marquee = name + F("   ");
  int maxOffset = marquee.length() - visibleChars;
  if (maxOffset < 0) maxOffset = 0;

  int offset = 0;
  const unsigned long initialPauseMs = 400;
  const unsigned long loopPauseMs = 400;
  const unsigned long stepMs = 200;
  unsigned long elapsed = millis() - marqueeStartedAt;
  if (maxOffset > 0 && elapsed >= initialPauseMs) {
    unsigned long scrollDuration = static_cast<unsigned long>(maxOffset) * stepMs;
    unsigned long cycleDuration = scrollDuration + loopPauseMs + scrollDuration + loopPauseMs;
    unsigned long cyclePosition = (elapsed - initialPauseMs) % cycleDuration;
    if (cyclePosition < scrollDuration) {
      offset = cyclePosition / stepMs;
    } else if (cyclePosition < scrollDuration + loopPauseMs) {
      offset = maxOffset;
    } else if (cyclePosition < scrollDuration + loopPauseMs + scrollDuration) {
      offset = maxOffset - ((cyclePosition - scrollDuration - loopPauseMs) / stepMs);
    }
  }

  display.setCursor(x, y);
  display.print(marquee.substring(offset, offset + visibleChars));
}

void drawSignalSubmenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setTextColor(SH110X_WHITE);

  display.setCursor(3, 3);
  String name = irExplorer.selectedFile;
  if (name.length() > 16) name = name.substring(0, 16);
  display.print(name);
  display.setCursor(1, 10);
  display.println(F("---------------------"));

  if (irSignalCount == 0) {
    display.setCursor(1, 24);
    display.println(F("No signals."));
    display.display();
    return;
  }

  const int perPage = 4;
  int maxStart = (irSignalCount > perPage) ? (irSignalCount - perPage) : 0;
  int startIndex = irSignalIndex - 1;
  if (startIndex < 0) startIndex = 0;
  if (startIndex > maxStart) startIndex = maxStart;

  int itemsToShow = irSignalCount - startIndex;
  if (itemsToShow > perPage) itemsToShow = perPage;

  for (int i = 0; i < itemsToShow; i++) {
    int idx = startIndex + i;
    const int signalY = (i + 2) * 11 - 2;
    display.setCursor(3, signalY);
    if (idx == irSignalIndex) {
      display.fillRect(0, signalY - 2, display.width(), 11, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }
    printIrSignalName(irSignalList[idx], 3, signalY, idx == irSignalIndex);
  }
  display.display();
}

// Original Remote is a loaded button list.
// It deliberately uses the normal (horizontal) display and stays on this
// screen after transmitting a button.
static void drawOriginalRemoteScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(3, 8);
  String title = irExplorer.selectedFile;
  if (title.endsWith(".ir")) title.remove(title.length() - 3);
  if (title.length() > 20) title = title.substring(0, 20);
  display.print(title);
  display.drawFastHLine(0, 11, display.width(), SH110X_WHITE);

  if (irSignalCount == 0) {
    display.setCursor(3, 31);
    display.print(F("No buttons"));
    display.display();
    return;
  }

  const byte visible = 4;
  int start = irSignalIndex - 1;
  if (start < 0) start = 0;
  if (start > irSignalCount - visible) start = max(0, irSignalCount - visible);
  for (byte row = 0; row < visible && start + row < irSignalCount; ++row) {
    const int idx = start + row;
    const int y = 21 + row * 11;
    if (idx == irSignalIndex) {
      display.fillRect(0, y - 8, display.width(), 11, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    String label = irSignalList[idx];
    if (label.length() > 20) label = label.substring(0, 20);
    display.setCursor(4, y);
    display.print(label);
    display.setTextColor(SH110X_WHITE);
  }
  display.display();
}


bool loadIRSignals(String fileName) {
  ensureIrExplorerDir();
  irSignalCount = 0;
  File file = SD.open(irExplorer.currentDir + "/" + fileName, FILE_READ);
  if (!file) {
    Serial.print(F("Failed to open file for signals: "));
    Serial.println(fileName);
    return false;
  }

  String signalName = "";
  while (file.available() && irSignalCount < MAX_SIGNALS) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.startsWith("name:")) {
      signalName = line.substring(5);
      signalName.trim();
      irSignalList[irSignalCount] = signalName;
      irSignalCount++;
    }
  }
  file.close();
  Serial.print(F("Found "));
  Serial.print(irSignalCount);
  Serial.print(F(" signals in "));
  Serial.println(fileName);
  return true;
}

bool sendIRSignal(String fileName, int signalIdx, uint32_t signalOffset = UINT32_MAX) {
  ensureIrExplorerDir();
  File file = SD.open(irExplorer.currentDir + "/" + fileName, FILE_READ);
  if (!file) {
    Serial.print(F("Failed to open file: "));
    Serial.println(fileName);
    return false;
  }

  String protocol = "";
  String address = "";
  String command = "";
  String value = "";
  String rawData = "";
  uint16_t frequency = 38000;
  uint16_t bits = 32;
  bool parsedMode = false;
  int currentSignal = -1;

  if (signalOffset != UINT32_MAX) {
    if (!file.seek(signalOffset)) {
      file.close();
      Serial.println(F("Failed to seek to IR signal"));
      return false;
    }
    currentSignal = signalIdx - 1;
  }

  String line;
  while (file.available()) {
    line = file.readStringUntil('\n');
    line.trim();
    if (line.startsWith("name:")) {
      currentSignal++;
      if (currentSignal > signalIdx) break;
    }
    if (currentSignal == signalIdx) {
      if (line.startsWith("type:")) {
        String type = line.substring(5);
        type.trim();
        parsedMode = (type == "parsed");
      } else if (line.startsWith("protocol:")) {
        protocol = line.substring(9);
        protocol.trim();
      } else if (line.startsWith("address:")) {
        address = line.substring(8);
        address.trim();
      } else if (line.startsWith("command:")) {
        command = line.substring(8);
        command.trim();
      } else if (line.startsWith("value:")) {
        value = line.substring(6);
        value.trim();
      } else if (line.startsWith("bits:")) {
        bits = line.substring(5).toInt();
      } else if (line.startsWith("frequency:")) {
        frequency = line.substring(10).toInt();
      } else if (line.startsWith("data:")) {
        rawData = line.substring(5);
        rawData.trim();
      }
    }
  }
  file.close();

  if (currentSignal < signalIdx) {
    Serial.println(F("Signal index not found in file"));
    return false;
  }

  auto parseHexStringToUint32LE = [](String str) -> uint32_t {
    return static_cast<uint32_t>(parseHexBytesToUint64LE(str) & 0xFFFFFFFFULL);
  };

  if (parsedMode && protocol.equalsIgnoreCase("Kaseikyo")) {
    const uint32_t addressValue = parseHexStringToUint32LE(address);
    const uint16_t commandValue = parseHexStringToUint32LE(command) & 0x3FF;
    const uint8_t id = (addressValue >> 24) & 0x03;
    const uint16_t vendor = (addressValue >> 8) & 0xFFFF;
    const uint8_t genre1 = (addressValue >> 4) & 0x0F;
    const uint8_t genre2 = addressValue & 0x0F;
    const uint8_t vendorLow = vendor & 0xFF;
    const uint8_t vendorHigh = vendor >> 8;
    const uint8_t vendorParity = ((vendorLow ^ vendorHigh) & 0x0F) ^ ((vendorLow ^ vendorHigh) >> 4);
    const uint8_t byte2 = (vendorParity & 0x0F) | (genre1 << 4);
    const uint8_t byte3 = (genre2 & 0x0F) | ((commandValue & 0x0F) << 4);
    const uint8_t byte4 = (id << 6) | (commandValue >> 4);
    const uint8_t byte5 = byte2 ^ byte3 ^ byte4;
    auto reverseByte = [](uint8_t value) -> uint8_t {
      value = (value & 0xF0) >> 4 | (value & 0x0F) << 4;
      value = (value & 0xCC) >> 2 | (value & 0x33) << 2;
      return (value & 0xAA) >> 1 | (value & 0x55) << 1;
    };
    // Kaseikyo transmits every byte LSB-first; IRremoteESP8266's
    // Panasonic sender consumes one 48-bit value MSB-first.
    const uint64_t kaseikyo = (static_cast<uint64_t>(reverseByte(vendorLow)) << 40) |
      (static_cast<uint64_t>(reverseByte(vendorHigh)) << 32) |
      (static_cast<uint64_t>(reverseByte(byte2)) << 24) |
      (static_cast<uint64_t>(reverseByte(byte3)) << 16) |
      (static_cast<uint64_t>(reverseByte(byte4)) << 8) |
      static_cast<uint64_t>(reverseByte(byte5));
    irsend.sendPanasonic64(kaseikyo, 48);
    return true;
  }

  if (parsedMode && protocol != "") {
    protocol = normalizeProtocolName(protocol);
    decode_type_t protocolType = strToDecodeType(protocol.c_str());
    uint64_t fullValue = value.length() > 0 ? parseHexBytesToUint64LE(value) : 0;
    uint16_t sendBits = bits > 0 ? bits : 0;

    if (protocol.equalsIgnoreCase("Samsung32")) {
      uint32_t data = 0;
      if (value.length() > 0) {
        data = parseHexStringToUint32LE(value);
      } else {
        const uint8_t addressValue = parseHexStringToUint32LE(address) & 0xFF;
        const uint8_t commandValue = parseHexStringToUint32LE(command) & 0xFF;
        data = irsend.encodeSAMSUNG(addressValue, commandValue);
      }
      Serial.print(F("Sending Samsung: 0x"));
      Serial.println(data, HEX);
      irsend.sendSAMSUNG(data, 32);
      return true;
      
    } else if (protocol.equalsIgnoreCase("SONY") ||
               protocol.equalsIgnoreCase("SIRC") ||
               protocol.equalsIgnoreCase("SIRC15") ||
               protocol.equalsIgnoreCase("SIRC20")) {
      uint32_t sonyCode = 0;
      uint16_t sonyBits = bits > 0 ? bits : 12;
      if (protocol.equalsIgnoreCase("SIRC15")) sonyBits = 15;
      if (protocol.equalsIgnoreCase("SIRC20")) sonyBits = 20;
      if (value.length() > 0) {
        sonyCode = parseHexStringToUint32LE(value);
      } else {
        const uint16_t commandValue = parseHexStringToUint32LE(command);
        const uint16_t addressValue = parseHexStringToUint32LE(address);
        sonyCode = irsend.encodeSony(sonyBits, commandValue, addressValue);
      }
      Serial.print(F("Sending Sony: 0x"));
      Serial.println(sonyCode, HEX);
      irsend.sendSony(sonyCode, sonyBits, 2);
      return true;
      
    } else if (protocol.equalsIgnoreCase("NEC") ||
               protocol.equalsIgnoreCase("NECext") ||
               protocol.equalsIgnoreCase("NEC42")) {
      uint32_t necCode = 0;
      
      if (value.length() > 0) {
        // Используем value если он сохранен
        necCode = parseHexStringToUint32LE(value);
      } else {
        // Формируем из address и command с правильным порядком байт
        uint16_t addressValue = parseHexStringToUint32LE(address);
        uint8_t commandValue = parseHexStringToUint32LE(command) & 0xFF;
        
        // Для NEC правильный порядок: command в старших 16 битах, address в младших
        necCode = irsend.encodeNEC(addressValue, commandValue);
      }
      
      Serial.print(F("Address bytes: ["));
      Serial.print(address);
      Serial.print(F("] -> 0x"));
      Serial.println(parseHexStringToUint32LE(address), HEX);
      
      Serial.print(F("Command bytes: ["));
      Serial.print(command);
      Serial.print(F("] -> 0x"));
      Serial.println(parseHexStringToUint32LE(command), HEX);
      
      Serial.print(F("Sending NEC code: 0x"));
      Serial.println(necCode, HEX);
      
      irsend.sendNEC(necCode, protocol.equalsIgnoreCase("NEC42") ? 42 : 32);
      return true;
      
    } else if (protocol.equalsIgnoreCase("EPSON")) {
      uint32_t epsonCode = 0;

      if (value.length() > 0) {
        epsonCode = parseHexStringToUint32LE(value);
      } else {
        uint32_t addressValue = parseHexStringToUint32LE(address);
        uint32_t commandValue = parseHexStringToUint32LE(command);
        epsonCode = (commandValue << 16) | addressValue;
      }

      if (sendBits == 0) {
        sendBits = 32;
      }

      Serial.print(F("Sending EPSON code: 0x"));
      Serial.print(epsonCode, HEX);
      Serial.print(F(" ("));
      Serial.print(sendBits);
      Serial.println(F(" bits)"));

      irsend.sendEpson(epsonCode, sendBits);
      return true;
      
    } else if (protocol.equalsIgnoreCase("RC6")) {
    uint32_t rc6Code = 0;
    int rc6Bits = bits;
    
    if (value.length() > 0) {
        // Если есть value - используем его, но с осторожностью
        rc6Code = parseHexStringToUint32LE(value);
    } else {
        // RC6 Mode 0: Control (8 бит) в битах 23-16, Command (8 бит) в битах 7-0
        // address и command в файле - 4-байтовые, берем младший байт каждого
        uint32_t addr = parseHexStringToUint32LE(address) & 0xFF;   // младший байт адреса
        uint32_t cmd = parseHexStringToUint32LE(command) & 0xFF;    // младший байт команды
        
        // Формируем RC6 код по стандарту: Control << 16 | Command
        rc6Code = (addr << 16) | cmd;
    }
    
    // RC6 Mode 0 обычно 20 или 24 бита (20 бит данных + заголовок)
    if (rc6Bits == 0) {
        rc6Bits = 20;  // стандартная битность для RC6 Mode 0
    }
    
    Serial.print(F("Address (hex): "));
    Serial.print(parseHexStringToUint32LE(address), HEX);
    Serial.print(F(" -> extracted: 0x"));
    Serial.println(parseHexStringToUint32LE(address) & 0xFF, HEX);
    
    Serial.print(F("Command (hex): "));
    Serial.print(parseHexStringToUint32LE(command), HEX);
    Serial.print(F(" -> extracted: 0x"));
    Serial.println(parseHexStringToUint32LE(command) & 0xFF, HEX);
    
    Serial.print(F("Sending RC6 code: 0x"));
    Serial.print(rc6Code, HEX);
    Serial.print(F(" ("));
    Serial.print(rc6Bits);
    Serial.println(F(" bits)"));
    
    irsend.sendRC6(rc6Code, rc6Bits);
    return true;
	} else if (protocol.equalsIgnoreCase("RC5")) {
     uint32_t rc5Code = 0;
  
    if (value.length() > 0) {
      rc5Code = parseHexStringToUint32LE(value);
    } else {
      rc5Code = parseHexStringToUint32LE(command);
    }
  
    // RC5 обычно 12 или 14 бит
    int rc5Bits = bits;
    if (rc5Bits == 0) {
      rc5Bits = 12; // Стандарт RC5
    }
  
    Serial.print(F("Sending RC5: 0x"));
    Serial.print(rc5Code, HEX);
    Serial.print(F(" ("));
    Serial.print(rc5Bits);
    Serial.println(F(" bits)"));
  
    irsend.sendRC5(rc5Code, rc5Bits);
    return true;
  }

    if (protocolType != decode_type_t::UNKNOWN) {
      if (sendBits == 0) {
        sendBits = IRsend::defaultBits(protocolType);
      }

      if (fullValue != 0 || value.length() > 0) {
        Serial.print(F("Sending generic protocol "));
        Serial.print(protocol);
        Serial.print(F(" value: "));
        Serial.print(uint64ToByteString(fullValue));
        Serial.print(F(" ("));
        Serial.print(sendBits);
        Serial.println(F(" bits)"));
        return irsend.send(protocolType, fullValue, sendBits);
      }
    }
    
    Serial.print(F("Unsupported protocol: "));
    Serial.println(protocol);
    return false;
    
  } else if (!parsedMode && rawData != "") {
    // Отправка raw сигнала
    uint16_t dataBufferSize = 1;
    for (int i = 0; i < rawData.length(); i++) {
      if (rawData[i] == ' ') dataBufferSize++;
    }
    
    uint16_t* dataBuffer = (uint16_t*)malloc(dataBufferSize * sizeof(uint16_t));
    if (!dataBuffer) {
      Serial.println(F("Failed to allocate memory for IR data"));
      return false;
    }
    
    uint16_t count = 0;
    String data = rawData;
    while (data.length() > 0 && count < dataBufferSize) {
      int delimiterIndex = data.indexOf(' ');
      if (delimiterIndex == -1) delimiterIndex = data.length();
      String dataChunk = data.substring(0, delimiterIndex);
      data.remove(0, delimiterIndex + 1);
      dataBuffer[count++] = dataChunk.toInt();
    }
    
    Serial.print(F("Sending raw data: "));
    Serial.print(count);
    Serial.println(F(" pulses"));
    
    irsend.sendRaw(dataBuffer, count, frequency);
    free(dataBuffer);
    return true;
  }
  
  Serial.println(F("Invalid IR signal format"));
  return false;
}

String parseRawSignal() {
  rawcode = resultToRawArray(&results);
  raw_data_len = getCorrectedRawLength(&results);
  String signal_code = "";
  for (uint16_t i = 0; i < raw_data_len; i++) {
    signal_code += String(rawcode[i]) + " ";
  }
  delete[] rawcode;
  rawcode = nullptr;
  signal_code.trim();
  return signal_code;
}

void appendToDeviceContent(String btn_name) {
  if (results.repeat) {
    Serial.println(F("Ignoring repeat IR frame"));
    return;
  }
  if (results.decode_type != decode_type_t::UNKNOWN && results.bits > 64) {
    strDeviceContent += "name: " + btn_name + "\n";
    strDeviceContent += "type: raw\n";
    strDeviceContent += "frequency: " + String(IR_FREQUENCY) + "\n";
    strDeviceContent += "duty_cycle: " + String(DUTY_CYCLE) + "\n";
    strDeviceContent += "data: " + parseRawSignal() + "\n";
    return;
  }

  strDeviceContent += "name: " + btn_name + "\n";
  strDeviceContent += "type: parsed\n";
  bool saveBits = false;
  bool saveValue = false;
  switch (results.decode_type) {
    case decode_type_t::SAMSUNG: {
      strDeviceContent += "protocol: Samsung32\n";
      saveBits = true;
      saveValue = true;
      break;
    }
    case decode_type_t::SONY: {
      strDeviceContent += "protocol: SONY\n";
      saveBits = true;
      saveValue = true;
      break;
    }
    case decode_type_t::NEC: {
      strDeviceContent += "protocol: NEC\n";
      saveBits = true;
      saveValue = true;
      break;
    }
    case decode_type_t::RC6: {
	  strDeviceContent += "protocol: RC6\n";
      saveBits = true;
      saveValue = true;
      break;
	}
	case decode_type_t::RC5: {
      strDeviceContent += "protocol: RC5\n";
      saveBits = true;
      saveValue = true;
	  break;
	}
    case decode_type_t::UNKNOWN: {
      strDeviceContent += "type: raw\n";
      strDeviceContent += "frequency: " + String(IR_FREQUENCY) + "\n";
      strDeviceContent += "duty_cycle: " + String(DUTY_CYCLE) + "\n";
      strDeviceContent += "data: " + parseRawSignal() + "\n";
      return;
    }
    default: {
      strDeviceContent += "protocol: " + normalizeProtocolName(typeToString(results.decode_type, results.repeat)) + "\n";
      saveBits = true;
      saveValue = true;
      break;
    }
  }
  if (saveBits && results.bits > 0) {
    strDeviceContent += "bits: " + String(results.bits) + "\n";
  }
  if (saveValue) {
    strDeviceContent += "value: " + uint64ToByteString(results.value) + "\n";
  }
  strDeviceContent += "address: " + uint32ToString(results.address) + "\n";
  strDeviceContent += "command: " + uint32ToString(results.command) + "\n";
  strDeviceContent += "#\n";
}

bool saveIRSignal() {
  ensureIrExplorerDir();
  String filename = "Infrared";
  File dir = SD.open(irExplorer.currentDir);
  if (!dir) {
    Serial.println(F("Failed to open /infrared directory"));
    return false;
  }
  dir.close();
  SD.mkdir(irExplorer.currentDir);
  int index = nextInfraredFileIndex();
  while (SD.exists(irExplorer.currentDir + "/" + filename + "_" + String(index) + ".ir")) {
    index++;
  }
  filename = irExplorer.currentDir + "/" + filename + "_" + String(index) + ".ir";
  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    Serial.print(F("Failed to create file: "));
    Serial.println(filename);
    return false;
  }
  file.println("Filetype: IR signals file");
  file.println("Version: 1");
  file.println("#");
  file.print(strDeviceContent);
  file.close();
  Serial.print(F("File saved: "));
  Serial.println(filename);
  return true;
}

void handleIRSubmenu() {
  static bool irInitialized = false;
  static unsigned long sendStartTime = 0;
  static bool universalOkWasDown = false;
  static bool universalBackWasDown = false;
  static unsigned long universalOkDownAt = 0;
  static unsigned long universalBackDownAt = 0;
  static MenuButtonState signalUpHeld;
  static MenuButtonState signalDownHeld;

  if (!irInitialized) {
    initIR();
    irInitialized = true;
    buttonUp.setDebounce(50);
    buttonDown.setDebounce(50);
    buttonOK.setDebounce(50);
    buttonBack.setDebounce(50);
    buttonUp.setTimeout(500);
    buttonDown.setTimeout(500);
    buttonOK.setTimeout(300);
    buttonBack.setTimeout(300);
    buttonUp.setClickTimeout(BUTTON_RELEASE_CLICK_MS);
    buttonDown.setClickTimeout(BUTTON_RELEASE_CLICK_MS);
    buttonOK.setClickTimeout(BUTTON_RELEASE_CLICK_MS);
    buttonBack.setClickTimeout(BUTTON_RELEASE_CLICK_MS);
    buttonUp.setStepTimeout(200);
    buttonDown.setStepTimeout(200);
    buttonOK.setStepTimeout(200);
    buttonBack.setStepTimeout(200);
  }

  static bool irReceiverEnabled = false;
  if (state == IR_READING && !irReceiverEnabled) {
    irrecv.enableIRIn();
    irReceiverEnabled = true;
  } else if (state != IR_READING && irReceiverEnabled) {
    irrecv.disableIRIn();
    irReceiverEnabled = false;
  }

  buttonUp.tick();
  buttonDown.tick();
  buttonOK.tick();
  buttonBack.tick();

  if (handleInputSuppress()) {
    return;
  }

  if (irMenuIndex >= getIRMenuItemCount()) {
    irMenuIndex = 0;
  }

  if (state == IR_SELECTION) {
    static byte lastMenuIndex = 255;
    if (irMenuIndex != lastMenuIndex) {
      displayIRSelection(irMenuIndex, irMenuIndex == IR_MENU_SEND ? irSelectedSignal : "");
      lastMenuIndex = irMenuIndex;
      Serial.println(irMenuIndex);
    }
    (void)buttonUp.isClick();
    (void)buttonDown.isClick();
    if (buttonOK.isClick()) {
      if (irMenuIndex == IR_MENU_TV_OFF) {
        switch (irMenuIndex) {
          case IR_MENU_TV_OFF: // TV-B-GONE
            state = SENDING_IR;
            irAbortRequested = false;
            resetTvbgState();
            Serial.println(F("Starting TV-B-GONE attack"));
            drawSendingScreen(0);
            break;
        }
      } else if (irMenuIndex == IR_MENU_SEND) {
        state = SENDING_IR;
        irAbortRequested = false;
        irOkReleasePending = false;
        irBackReleasePending = false;
        customSendLastTime = 0;
        customSendRepeatCount = 0;
        sendStartTime = millis();
        Serial.print(F("Starting IR-Send for signal: "));
        Serial.println(irSelectedSignal);
        drawSendingScreen(0);
      }
    }
    if (buttonBack.isClick()) {
      if (irMenuIndex == IR_MENU_SEND) {
        state = IR_SIGNAL_SUBMENU;
        display.clearDisplay();
        drawSignalSubmenu();
      } else {
        inIRMenu = true;
        state = MENU;
        display.clearDisplay();
        displayIRMenu(display, irMenuIndex);
        display.display();
      }
    }
  } else if (state == SENDING_IR) {
    unsigned long currentMillis = millis();
    if (irMenuIndex == IR_MENU_TV_OFF) {
      if (isAbortSendPressed()) {
        startInputSuppress();
        state = IR_SELECTION;
        resetTvbgState();
        displayIRSelection(irMenuIndex);
        return;
      }
      if (tvbg.totalCount == 0) {
        resetTvbgState();
      }
      if (tvbg.totalSent >= tvbg.totalCount) {
        state = IR_SELECTION;
        resetTvbgState();
        displayIRSelection(irMenuIndex);
        return;
      }
      if ((currentMillis - tvbg.lastSendTime) >= TVBG_CODE_DELAY_MS || tvbg.totalSent == 0) {
        if (sendNextTvbgCode()) {
          tvbg.lastSendTime = currentMillis;
          const int progress = map(tvbg.totalSent, 0, tvbg.totalCount, 0, 100);
          if (progress != tvbg.lastProgress) {
            drawSendingScreen(progress);
            tvbg.lastProgress = progress;
          }
          Serial.print(F("TV-B-Gone code sent, progress: "));
          Serial.print(progress);
          Serial.println('%');
        } else {
          state = IR_SELECTION;
          resetTvbgState();
          displayIRSelection(irMenuIndex);
          return;
        }
      }
    } else if (universalActive) {
      // OK/BACK are actions on the physical release edge. This is shared by
      // Sending and Paused, so both screens react identically and immediately.
      const bool okReleased = irOkReleasePending ||
        irButtonReleased(BUTTON_OK, universalOkWasDown, universalOkDownAt);
      const bool backReleased = irBackReleasePending ||
        irButtonReleased(BUTTON_BACK, universalBackWasDown, universalBackDownAt);
      irOkReleasePending = false;
      irBackReleasePending = false;
      const bool backWasConsumed = millis() < universalBackIgnoreUntil;
      const int progress = universalSignalCount > 0
        ? map(min<uint16_t>(universalSignalPosition, universalSignalCount), 0, universalSignalCount, 0, 100)
        : 0;
      if (universalLoadError) {
        // An SD load failure is informational only; do not turn OK into
        // Pause while the error popup is displayed. BACK uses the normal
        // Sending exit path below.
        // UP/DOWN are not actions on this popup. Consume their clicks so a
        // press made while the error is shown cannot navigate the submenu
        // after BACK returns to it.
        (void)buttonUp.isClick();
        (void)buttonDown.isClick();
        if (backReleased) {
          state = IR_SIGNAL_SUBMENU;
          universalLoadError = false;
          universalSignalCount = 0;
          universalSignalPosition = 0;
          universalPaused = false;
          irSignalIndex = universalSelectedAction;
          buttonBack.resetStates();
          buttonOK.resetStates();
          buttonUp.resetStates();
          buttonDown.resetStates();
          signalUpHeld.wasPressed = false;
          signalUpHeld.nextRepeatAt = 0;
          signalDownHeld.wasPressed = false;
          signalDownHeld.nextRepeatAt = 0;
          drawUniversalRemoteScreen();
        } else {
          drawUniversalSendingScreen(0);
        }
        return;
      }
      if (universalPaused) {
        if (irSignalIsNavButtonPress(BUTTON_UP, signalUpHeld) && universalSignalPosition + 1 < universalSignalCount) {
          universalSignalPosition++;
          drawUniversalSendingScreen(
            map(universalSignalPosition + 1, 0, universalSignalCount, 0, 100));
        }
        if (irSignalIsNavButtonPress(BUTTON_DOWN, signalDownHeld) && universalSignalPosition > 0) {
          universalSignalPosition--;
          drawUniversalSendingScreen(
            map(universalSignalPosition + 1, 0, universalSignalCount, 0, 100));
        }
        if (okReleased && universalSignalCount > 0 && universalSignalPosition < universalSignalCount) {
          sendIRSignal(
            universalRemoteFiles[universalCategory],
            0,
            universalSignalOffsets[universalSignalPosition]);
          drawUniversalSendingScreen(
            map(universalSignalPosition + 1, 0, universalSignalCount, 0, 100));
        }
        if (backReleased && !backWasConsumed) {
          universalPaused = false;
          state = SENDING_IR;
          customSendLastTime = 0;
          irOkReleasePending = false;
          irBackReleasePending = false;
          buttonBack.resetStates();
          buttonOK.resetStates();
          startInputSuppress();
          universalBackIgnoreUntil = millis() + 300;
          universalBackWasDown = false;
          universalBackDownAt = 0;
          drawUniversalSendingScreen(progress);
        }
        return;
      }
      if (backReleased) {
        state = IR_SIGNAL_SUBMENU;
        // Consume this BACK release so it cannot be handled again by the
        // signal submenu on the next loop iteration.
        buttonBack.resetStates();
        buttonOK.resetStates();
        buttonUp.resetStates();
        buttonDown.resetStates();
        signalUpHeld.wasPressed = false;
        signalUpHeld.nextRepeatAt = 0;
        signalDownHeld.wasPressed = false;
        signalDownHeld.nextRepeatAt = 0;
        universalSignalPosition = 0;
        universalSignalCount = 0;
        universalPaused = false;
        irSignalIndex = universalSelectedAction;
        drawUniversalRemoteScreen();
        return;
      }
      if (okReleased) {
        universalPaused = true;
        if (universalSignalPosition > 0) universalSignalPosition--;
        drawUniversalSendingScreen(
          map(universalSignalPosition + 1, 0, universalSignalCount, 0, 100));
        return;
      }
      if (universalSignalPosition >= universalSignalCount) {
        state = IR_SIGNAL_SUBMENU;
        buttonUp.resetStates();
        buttonDown.resetStates();
        signalUpHeld.wasPressed = false;
        signalUpHeld.nextRepeatAt = 0;
        signalDownHeld.wasPressed = false;
        signalDownHeld.nextRepeatAt = 0;
        universalSignalPosition = 0;
        universalSignalCount = 0;
        universalPaused = false;
        irSignalIndex = universalSelectedAction;
        drawUniversalRemoteScreen();
        return;
      }
      if (currentMillis - customSendLastTime >= CUSTOM_SEND_REPEAT_DELAY_MS || customSendLastTime == 0) {
        customSendLastTime = currentMillis;
        const bool sent = sendIRSignal(
              universalRemoteFiles[universalCategory],
              0,
              universalSignalOffsets[universalSignalPosition]);
        if (!sent) Serial.println(F("Skipping unsupported universal IR record"));
        universalSignalPosition++;
        drawUniversalSendingScreen(map(universalSignalPosition, 0, universalSignalCount, 0, 100));
      }
    } else if (irMenuIndex == IR_MENU_SEND) {
      if (buttonBack.isClick()) {
        state = IR_SELECTION;
        customSendRepeatCount = 0;
        display.clearDisplay();
        displayIRSelection(irMenuIndex, irSelectedSignal);
        return;
      }
      if (buttonOK.isClick()) {
        customSendLastTime = 0;
        customSendRepeatCount = 0;
        sendStartTime = millis();
        drawSendingScreen(0);
      }
      if (currentMillis - sendStartTime >= 2000) {
        state = IR_SELECTION;
        customSendRepeatCount = 0;
        display.clearDisplay();
        displayIRSelection(irMenuIndex, irSelectedSignal);
        return;
      }
      if (customSendRepeatCount < CUSTOM_SEND_REPEAT_LIMIT &&
          (customSendLastTime == 0 || currentMillis - customSendLastTime >= CUSTOM_SEND_REPEAT_DELAY_MS)) {
        customSendLastTime = currentMillis;
        if (sendIRSignal(irExplorer.selectedFile, irSignalIndex)) {
          customSendRepeatCount++;
          Serial.print(F("IR signal sent: "));
          Serial.println(irSelectedSignal);
          if (customSendRepeatCount >= CUSTOM_SEND_REPEAT_LIMIT) {
            state = IR_SELECTION;
            customSendRepeatCount = 0;
            display.clearDisplay();
            displayIRSelection(irMenuIndex, irSelectedSignal);
            return;
          }
          drawSendingScreen(0);
        } else {
          Serial.println(F("Failed to send IR signal"));
          state = IR_SELECTION;
          display.clearDisplay();
          displayIRSelection(irMenuIndex, irSelectedSignal);
          return;
        }
      }
    }
  } else if (state == IR_SIGNAL_SUBMENU) {
    if (universalActive && millis() < universalBackIgnoreUntil) {
      (void)buttonBack.isClick();
      (void)buttonOK.isClick();
      return;
    }
    static int lastSignalIndex = -1;
    static unsigned long lastSignalMarqueeDrawAt = 0;
    if (irSignalIndex != lastSignalIndex) {
      if (universalActive) drawUniversalRemoteScreen();
      else if (irRemoteMode) drawOriginalRemoteScreen();
      else drawSignalSubmenu();
      lastSignalIndex = irSignalIndex;
      lastSignalMarqueeDrawAt = millis();
    }
    const byte signalCount = universalActive ? universalActionCounts[universalCategory] : irSignalCount;
    // Universal action panels use one-step navigation only. Continuous
    // navigation is reserved for the Paused progress picker.
    const bool signalUpPress = (universalActive ? buttonUp.isClick()
                                                : irSignalIsNavButtonPress(BUTTON_UP, signalUpHeld)) && signalCount > 0;
    const bool signalDownPress = (universalActive ? buttonDown.isClick()
                                                  : irSignalIsNavButtonPress(BUTTON_DOWN, signalDownHeld)) && signalCount > 0;
    if (signalUpPress) {
      irSignalIndex = universalActive
        ? ((irSignalIndex == signalCount - 1) ? 0 : irSignalIndex + 1)
        : ((irSignalIndex == 0) ? (signalCount - 1) : irSignalIndex - 1);
    }
    if (signalDownPress) {
      irSignalIndex = universalActive
        ? ((irSignalIndex == 0) ? (signalCount - 1) : irSignalIndex - 1)
        : ((irSignalIndex == signalCount - 1) ? 0 : irSignalIndex + 1);
    }
    if (!signalUpPress && !signalDownPress && irSelectedSignalNeedsScroll() &&
        millis() - lastSignalMarqueeDrawAt >= 200) {
      lastSignalMarqueeDrawAt = millis();
      if (universalActive) drawUniversalRemoteScreen();
      else if (irRemoteMode) drawOriginalRemoteScreen();
      else drawSignalSubmenu();
    }
    if (buttonOK.isClick() && signalCount > 0) {
      irSelectedSignal = universalActive ? universalActions[universalCategory][irSignalIndex] : irSignalList[irSignalIndex];
      if (universalActive) {
        universalSelectedAction = irSignalIndex;
        // Enter the progress view first. SD indexing then happens while the
        // operation is already visibly at 0%, instead of appearing frozen in
        // the action menu.
        state = SENDING_IR;
        irAbortRequested = false;
        irOkReleasePending = false;
        irBackReleasePending = false;
        customSendLastTime = 0;
        universalPaused = false;
        drawUniversalSendingScreen(0);
        if (collectUniversalSignalIndices(irSelectedSignal)) {
          // The first loop iteration starts transmission immediately after
          // the SD index has been prepared.
          // Ignore button releases that happened while the loading scan was
          // in progress; loading itself never has Pause semantics.
          irOkReleasePending = false;
          irBackReleasePending = false;
        } else {
          if (universalLoadCanceled) {
            state = IR_SIGNAL_SUBMENU;
            universalSignalCount = 0;
            universalSignalPosition = 0;
            universalPaused = false;
            irSignalIndex = universalSelectedAction;
            buttonBack.resetStates();
            buttonOK.resetStates();
            drawUniversalRemoteScreen();
            return;
          }
          // Keep the progress popup visible and report the SD load failure.
          universalLoadError = true;
          universalPaused = false;
          drawUniversalSendingScreen(0);
        }
      } else if (irRemoteMode) {
        if (sendIRSignal(irExplorer.selectedFile, irSignalIndex)) {
          Serial.print(F("Remote button sent: "));
          Serial.println(irSelectedSignal);
        }
        drawOriginalRemoteScreen();
      } else {
        state = IR_SELECTION;
        displayIRSelection(irMenuIndex, irSelectedSignal);
      }
      Serial.print(F("Selected signal: "));
      Serial.println(irSelectedSignal);
    }
    if (buttonBack.isClick()) {
      if (universalActive) {
        universalActive = false;
        irSignalCount = 0;
        // Return to the category that opened this remote.
        irSignalIndex = universalCategory;
        state = IR_UNIVERSAL_MENU;
        if (submenu == 1) displayUniversalRemoteMenu();
        else displayOriginalRemoteMenu();
      } else {
        state = IR_FILE_EXPLORER;
        irSignalCount = 0;
        irSignalIndex = 0;
        irSelectedSignal = "";
        display.clearDisplay();
        ExplorerDraw(irExplorer, display);
      }
    }
    yield();
  } else if (state == IR_UNIVERSAL_MENU) {
    static MenuButtonState universalUpHeld;
    static MenuButtonState universalDownHeld;
    if (isMenuButtonPress(BUTTON_UP, universalUpHeld, getMenuSubmenuRepeatDelay(true))) {
      const byte previousIndex = irSignalIndex;
      irSignalIndex = (irSignalIndex + UNIVERSAL_REMOTE_ITEM_COUNT - 1) % UNIVERSAL_REMOTE_ITEM_COUNT;
      drawCategoryMenu(previousIndex);
    }
    if (isMenuButtonPress(BUTTON_DOWN, universalDownHeld, getMenuSubmenuRepeatDelay(true))) {
      const byte previousIndex = irSignalIndex;
      irSignalIndex = (irSignalIndex + 1) % UNIVERSAL_REMOTE_ITEM_COUNT;
      drawCategoryMenu(previousIndex);
    }
    if (buttonOK.isClick()) {
      universalCategory = irSignalIndex;
      universalActive = true;
      irSignalIndex = 0;
    // Names are already part of the universal layout. Do not scan SD
      // just to enter the remote screen; SD is touched only when transmitting.
      irExplorer.currentDir = "/infrared/assets";
      irExplorer.selectedFile = universalRemoteFiles[universalCategory];
      irSignalCount = universalActionCounts[universalCategory];
      for (byte i = 0; i < irSignalCount; ++i) {
        irSignalList[i] = universalActions[universalCategory][i];
      }
      // Do not carry the category-menu DOWN event into the first action
      // screen. Otherwise the new remote opens on its last action.
      buttonUp.resetStates();
      buttonDown.resetStates();
      signalUpHeld.wasPressed = false;
      signalUpHeld.nextRepeatAt = 0;
      signalDownHeld.wasPressed = false;
      signalDownHeld.nextRepeatAt = 0;
      state = IR_SIGNAL_SUBMENU;
      drawUniversalRemoteScreen();
    }
    if (buttonBack.isClick()) {
      inIRMenu = true;
      setUniversalPortrait(false);
      state = MENU;
      displayIRMenu(display, irMenuIndex);
      display.display();
    }
  } else if (state == IR_FILE_EXPLORER) {
    ExplorerAction action = ExplorerHandle(
      irExplorer,
      irExplorerCfg,
      display,
      buttonUp.isClick(),
      buttonDown.isClick(),
      buttonOK.isClick(),
      buttonBack.isClick(),
      buttonBack.isHold()
    );
    if (action == EXPLORER_SELECT_FILE) {
      irSignalIndex = 0;
      irSignalCount = 0;
      if (loadIRSignals(irExplorer.selectedFile)) {
        state = IR_SIGNAL_SUBMENU;
        display.clearDisplay();
        if (irRemoteMode) drawOriginalRemoteScreen();
        else drawSignalSubmenu();
        Serial.print(F("Selected IR file: "));
        Serial.println(irExplorer.selectedFile);
      } else {
        ExplorerShowSDError(display);
        ExplorerDraw(irExplorer, display);
      }
    } else if (action == EXPLORER_EXIT) {
      inIRMenu = true;
      state = MENU;
      irExplorer.selectedFile = "";
      display.clearDisplay();
      displayIRMenu(display, irMenuIndex);
      display.display();
    }
  } else if (state == IR_READING) {
    static int lastSignalRead = -1;
    if (irrecv.decode(&results)) {
      if (results.repeat) {
        Serial.println(F("Ignoring repeat IR frame"));
      } else {
        readSignal = true;
        signalsRead++;
        if (signalsRead <= 20) {
          appendToDeviceContent("Signal " + String(signalsRead));
          Serial.println(F("IR signal captured"));
        } else {
          Serial.println(F("IR signal captured but not stored (limit)"));
        }
      }
      irrecv.resume();
    }
    if (signalsRead != lastSignalRead) {
      drawReadingScreen();
      lastSignalRead = signalsRead;
      readSignal = false;
    }
    if (buttonOK.isHold()) {
      if (signalsRead > 0) {
        if (saveIRSignal()) {
          drawSaveConfirm();
          delay(1000);
          signalsRead = 0;
          strDeviceContent = "";
          readSignal = false;
          irrecv.resume();
          drawReadingScreen();
          Serial.println(F("IR signal saved"));
        } else {
          ExplorerShowSDError(display);
          readSignal = false;
          irrecv.resume();
          drawReadingScreen();
          Serial.println(F("Failed to save IR signal"));
        }
      }
    }
    if (buttonBack.isClick()) {
      setUniversalPortrait(false);
      startInputSuppress();
      inIRMenu = true;
      state = MENU;
      readSignal = false;
      signalsRead = 0;
      strDeviceContent = "";
      irrecv.disableIRIn();
      irReceiverEnabled = false;
      display.clearDisplay();
      displayIRMenu(display, irMenuIndex);
      display.display();
    }
    yield();
  } else {
    static MenuButtonState upHeld;
    static MenuButtonState downHeld;
    if (!inIRMenu) {
      inIRMenu = true;
      display.clearDisplay();
      displayIRMenu(display, irMenuIndex);
      display.display();
    }
    static byte lastMenuIndex = 255;
    if (irMenuIndex != lastMenuIndex) {
      display.clearDisplay();
      displayIRMenu(display, irMenuIndex);
      display.display();
      lastMenuIndex = irMenuIndex;
    }
    const unsigned long repeatDelayMs = getMenuSubmenuRepeatDelay(submenu == 1);
    if (isMenuButtonPress(BUTTON_UP, upHeld, repeatDelayMs)) {
      byte previousIndex = irMenuIndex;
      irMenuIndex = (irMenuIndex - 1 + getIRMenuItemCount()) % getIRMenuItemCount();
      displayIRMenu(display, irMenuIndex, previousIndex);
      lastMenuIndex = irMenuIndex;
    }
    if (isMenuButtonPress(BUTTON_DOWN, downHeld, repeatDelayMs)) {
      byte previousIndex = irMenuIndex;
      irMenuIndex = (irMenuIndex + 1) % getIRMenuItemCount();
      displayIRMenu(display, irMenuIndex, previousIndex);
      lastMenuIndex = irMenuIndex;
    }
    if (buttonOK.isClick()) {
      switch (irMenuIndex) {
      case IR_MENU_SEND: // Send
          if (!ensureSDReadyInteractive(true)) {
            displayIRMenu(display, irMenuIndex);
            display.display();
            break;
          }
          state = IR_FILE_EXPLORER;
          ExplorerInit(irExplorer, irFileList, MAX_FILES, irExplorerCfg);
          ExplorerLoad(irExplorer, irExplorerCfg);
          display.clearDisplay();
          ExplorerDraw(irExplorer, display);
          irRemoteMode = false;
          break;
        case IR_MENU_REMOTE: // Remote / Universal Remote
          // Both interfaces use the same SD-backed universal remote data.
          // Only their category/action selector presentation differs; there
          // is no file browser in either Remote entry point.
          if (!ensureSDReadyInteractive(true)) {
            displayIRMenu(display, irMenuIndex);
            display.display();
            break;
          }
          // The selected asset is opened lazily only when a button is
          // transmitted; no file browser is shown.
          irRemoteMode = false;
          state = IR_UNIVERSAL_MENU;
          irSignalIndex = 0;
        drawCategoryMenu();
          break;
        case IR_MENU_READ: // IR-Read
          if (!ensureSDReadyInteractive(true)) {
            displayIRMenu(display, irMenuIndex);
            display.display();
            break;
          }
          state = IR_READING;
          readSignal = false;
          signalsRead = 0;
          strDeviceContent = "";
          display.clearDisplay();
          drawReadingScreen();
          break;
        case IR_MENU_TV_OFF: // TV-B-GONE
          state = IR_SELECTION;
          display.clearDisplay();
          displayIRSelection(irMenuIndex);
          break;
        default:
          inIRMenu = true;
          state = MENU;
          display.clearDisplay();
          displayIRMenu(display, irMenuIndex);
          display.display();
          break;
      }
    }
    if (buttonBack.isClick()) {
      inIRMenu = false;
      state = MENU;
      currentMenu = 3;
      display.clearDisplay();
      returnToMainMenu();
      display.display();
    }
  }
}
