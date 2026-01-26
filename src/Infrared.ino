#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <GyverButton.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <SD.h>
#include "CONFIG.h"
#include "menu/infrared.h"
#include "menu/subghz.h"

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

extern Adafruit_SH1106G display;
extern GButton buttonUp;
extern GButton buttonDown;
extern GButton buttonOK;
extern GButton buttonBack;
extern byte currentMenu;
extern bool inMenu;
extern byte irMenuIndex;

bool inIRMenu = false;

enum AppState { MENU, IR_SELECTION, SENDING_IR, IR_FILE_EXPLORER, IR_READING, IR_DELETE_CONFIRM, IR_SIGNAL_SUBMENU };
AppState state = MENU;

volatile bool irAbortRequested = false;
void IRAM_ATTR onIrAbort() {
  irAbortRequested = true;
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

// Projector codes
const uint32_t pjOffSignals[] = {
  0xA90, 0x4B74, 0xB04F, 0xA59A, 0xB90,
  0xC90, 0xD90, 0x4BB4, 0x30CF, 0xA55A
};

// AC codes
const uint32_t acOffSignals[] = {
  0xB2F, 0xB2E, 0xB2D, 0xB2C, 0xA1F0,
  0xE0E09966, 0xC2A4, 0xB4C8, 0xB2A, 0xA1E0,
  0xE0E019E6, 0xC2A5, 0xB4C9
};

constexpr int numPJSignals = sizeof(pjOffSignals) / sizeof(pjOffSignals[0]);
constexpr int numACSignals = sizeof(acOffSignals) / sizeof(acOffSignals[0]);

struct FixedAttackState {
  const uint32_t* signals = nullptr;
  int count = 0;
  int index = 0;
  unsigned long lastSendTime = 0;
  int lastProgress = -1;
};

FixedAttackState fixedAttack;

#define MAX_FILES 50
struct IRFileEntry {
  String name;
  bool isDir;
};
IRFileEntry irFileList[MAX_FILES];
int irFileCount = 0;
int irFileIndex = 0;
bool irInFileExplorer = false;
bool irInDeleteConfirm = false;
String irSelectedFile = "";
String irCurrentDir = "/Infrared";

#define MAX_SIGNALS 20
String irSignalList[MAX_SIGNALS];
int irSignalCount = 0;
int irSignalIndex = 0;
String irSelectedSignal = "";

IRsend irsend(IR_TRANSMITTER);
IRrecv irrecv(IR_RECIVER, 1024, 100);
decode_results results;

bool readSignal = false;
String strDeviceContent = "";
int signalsRead = 0;
uint16_t* rawcode = nullptr;
uint16_t raw_data_len = 0;
#define IR_FREQUENCY 38000
#define DUTY_CYCLE 0.330000
unsigned long customSendLastTime = 0;

void initIR() {
  irsend.begin();
  pinMode(IR_RECIVER, INPUT);
  static bool abortIsrAttached = false;
  if (!abortIsrAttached) {
    attachInterrupt(digitalPinToInterrupt(BUTTON_OK), onIrAbort, RISING);
    attachInterrupt(digitalPinToInterrupt(BUTTON_BACK), onIrAbort, RISING);
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

void startFixedAttack(const uint32_t* signals, int count, const __FlashStringHelper* logMsg) {
  fixedAttack = {};
  fixedAttack.signals = signals;
  fixedAttack.count = count;
  irAbortRequested = false;
  drawSendingScreen(0);
  Serial.println(logMsg);
}

bool isAbortSendPressed() {
  if (irAbortRequested) return true;
  if (buttonOK.isClick() || buttonBack.isClick()) {
    irAbortRequested = true;
    return true;
  }
  return false;
}

bool handleFixedAttack(unsigned long currentMillis) {
  if (isAbortSendPressed()) {
    startInputSuppress();
    return false;
  }
  if (fixedAttack.signals == nullptr || fixedAttack.count == 0) {
    return false;
  }
  if (fixedAttack.index >= fixedAttack.count) {
    return false;
  }
  if ((currentMillis - fixedAttack.lastSendTime) >= 15) {
    fixedAttack.lastSendTime = currentMillis;
    const uint32_t signal = fixedAttack.signals[fixedAttack.index];
    irsend.sendNEC(signal, 32);
    fixedAttack.index++;
    const int progress = map(fixedAttack.index, 0, fixedAttack.count, 0, 100);
    if (progress != fixedAttack.lastProgress) {
      drawSendingScreen(progress);
      fixedAttack.lastProgress = progress;
    }
    Serial.print(F("IR signal sent: 0x"));
    Serial.println(signal, HEX);
  }
  return true;
}

void displayIRSelection(byte menuIndex, String signalName = "") {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setTextSize(1);
  display.setCursor(62, 43);
  display.print(F("Press OK."));
  
  if (menuIndex == 0 || menuIndex == 2 || menuIndex == 3 || menuIndex == 4) {
    display.drawBitmap(14, 12, image_Power_bits, 38, 40, SH110X_WHITE);
  }
  if (menuIndex == 0 && signalName != "") {
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
  display.setCursor(69, 23);
  if (irMenuIndex == 2 || irMenuIndex == 3 || irMenuIndex == 4) {
    display.print(progress);
    display.print(F("%"));
  }
  
  if (irMenuIndex == 0 || irMenuIndex == 2 || irMenuIndex == 3 || irMenuIndex == 4) {
    display.drawBitmap(14, 12, image_Power_hvr_bits, 38, 40, SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(62, 43);
    display.print(irMenuIndex == 0 ? F("Sending...") : F("IR-Attack."));
  }
  
  display.display();
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
    display.setCursor(1, 1);
    display.print(F("Signal:"));

    // Code
    display.setCursor(1, 12);
    display.print(F("Code: "));
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
    display.print(code.length() > 16 ? code.substring(0, 16) : code);

    // Type
    display.setCursor(1, 24);
    display.print(F("Type: "));
    String signalType = (results.decode_type == UNKNOWN ? "RAW" : typeToString(results.decode_type, results.repeat));
    display.print(signalType.length() > 16 ? signalType.substring(0, 16) : signalType);

    // Bits
    display.setCursor(1, 36);
    display.print(F("Bits: "));
    display.print(results.bits);

    display.setCursor(1, 51);
    display.print(F("Hold OK to save"));
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

void drawFileExplorer() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setTextColor(SH110X_WHITE);

  display.setCursor(1, 1);
  display.print(irCurrentDir);

  display.setCursor(1, 12);
  display.println(F("---------------------"));

  if (irFileCount == 0) {
    display.setCursor(1, 24);
    display.println(F("No files."));
    display.display();
    return;
  }

  const int perPage = 4;
  int maxStart = (irFileCount > perPage) ? (irFileCount - perPage) : 0;
  int startIndex = irFileIndex;
  if (startIndex > maxStart) startIndex = maxStart;
  for (int i = 0; i < perPage; i++) {
    int idx = startIndex + i;
    if (idx >= irFileCount) break;
    display.setCursor(1, 24 + i * 10);
    if (idx == irFileIndex) {
      display.fillRect(0, 24 + i * 10 - 1, display.width(), 10, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }
    String name = irFileList[idx].name;
    if (irFileList[idx].isDir) name = "[" + name + "]";
    display.print(name);
  }
  display.display();
}

void drawSignalSubmenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setTextColor(SH110X_WHITE);

  display.setCursor(1, 1);
  String name = irSelectedFile;
  if (name.length() > 16) name = name.substring(0, 16);
  display.print(F("File: "));
  display.print(name);
  display.setCursor(1, 12);
  display.println(F("---------------------"));

  if (irSignalCount == 0) {
    display.setCursor(1, 24);
    display.println(F("No signals."));
    display.display();
    return;
  }

  const int perPage = 4;
  int maxStart = (irSignalCount > perPage) ? (irSignalCount - perPage) : 0;
  int startIndex = irSignalIndex;
  if (startIndex > maxStart) startIndex = maxStart;
  for (int i = 0; i < perPage; i++) {
    int idx = startIndex + i;
    if (idx >= irSignalCount) break;
    display.setCursor(1, 24 + i * 10);
    if (idx == irSignalIndex) {
      display.fillRect(0, 24 + i * 10 - 1, display.width(), 10, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }
    String signalName = irSignalList[idx];
    if (signalName.length() > 16) signalName = signalName.substring(0, 16);
    display.print(signalName);
  }
  display.display();
}

void drawDeleteConfirm() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(1, 1);
  String name = irFileList[irFileIndex].name;
  if (name.length() > 16) name = name.substring(0, 16);
  display.print(F("File: "));
  display.print(name);
  display.setCursor(1, 24);
  display.print(F("Press OK to DELETE."));
  display.display();
}

void loadIRSignals(String fileName) {
  irSignalCount = 0;
  File file = SD.open(irCurrentDir + "/" + fileName, FILE_READ);
  if (!file) {
    Serial.print(F("Failed to open file for signals: "));
    Serial.println(fileName);
    return;
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
}

bool sendIRSignal(String fileName, int signalIdx) {
  File file = SD.open(irCurrentDir + "/" + fileName, FILE_READ);
  if (!file) {
    Serial.print(F("Failed to open file: "));
    Serial.println(fileName);
    return false;
  }

  String protocol = "";
  String address = "";
  String command = "";
  String rawData = "";
  uint16_t frequency = 38000;
  uint8_t bits = 32;
  bool parsedMode = false;
  int currentSignal = -1;

  String line;
  while (file.available()) {
    line = file.readStringUntil('\n');
    line.trim();
    if (line.startsWith("name:")) {
      currentSignal++;
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
      }
    }
  }
  file.close();

  if (currentSignal < signalIdx) {
    Serial.println(F("Signal index not found in file"));
    return false;
  }

  if (parsedMode && protocol != "" && address != "" && command != "") {
    if (protocol.equalsIgnoreCase("Samsung32")) {
      uint8_t addressValue = strtoul(address.substring(0, 2).c_str(), nullptr, 16);
      uint8_t commandValue = strtoul(command.substring(0, 2).c_str(), nullptr, 16);
      uint64_t data = irsend.encodeSAMSUNG(addressValue, commandValue);
      irsend.sendSAMSUNG(data, 32);
      return true;
    } else if (protocol.equalsIgnoreCase("SONY")) {
      uint8_t commandValue = strtoul(command.substring(0, 2).c_str(), nullptr, 16);
      irsend.sendSony(commandValue, 12);
      return true;
    }
    Serial.print(F("Unsupported protocol: "));
    Serial.println(protocol);
    return false;
  } else if (!parsedMode && rawData != "" && frequency != 0) {
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
    irsend.sendRaw(dataBuffer, count, frequency);
    free(dataBuffer);
    return true;
  }
  Serial.println(F("Invalid IR signal format"));
  return false;
}

void loadIRFileList() {
  irFileCount = 0;
  File dir = SD.open(irCurrentDir);
  if (!dir) {
    Serial.print(F("Failed to open directory: "));
    Serial.println(irCurrentDir);
    return;
  }
  while (irFileCount < MAX_FILES) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (entry.isDirectory()) {
      irFileList[irFileCount].name = entry.name();
      irFileList[irFileCount].isDir = true;
      irFileCount++;
    }
    entry.close();
  }
  dir.rewindDirectory();
  while (irFileCount < MAX_FILES) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory() && String(entry.name()).endsWith(".ir")) {
      irFileList[irFileCount].name = entry.name();
      irFileList[irFileCount].isDir = false;
      irFileCount++;
    }
    entry.close();
  }
  dir.close();
  Serial.print(F("Found "));
  Serial.print(irFileCount);
  Serial.println(F(" entries"));
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
  strDeviceContent += "name: " + btn_name + "\n";
  strDeviceContent += "type: parsed\n";
  switch (results.decode_type) {
    case decode_type_t::SAMSUNG: {
      strDeviceContent += "protocol: Samsung32\n";
      break;
    }
    case decode_type_t::SONY: {
      strDeviceContent += "protocol: SONY\n";
      break;
    }
    case decode_type_t::NEC: {
      strDeviceContent += "protocol: NEC\n";
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
      strDeviceContent += "protocol: " + typeToString(results.decode_type, results.repeat) + "\n";
      break;
    }
  }
  strDeviceContent += "address: " + uint32ToString(results.address) + "\n";
  strDeviceContent += "command: " + uint32ToString(results.command) + "\n";
  strDeviceContent += "#\n";
}

bool saveIRSignal() {
  String filename = "Infrared";
  int index = 1;
  File dir = SD.open(irCurrentDir);
  if (!dir) {
    Serial.println(F("Failed to open /Infrared directory"));
    return false;
  }
  dir.close();
  SD.mkdir(irCurrentDir);
  while (SD.exists(irCurrentDir + "/" + filename + "_" + String(index) + ".ir")) {
    index++;
  }
  filename = irCurrentDir + "/" + filename + "_" + String(index) + ".ir";
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

  if (!irInitialized) {
    initIR();
    irInitialized = true;
    buttonUp.setDebounce(50);
    buttonDown.setDebounce(50);
    buttonOK.setDebounce(50);
    buttonBack.setDebounce(50);
    buttonUp.setTimeout(500);
    buttonDown.setTimeout(500);
    buttonOK.setTimeout(500);
    buttonBack.setTimeout(500);
    buttonUp.setClickTimeout(300);
    buttonDown.setClickTimeout(300);
    buttonOK.setClickTimeout(300);
    buttonBack.setClickTimeout(300);
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

  if (irMenuIndex >= IR_MENU_ITEM_COUNT) {
    irMenuIndex = 0;
  }

  if (state == IR_SELECTION) {
    static byte lastMenuIndex = 255;
    if (irMenuIndex != lastMenuIndex) {
      displayIRSelection(irMenuIndex, irMenuIndex == 0 ? irSelectedSignal : "");
      lastMenuIndex = irMenuIndex;
      Serial.println(irMenuIndex);
    }
    (void)buttonUp.isClick();
    (void)buttonDown.isClick();
    if (buttonOK.isClick()) {
      if (irMenuIndex == 2 || irMenuIndex == 3 || irMenuIndex == 4) {
        switch (irMenuIndex) {
          case 2: // PJ-OFF
            state = SENDING_IR;
            startFixedAttack(pjOffSignals, numPJSignals, F("Starting PJ-OFF IR attack (NEC)"));
            break;
          case 3: // AC-OFF
            state = SENDING_IR;
            startFixedAttack(acOffSignals, numACSignals, F("Starting AC-OFF IR attack (NEC)"));
            break;
          case 4: // TV-OFF
            state = SENDING_IR;
            irAbortRequested = false;
            resetTvbgState();
            Serial.println(F("Starting TV-OFF attack"));
            drawSendingScreen(0);
            break;
        }
      } else if (irMenuIndex == 0) {
        state = SENDING_IR;
        irAbortRequested = false;
        customSendLastTime = 0;
        sendStartTime = millis();
        Serial.print(F("Starting IR-Send for signal: "));
        Serial.println(irSelectedSignal);
        drawSendingScreen(0);
      }
    }
    if (buttonBack.isClick()) {
      if (irMenuIndex == 0) {
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
    if (irMenuIndex == 4) {
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
    } else if (irMenuIndex == 2 || irMenuIndex == 3) {
      if (isAbortSendPressed()) {
        startInputSuppress();
        state = IR_SELECTION;
        fixedAttack = {};
        displayIRSelection(irMenuIndex);
        return;
      }
      if (!handleFixedAttack(currentMillis)) {
        state = IR_SELECTION;
        fixedAttack = {};
        displayIRSelection(irMenuIndex);
        return;
      }
    } else if (irMenuIndex == 0) {
      if (buttonBack.isClick()) {
        state = IR_SELECTION;
        display.clearDisplay();
        displayIRSelection(irMenuIndex, irSelectedSignal);
        return;
      }
      if (buttonOK.isClick()) {
        sendStartTime = millis();
        drawSendingScreen(0);
      }
      if (currentMillis - sendStartTime >= 2000) {
        state = IR_SELECTION;
        display.clearDisplay();
        displayIRSelection(irMenuIndex, irSelectedSignal);
        return;
      }
      if (currentMillis - customSendLastTime >= 15) {
        customSendLastTime = currentMillis;
        if (sendIRSignal(irSelectedFile, irSignalIndex)) {
          Serial.print(F("IR signal sent: "));
          Serial.println(irSelectedSignal);
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
    static int lastSignalIndex = -1;
    if (irSignalIndex != lastSignalIndex) {
      drawSignalSubmenu();
      lastSignalIndex = irSignalIndex;
    }
    if (buttonUp.isClick()) {
      irSignalIndex = (irSignalIndex == 0) ? (irSignalCount - 1) : irSignalIndex - 1;
    }
    if (buttonDown.isClick()) {
      irSignalIndex = (irSignalIndex == irSignalCount - 1) ? 0 : irSignalIndex + 1;
    }
    if (buttonOK.isClick() && irSignalCount > 0) {
      irSelectedSignal = irSignalList[irSignalIndex];
      state = IR_SELECTION;
      displayIRSelection(irMenuIndex, irSelectedSignal);
      Serial.print(F("Selected signal: "));
      Serial.println(irSelectedSignal);
    }
    if (buttonBack.isClick()) {
      state = IR_FILE_EXPLORER;
      irInFileExplorer = true;
      irSignalCount = 0;
      irSignalIndex = 0;
      irSelectedSignal = "";
      display.clearDisplay();
      drawFileExplorer();
    }
    yield();
  } else if (state == IR_FILE_EXPLORER && irInFileExplorer && !irInDeleteConfirm) {
    static int lastFileIndex = -1;
    if (irFileIndex != lastFileIndex) {
      drawFileExplorer();
      lastFileIndex = irFileIndex;
    }
    if (buttonUp.isClick()) {
      irFileIndex = (irFileIndex == 0) ? (irFileCount - 1) : irFileIndex - 1;
    }
    if (buttonDown.isClick()) {
      irFileIndex = (irFileIndex == irFileCount - 1) ? 0 : irFileIndex + 1;
    }
    if (buttonOK.isClick() && irFileCount > 0) {
      if (irFileList[irFileIndex].isDir) {
        irCurrentDir += "/" + irFileList[irFileIndex].name;
        irFileIndex = 0;
        irFileCount = 0;
        loadIRFileList();
        display.clearDisplay();
        drawFileExplorer();
      } else {
        irInFileExplorer = false;
        irInDeleteConfirm = false;
        irSelectedFile = irFileList[irFileIndex].name;
        irSignalIndex = 0;
        irSignalCount = 0;
        loadIRSignals(irSelectedFile);
        state = IR_SIGNAL_SUBMENU;
        display.clearDisplay();
        drawSignalSubmenu();
        Serial.print(F("Selected IR file: "));
        Serial.println(irSelectedFile);
      }
    }
    if (buttonBack.isHold()) {
      irInDeleteConfirm = true;
      display.clearDisplay();
      drawDeleteConfirm();
    }
    if (buttonBack.isClick()) {
      if (irCurrentDir != "/Infrared") {
        int lastSlash = irCurrentDir.lastIndexOf('/');
        irCurrentDir = irCurrentDir.substring(0, lastSlash);
        if (irCurrentDir == "") irCurrentDir = "/Infrared";
        irFileIndex = 0;
        irFileCount = 0;
        loadIRFileList();
        display.clearDisplay();
        drawFileExplorer();
      } else {
        inIRMenu = false;
        state = MENU;
        inMenu = true;
        currentMenu = 3;
        irInFileExplorer = false;
        irInDeleteConfirm = false;
        irSelectedFile = "";
        display.clearDisplay();
        OLED_printMenu(display, currentMenu);
        display.display();
      }
    }
    yield();
  } else if (state == IR_FILE_EXPLORER && irInFileExplorer && irInDeleteConfirm) {
    if (buttonOK.isClick()) {
      String filePath = irCurrentDir + "/" + irFileList[irFileIndex].name;
      if (SD.remove(filePath)) {
        display.clearDisplay();
        display.drawBitmap(5, 2, image_DolphinMafia_bits, 119, 62, SH110X_WHITE);
        display.setTextColor(SH110X_WHITE);
        display.setTextWrap(false);
        display.setCursor(84, 15);
        display.print(F("Deleted"));
        display.display();
        Serial.print(F("File deleted: "));
        Serial.println(filePath);
        delay(1000);
        irFileCount = 0;
        irFileIndex = 0;
        loadIRFileList();
        irInDeleteConfirm = false;
        display.clearDisplay();
        drawFileExplorer();
      } else {
        display.clearDisplay();
        display.setTextColor(SH110X_WHITE);
        display.setTextWrap(false);
        display.setCursor(1, 1);
        display.print(F("Failed to delete"));
        display.display();
        Serial.print(F("Failed to delete file: "));
        Serial.println(filePath);
        delay(1000);
        irInDeleteConfirm = false;
        display.clearDisplay();
        drawFileExplorer();
      }
    }
    if (buttonBack.isClick()) {
      irInDeleteConfirm = false;
      display.clearDisplay();
      drawFileExplorer();
    }
  } else if (state == IR_READING) {
    static int lastSignalRead = -1;
    if (irrecv.decode(&results)) {
      readSignal = true;
      signalsRead++;
      if (signalsRead <= 20) {
        appendToDeviceContent("Signal " + String(signalsRead));
        Serial.println(F("IR signal captured"));
      } else {
        Serial.println(F("IR signal captured but not stored (limit)"));
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
          display.clearDisplay();
          display.setTextColor(SH110X_WHITE);
          display.setTextWrap(false);
          display.setCursor(1, 1);
          display.print(F("Failed to save"));
          display.display();
          delay(1000);
          readSignal = false;
          irrecv.resume();
          drawReadingScreen();
          Serial.println(F("Failed to save IR signal"));
        }
      }
    }
    if (buttonBack.isClick()) {
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
    if (buttonUp.isClick()) {
      irMenuIndex = (irMenuIndex - 1 + IR_MENU_ITEM_COUNT) % IR_MENU_ITEM_COUNT;
    }
    if (buttonDown.isClick()) {
      irMenuIndex = (irMenuIndex + 1) % IR_MENU_ITEM_COUNT;
    }
    if (buttonOK.isClick()) {
      switch (irMenuIndex) {
        case 0: // IR-Send
          state = IR_FILE_EXPLORER;
          irInFileExplorer = true;
          irFileIndex = 0;
          irFileCount = 0;
          irCurrentDir = "/Infrared";
          loadIRFileList();
          display.clearDisplay();
          drawFileExplorer();
          break;
        case 1: // IR-Read
          state = IR_READING;
          readSignal = false;
          signalsRead = 0;
          strDeviceContent = "";
          display.clearDisplay();
          drawReadingScreen();
          break;
        case 2:
        case 3:
        case 4:
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
      inMenu = true;
      currentMenu = 3;
      display.clearDisplay();
      OLED_printMenu(display, currentMenu);
      display.display();
    }
  }
}
