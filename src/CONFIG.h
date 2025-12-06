#ifndef PIN_H
#define PIN_H

// Display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADR 0x3C
#define OLED_RESET -1
#define OLED_SCL 22
#define OLED_SDA 21

// Buttons
#define BUTTON_UP 27    // Up
#define BUTTON_DOWN 26  // Down
#define BUTTON_OK 33    // OK
#define BUTTON_BACK 32  // Back

// SD-Card
#define SD_MOSI 15
#define SD_CLK 2
#define SD_MISO 0

// CC1101
#define CC1101_GDO0 4
#define CC1101_CS 5
#define CC1101_SCK 18
#define CC1101_MOSI 23
#define CC1101_MISO 19

// Infrared
#define IR_TRANSMITTER 12
#define IR_RECIVER 14

// GPIO
#define GPIO_A 35
#define GPIO_B 25
#define GPIO_C 17
#define GPIO_D 16
#define GPIO_E 13
#define GPIO_F 12

#endif
