// Сгенерировано tools/genfont.py — не редактировать вручную.
#pragma once
#include <stdint.h>

struct GfxGlyph {
  uint16_t off;      // смещение в bitmap
  uint8_t  w, h;     // размер в пикселях
  int8_t   dx, dy;   // сдвиг от курсора / базовой линии
  uint8_t  adv;      // шаг курсора
};

struct GfxFont {
  const uint8_t  *bitmap;
  const GfxGlyph *glyphs;
  const uint16_t *cps;   // кодовые точки, отсортированы
  uint16_t        count;
  uint8_t         yAdvance;  // шаг между строками
  uint8_t         baseline;  // от верха строки до базовой линии
};

extern const GfxFont FONT_F5;
extern const GfxFont FONT_R12;
extern const GfxFont FONT_B13;
extern const GfxFont FONT_B16;

// Поиск шрифта по идентификатору из JSON ("f5", "b16", ...).
// Возвращает nullptr, если имя неизвестно.
const GfxFont *fontByName(const char *name);
