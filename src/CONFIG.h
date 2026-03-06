#ifndef PIN_H
#define PIN_H

// Display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADR 0x3C
#define OLED_RESET -1
#define OLED_SCL 22
#define OLED_SDA 21
// Driver
#define DISPLAY_SH1106 0
#define DISPLAY_SSD1306 1
#define DISPLAY_TYPE DISPLAY_SH1106   // DISPLAY_SH1106 | DISPLAY_SSD1306

// Buttons
#define BUTTON_UP 27    // Up
#define BUTTON_DOWN 26  // Down
#define BUTTON_OK 33    // OK
#define BUTTON_BACK 32  // Back

// SD Card
#define SD_CS 15
#define SD_MOSI 13
#define SD_CLK 14
#define SD_MISO 17

// CC1101
#define CC1101_GDO0 4
#define CC1101_CS 5
#define CC1101_SCK 18
#define CC1101_MOSI 23
#define CC1101_MISO 19

// Infrared
#define IR_TRANSMITTER 16
#define IR_RECIVER 35

// GPIO
#define GPIO_A 16
#define GPIO_B 12
#define GPIO_C 18
#define GPIO_D 23
#define GPIO_E 19
#define GPIO_F 25

// Firmware version
static const char* FIRMWARE = "v0.7";

#endif
