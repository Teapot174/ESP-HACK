# 📡 ESP-HACK FW — [English](./README.md)

![ESP-HACK_LOGO](others/Pictures/ESP-HACK.png)

## 🚀 О проекте ESP-HACK FW

ESP-HACK — мощная универсальная прошивка для ESP32, собранная для исследований и пентестинга радиочастот, Bluetooth, инфракрасных сигналов и GPIO-интеграций.
Проект ориентирован на энтузиастов и пентестеров, желающих исследовать протоколы и устройства в суб-гигагерцовых диапазонах и в беспроводных технологиях.

> *Прошивка стабильна в рамках заявленного функционала, но некоторые фичи отмечены как “в разработке”. Используйте устройство согласно законам вашего региона.*

---

### ⚠️ Дисклеймер

Данная прошивка разработана исключительно для исследовательских целей и тестирования оборудования.
Используя прошивку вы обязаны соблюдать законодательство своего региона. Создатель прошивки не несет ответственность за ваши действия. Глушилки — НЕЛЕГАЛЬНЫ.

---

## ⚡ Возможности

### WiFi

- Deauther *(работает, но плохо)*  
- Beacon Spam  
- EvilPortal  
- Wardriving

### Bluetooth

- BLE-Spam:
IOS, Android, Windows
- BadBLE

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
- Jammer (ILLIGAL)
- Spectrum
- Config

**Serial *(in development)***

### Settings
- Цвет дисплея
- Время режима ожидания
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

| Module | Pin | Pin | Pin | Pin | Pin | Pin | Pin |
|--------|-------|-------|-------|-------|-------|-------|-------|
| **📺 Дисплей** | VCC → 3V3 | GND → GND | SCL → G22 | SDA → G21 | - | - | - |
| **🔘 Кнопки** | UP → G27 | DOWN → G26 | OK → G33 | BACK → G32 | - | - | - |
| **📡 CC1101** | 1 → GND | 2 → 3V3 | 3 → G4 | 4 → G15 | 5 → G18 | 6 → G23 | 7 → G19 |
| **💡 ИК** | IR-T → G12 | IR-R → G14 | - | - | - | - | - |
| **🔌 GPIO** | A → G2 | B → G25 | C → G17 | D → G16 | E → G13 | F → G12 | - |
| **💾 SD Card** | 3v3 → 3v3 | CS → G5 | MOSI → G23 | CLK → G18 | MISO → G19 | GND → GND | - |

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
