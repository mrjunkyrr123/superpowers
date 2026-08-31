#include "gfx.h"
#include <stdlib.h>

uint32_t utf8Next(const char *&p) {
  const uint8_t c = static_cast<uint8_t>(*p);
  if (c == 0) return 0;
  int extra;
  uint32_t cp;
  if (c < 0x80)        { ++p; return c; }
  else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
  else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
  else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
  else { ++p; return 0xFFFD; }

  for (int i = 0; i < extra; ++i) {
    const uint8_t n = static_cast<uint8_t>(p[1 + i]);
    if ((n & 0xC0) != 0x80) { ++p; return 0xFFFD; }  // обрыв последовательности
    cp = (cp << 6) | (n & 0x3F);
  }
  p += extra + 1;
  return cp;
}

const GfxGlyph *findGlyph(const GfxFont &f, uint32_t cp) {
  if (cp > 0xFFFF) return nullptr;
  uint16_t lo = 0, hi = f.count;           // кодовые точки отсортированы
  while (lo < hi) {
    const uint16_t mid = lo + (hi - lo) / 2;
    if (f.cps[mid] < cp) lo = mid + 1;
    else hi = mid;
  }
  if (lo < f.count && f.cps[lo] == cp) return &f.glyphs[lo];
  return nullptr;
}

static const GfxGlyph *glyphOrFallback(const GfxFont &f, uint32_t cp) {
  if (const GfxGlyph *g = findGlyph(f, cp)) return g;
  return findGlyph(f, '?');
}

int textWidth(const GfxFont &f, const char *s) {
  if (!s) return 0;
  int w = 0;
  while (*s) {
    const uint32_t cp = utf8Next(s);
    if (const GfxGlyph *g = glyphOrFallback(f, cp)) w += g->adv;
  }
  return w;
}

void drawText(PixelSink &sink, int16_t x, int16_t yTop, const GfxFont &f,
              uint16_t color, const char *s, int16_t clipX0, int16_t clipX1) {
  if (!s) return;
  const int16_t base = yTop + f.baseline;
  int16_t cur = x;
  while (*s) {
    const uint32_t cp = utf8Next(s);
    const GfxGlyph *g = glyphOrFallback(f, cp);
    if (!g) continue;
    // Растр упакован MSB-first, строки идут подряд без выравнивания.
    for (uint8_t ry = 0; ry < g->h; ++ry) {
      for (uint8_t rx = 0; rx < g->w; ++rx) {
        const uint32_t bit = static_cast<uint32_t>(ry) * g->w + rx;
        if (!(f.bitmap[g->off + bit / 8] & (0x80 >> (bit % 8)))) continue;
        const int16_t px = cur + g->dx + rx;
        if (px < clipX0 || px >= clipX1) continue;
        sink.px(px, base + g->dy + ry, color);
      }
    }
    cur += g->adv;
  }
}

void drawRect(PixelSink &sink, int16_t x, int16_t y, int16_t w, int16_t h,
              uint16_t color, bool filled) {
  if (w <= 0 || h <= 0) return;
  if (filled) {
    for (int16_t yy = y; yy < y + h; ++yy)
      for (int16_t xx = x; xx < x + w; ++xx) sink.px(xx, yy, color);
    return;
  }
  for (int16_t xx = x; xx < x + w; ++xx) {
    sink.px(xx, y, color);
    sink.px(xx, y + h - 1, color);
  }
  for (int16_t yy = y; yy < y + h; ++yy) {
    sink.px(x, yy, color);
    sink.px(x + w - 1, yy, color);
  }
}

void drawLine(PixelSink &sink, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
              uint16_t color) {
  const int16_t dx = abs(x1 - x0), dy = -abs(y1 - y0);
  const int16_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int32_t err = dx + dy;
  for (;;) {
    sink.px(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    const int32_t e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

uint16_t parseColor(const char *s, uint16_t fallback) {
  if (!s || s[0] != '#') return fallback;
  int v[6];
  for (int i = 0; i < 6; ++i) {
    v[i] = hexVal(s[1 + i]);
    if (v[i] < 0) return fallback;
  }
  if (s[7] != '\0') return fallback;
  const uint8_t r = v[0] * 16 + v[1], g = v[2] * 16 + v[3], b = v[4] * 16 + v[5];
  return static_cast<uint16_t>((r & 0xF8) << 8) |
         static_cast<uint16_t>((g & 0xFC) << 3) |
         static_cast<uint16_t>(b >> 3);
}
