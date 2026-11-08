#pragma once
#include <stddef.h>
#include <stdint.h>
#include "fonts.h"

// Приёмник пикселей. Абстракция нужна затем, чтобы отрисовку можно было
// прогнать на хосте без панели (см. test/), а не только на железе.
struct PixelSink {
  virtual void px(int16_t x, int16_t y, uint16_t color) = 0;
  // Горизонтальный отрезок [x, x+w). По умолчанию — по точке; приёмник с
  // кадровым буфером переопределяет его и заливает строку целиком: на экране
  // 480x480 одна очистка кадра — это 230 тысяч вызовов px().
  virtual void hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
    for (int16_t i = 0; i < w; ++i) px(x + i, y, color);
  }
  // Цвет уже нарисованной точки — для сглаженного шрифта (смешивание с фоном).
  // Приёмник без памяти кадра отвечает «чёрный».
  virtual uint16_t get(int16_t, int16_t) { return 0; }
  virtual ~PixelSink() {}
};

// Смесь двух цветов RGB565: a — доля fg из 15. Целочисленно и без отрицательных
// чисел — тот же результат даёт mock-server/render.py.
uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a);

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

// Строка, ужатая до ширины maxW: не влезла — обрезается по знакам и кончается «…».
// Режет по ширине, а не по числу знаков: сервер ширину текста не знает.
// out — буфер под результат; возвращает его ширину в пикселях.
int fitText(const GfxFont &f, const char *s, int maxW, char *out, size_t outSize);

void drawRect(PixelSink &sink, int16_t x, int16_t y, int16_t w, int16_t h,
              uint16_t color, bool filled);

// Скруглённый прямоугольник — основа кнопок на сенсорном экране.
// Радиус задаётся целым числом, смещение считается целочисленным корнем:
// это гарантирует, что прошивка и серверное превью дадут один и тот же растр.
void fillRoundRect(PixelSink &sink, int16_t x, int16_t y, int16_t w, int16_t h,
                   int16_t r, uint16_t color);
// Рамка толщиной t внутри скруглённого прямоугольника: внутренний контур —
// тот же прямоугольник, сжатый на t, с радиусом r - t.
void strokeRoundRect(PixelSink &sink, int16_t x, int16_t y, int16_t w, int16_t h,
                     int16_t r, int16_t t, uint16_t color);
void drawLine(PixelSink &sink, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
              uint16_t color);

// "#RRGGBB" -> RGB565. При разборе мусора возвращает fallback.
uint16_t parseColor(const char *s, uint16_t fallback);
