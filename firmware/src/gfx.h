#pragma once
#include <stdint.h>
#include "fonts.h"

// Приёмник пикселей. Абстракция нужна затем, чтобы отрисовку можно было
// прогнать на хосте без панели (см. test/), а не только на железе.
struct PixelSink {
  virtual void px(int16_t x, int16_t y, uint16_t color) = 0;
  virtual ~PixelSink() {}
};

// Разбор UTF-8. Возвращает кодовую точку и сдвигает p. Битые последовательности
// возвращают 0xFFFD и съедают один байт — так строка не «съезжает» целиком
// из-за одного повреждённого байта.
uint32_t utf8Next(const char *&p);

const GfxGlyph *findGlyph(const GfxFont &f, uint32_t cp);

int  textWidth(const GfxFont &f, const char *s);

// yTop — верх строки, не базовая линия: так проще верстать на 32 px по высоте.
// clipX0/clipX1 ограничивают вывод по горизонтали (для бегущей строки).
void drawText(PixelSink &sink, int16_t x, int16_t yTop, const GfxFont &f,
              uint16_t color, const char *s,
              int16_t clipX0 = INT16_MIN, int16_t clipX1 = INT16_MAX);

void drawRect(PixelSink &sink, int16_t x, int16_t y, int16_t w, int16_t h,
              uint16_t color, bool filled);
void drawLine(PixelSink &sink, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
              uint16_t color);

// "#RRGGBB" -> RGB565. При разборе мусора возвращает fallback.
uint16_t parseColor(const char *s, uint16_t fallback);
