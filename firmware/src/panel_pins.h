#pragma once
// Распиновка HUB75 — значения по умолчанию из ESP32-HUB75-MatrixPanel-DMA.
// Меняйте только при разводке своей платы: библиотека и большинство готовых
// плат (ESP32 Trinity) рассчитаны именно на эти пины.
//
// Пин E нужен только панелям 1/32 scan (64x64). Для 64x32 он не используется.
#define PIN_R1   25
#define PIN_G1   26
#define PIN_B1   27
#define PIN_R2   14
#define PIN_G2   12
#define PIN_B2   13
#define PIN_A    23
#define PIN_B    19
#define PIN_C     5
#define PIN_D    17
#define PIN_E    -1
#define PIN_LAT   4
#define PIN_OE   15
#define PIN_CLK  16
