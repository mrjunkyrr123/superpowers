#pragma once
#include <stdint.h>

// Железо Guition 4848S040: экран, подсветка, сенсор. Всё, что зависит от
// платы, живёт здесь, чтобы main.cpp оставался про логику.

static const int16_t SCREEN_W = 480, SCREEN_H = 480;

// Поднимает экран и сенсор. false — экран не поднялся (обычно нет PSRAM).
bool boardBegin();

// Подсветка 0..100 %.
void backlight(int percent);

// Плавно меняет подсветку за ms. Делает таймер, не процессор: вызов сразу возвращается.
void backlightFade(int fromPercent, int toPercent, int ms);

// Выводит кадр 480x480 RGB565 целиком.
void present(const uint16_t *frame);

// Выводит только строки [y0, y1) того же кадра.
void presentRows(const uint16_t *frame, int16_t y0, int16_t y1);

enum class Touch {
  NoChange,   // у сенсора нет новых данных
  Down,       // палец на экране, x/y заполнены
  Up,         // палец убран
};

// Опрашивает GT911. INT у платы не разведён, поэтому только по таймеру.
Touch touchRead(int16_t &x, int16_t &y);
