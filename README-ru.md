# 📡 ESP-HACK FW — [English](./README.md)

![ESP-HACK_LOGO](others/Pictures/ESP-HACK.png)

## 🚀 О проекте ESP-HACK FW

ESP-HACK — мощная универсальная прошивка для ESP32, собранная для исследований и пентестинга радиочастот, Bluetooth, инфракрасных сигналов и GPIO-интеграций.
Проект ориентирован на энтузиастов и пентестеров, желающих исследовать протоколы и устройства в суб-гигагерцовых диапазонах и в беспроводных технологиях.

[Больше информации на нашем вики :)](https://teapot174.github.io/)

> *Прошивка стабильна в рамках заявленного функционала, но некоторые фичи отмечены как “в разработке”. Используйте устройство согласно законам вашего региона.*

---

### ⚠️ Дисклеймер

Данная прошивка разработана исключительно для исследовательских целей и тестирования оборудования.
Используя прошивку вы обязаны соблюдать законодательство своего региона. Создатель прошивки не несет ответственность за ваши действия. Глушилки — НЕЛЕГАЛЬНЫ.

---

## ⚡ Возможности

### WiFi

- Deauther *(анворк, лол)*  
- Beacon Spam  
- EvilPortal  
- Wardriving
- Packets

### Bluetooth

- BLE-Spam:
IOS, Android, Windows
- BadBLE
- Mouse

### SubGHz

- Read  
- Send
- RAW (Запись/Отправка)
- Analyzer
- Bruteforce:
Came, Nice, Ansonic, Holtek, Chamberlain
- Jammer (ILLEGAL)

### Infrared

- IR-Send
- IR-Read  
- TV, PJ, AC OFF

### GPIO

**iButton**
- Read
- Write
- Config

**NRF24**
- Jammer (ILLEGAL)
- Spectrum
- Config

**PN532 *(in development)***

### Games
- Snake
- Flappy Bird
- Ping Pong
- Tetris

### Settings
- Цвет дисплея
- Режим ожидания
- Перезагрузка
- Сброс
- О прошивке

---

### 📡 Поддерживаемые модуляции SubGHz
(315MHz/433.92MHz/868Mhz/915Mhz)
- Princeton  
- RcSwitch  
- Came
- Nice 
- Holtec
- Ansonic
- Chamberlain
- StarLine  
- KeeLoq
- Любой другой протокол с RAW режимом

---

## 🛠️ Сборка

### 🔧 PCB

![ESP-HACK_PCB](others/Pictures/PCB.png)
Thanks Dripside!

### 🔧 Необходимые компоненты

| Компонент | Ссылка |
|-----------|--------|
| ESP32-WROOM | [TAP](https://aliexpress.ru/item/1005007817121199.html) |
| CC1101 | [TAP](https://aliexpress.ru/item/1005008544032996.html) |
| Display SH1106 | [TAP](https://aliexpress.ru/item/1005004464878029.html) |
| SD Модуль | [TAP](https://aliexpress.ru/item/32674518514.html) |
| Кнопки | [TAP](https://aliexpress.ru/item/4000452176168.html) |
| IR-TX, IR-RX | [TAP](https://aliexpress.ru/item/1005007446501425.html) |

---

### 🔌 Схема подключения
![ESP-HACK_Scheme](others/Pictures/Scheme.png)

| Модуль | Пин | Пин | Пин | Пин | Пин | Пин | Пин |
|--------|-------|-------|-------|-------|-------|-------|-------|
| **📺 Дисплей** | VCC → 3V3 | GND → GND | SCL → G22 | SDA → G21 | - | - | - |
| **🔘 Кнопки** | UP → G27 | DOWN → G26 | OK → G33 | BACK → G32 | - | - | - |
| **📡 CC1101** | 1 → GND | 2 → 3V3 | 3 → G4 | 4 → G5 | 5 → G18 | 6 → G23 | 7 → G19 |
| **💡 ИК** | IR-TX → G16 | IR-RX → G35 | - | - | - | - | - |
| **🔌 GPIO** | A → G16 | B → G2 | C → G18 | D → G23 | E → G19 | F → G25 | - |
| **💾 SD Card** | 3v3 → 3v3 | CS → G15 | MOSI → G13 | CLK → G14 | MISO → G17 | GND → GND | - |

---

## Ошибки (ERROR:)

В процессе работы ESP-HACK могут возникать следующие ошибки:

| Код ошибки | ❌ Описание ошибки | 🛠️ Возможное решение |
|------------|-----------|------------------|
| **0x000**  | Ошибка инициализации **SD-Карты**         | 🛠️ Отформатируйте SD-карту в **FAT32** либо замените её.                         |
| **0x001**  | Ошибка инициализации **CC1101**           | 🛠️ Проверьте подключение и работоспособность модуля.                             |
| **0x002**  | Ошибка инициализации **NRF24**            | 🛠️ Проверьте правильность выбора пинов, соединений и перезагрузите устройство.   |

---

## 📸 Финальный результат

![ESP-HACK_Device](others/Pictures/Device.jpg)

---

## ✉️ Контакты / Автор

Автор: Teapot174  
Telegram: `@ESPH4CK`
