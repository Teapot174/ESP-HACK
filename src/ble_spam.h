#pragma once
#include <Arduino.h>
#include <NimBLEBeacon.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <esp_gap_ble_api.h>

// Bluetooth max TX
#define MAX_TX_POWER ESP_PWR_LVL_P9 

enum EBLEPayloadType { Microsoft, SourApple, AppleJuice, Samsung, Google };

struct Device {
    const char* name;
    uint32_t code;
};

struct AppleDeviceMapping {
    uint8_t* packet;
    uint8_t size;
    const char* name;
};

// IOS-Devices
extern uint8_t Airpods[31];
extern uint8_t AirpodsPro[31];
extern uint8_t AirpodsMax[31];
extern uint8_t AirpodsGen2[31];
extern uint8_t AirpodsGen3[31];
extern uint8_t AirpodsProGen2[31];
extern uint8_t PowerBeats[31];
extern uint8_t PowerBeatsPro[31];
extern uint8_t BeatsSoloPro[31];
extern uint8_t BeatsStudioBuds[31];
extern uint8_t BeatsFlex[31];
extern uint8_t BeatsX[31];
extern uint8_t BeatsSolo3[31];
extern uint8_t BeatsStudio3[31];
extern uint8_t BeatsStudioPro[31];
extern uint8_t BeatsFitPro[31];
extern uint8_t BeatsStudioBudsPlus[31];
extern uint8_t AppleTVSetup[23];
extern uint8_t AppleTVPair[23];
extern uint8_t AppleTVNewUser[23];
extern uint8_t AppleTVAppleIDSetup[23];
extern uint8_t AppleTVWirelessAudioSync[23];
extern uint8_t AppleTVHomekitSetup[23];
extern uint8_t AppleTVKeyboard[23];
extern uint8_t AppleTVConnectingToNetwork[23];
extern uint8_t HomepodSetup[23];
extern uint8_t SetupNewPhone[23];
extern uint8_t TransferNumber[23];
extern uint8_t TVColorBalance[23];
extern uint8_t AppleVisionPro[23];

// Android-Devices
extern const Device devices[];
extern const int devicesCount;

// Apple Devices Mapping
extern const AppleDeviceMapping appleDevices[];
extern const int appleDevicesCount;

void aj_adv(int ble_choice);
void ibeacon(const char* DeviceName = "ESP-HACK", const char* BEACON_UUID = "8ec76ea3-6668-48da-9866-75be8bc86f4d", int ManufacturerId = 0x4C00);
void executeSpam(EBLEPayloadType type);
void generateRandomMac(uint8_t *mac);
BLEAdvertisementData GetUniversalAdvertisementData(EBLEPayloadType Type);