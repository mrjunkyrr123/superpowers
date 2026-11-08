#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a) {
  if (a >= 15) return fg;
  if (a == 0) return bg;
  const uint16_t na = 15 - a;
  const uint16_t r = ((fg >> 11) * a + (bg >> 11) * na) / 15;
  const uint16_t g = (((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * na) / 15;
  const uint16_t b = ((fg & 0x1F) * a + (bg & 0x1F) * na) / 15;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// Яркость точки глифа: 0..15. Растр — строки подряд без выравнивания:
// 1 бит — MSB-first, 4 бита — старший полубайт первым.
static inline uint8_t glyphAlpha(const GfxFont &f, const GfxGlyph *g, uint32_t i) {
  if (f.bpp == 4) {
    const uint8_t b = f.bitmap[g->off + i / 2];
    return (i & 1) ? (b & 0x0F) : (b >> 4);
  }
  return (f.bitmap[g->off + i / 8] & (0x80 >> (i % 8))) ? 15 : 0;
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
    // Подряд идущие полные точки строки отдаются одним отрезком; полупрозрачные
    // края сглаженного шрифта смешиваются с тем, что уже нарисовано под ними.
    for (uint8_t ry = 0; ry < g->h; ++ry) {
      const int16_t py = base + g->dy + ry;
      int16_t runStart = -1;
      for (int16_t rx = 0; rx <= g->w; ++rx) {
        uint8_t a = 0;
        const int16_t px = cur + g->dx + rx;
        if (rx < g->w && px >= clipX0 && px < clipX1)
          a = glyphAlpha(f, g, static_cast<uint32_t>(ry) * g->w + rx);
        if (a == 15) {
          if (runStart < 0) runStart = rx;
          continue;
        }
        if (runStart >= 0) {
          sink.hline(cur + g->dx + runStart, py, rx - runStart, color);
          runStart = -1;
        }
        if (a) sink.px(px, py, blend565(sink.get(px, py), color, a));
      }
    }
    cur += g->adv;
  }
}

int fitText(const GfxFont &f, const char *s, int maxW, char *out, size_t outSize) {
  if (!outSize) return 0;
  out[0] = '\0';
  if (!s) return 0;
  const int full = textWidth(f, s);
  if (full <= maxW) {
    snprintf(out, outSize, "%s", s);   // strlcpy нет в хостовой сборке (MinGW)
    return full;
  }
  // В старых шрифтах (DejaVu) знака «…» нет — там три точки.
  const char *ell = findGlyph(f, 0x2026) ? "\xE2\x80\xA6" : "...";
  const int ew = textWidth(f, ell);
  int w = 0;
  const char *p = s, *cut = s;
  int cutW = 0;
  while (*p) {
    const char *q = p;
    const uint32_t cp = utf8Next(q);
    const GfxGlyph *g = glyphOrFallback(f, cp);
    const int adv = g ? g->adv : 0;
    if (w + adv + ew > maxW) break;
    w += adv;
    p = q;
    if (cp != ' ') { cut = p; cutW = w; }   // пробел перед «…» не оставляем
  }
  size_t n = static_cast<size_t>(cut - s);
  const size_t el = strlen(ell);
  if (n + el + 1 > outSize) n = outSize > el + 1 ? outSize - el - 1 : 0;
  memcpy(out, s, n);
  memcpy(out + n, ell, el);
  out[n + el] = '\0';
  return cutW + ew;
}

void drawRect(PixelSink &sink, int16_t x, int16_t y, int16_t w, int16_t h,
              uint16_t color, bool filled) {
  if (w <= 0 || h <= 0) return;
  if (filled) {
    for (int16_t yy = y; yy < y + h; ++yy) sink.hline(x, yy, w, color);
    return;
  }
  sink.hline(x, y, w, color);
  sink.hline(x, y + h - 1, w, color);
  for (int16_t yy = y; yy < y + h; ++yy) {
    sink.px(x, yy, color);
    sink.px(x + w - 1, yy, color);
  }
}

// Целочисленный квадратный корень с округлением вниз. Намеренно не sqrtf():
// плавающая точка дала бы расхождение с эталонным рендерером на Python.
static uint32_t isqrt32(uint32_t n) {
  uint32_t x = 0;
  while ((x + 1) * (x + 1) <= n) ++x;
  return x;
}

static int16_t roundRectInset(int16_t row, int16_t h, int16_t r) {
  if (r <= 0) return 0;
  int16_t dy;
  if (row < r) dy = r - 1 - row;
  else if (row >= h - r) dy = row - (h - r);
  else return 0;
  return r - static_cast<int16_t>(isqrt32(static_cast<uint32_t>(r) * r -
                                          static_cast<uint32_t>(dy) * dy));
}

void fillRoundRect(PixelSink &sink, int16_t x, int16_t y, int16_t w, int16_t h,
                   int16_t r, uint16_t color) {
  if (w <= 0 || h <= 0) return;
  const int16_t maxR = (w < h ? w : h) / 2;
  if (r > maxR) r = maxR;
  for (int16_t row = 0; row < h; ++row) {
    const int16_t inset = roundRectInset(row, h, r);
    sink.hline(x + inset, y + row, w - 2 * inset, color);
  }
}

void strokeRoundRect(PixelSink &sink, int16_t x, int16_t y, int16_t w, int16_t h,
                     int16_t r, int16_t t, uint16_t color) {
  if (w <= 0 || h <= 0 || t <= 0) return;
  if (2 * t >= w || 2 * t >= h) {        // рамка толще половины — это заливка
    fillRoundRect(sink, x, y, w, h, r, color);
    return;
  }
  const int16_t maxR = (w < h ? w : h) / 2;
  if (r > maxR) r = maxR;
  const int16_t iw = w - 2 * t, ih = h - 2 * t;
  const int16_t imaxR = (iw < ih ? iw : ih) / 2;
  int16_t ri = r - t;
  if (ri < 0) ri = 0;
  if (ri > imaxR) ri = imaxR;
  for (int16_t row = 0; row < h; ++row) {
    const int16_t oi = roundRectInset(row, h, r);
    if (row < t || row >= h - t) {
      sink.hline(x + oi, y + row, w - 2 * oi, color);
      continue;
    }
    const int16_t ii = t + roundRectInset(row - t, ih, ri);
    sink.hline(x + oi, y + row, ii - oi, color);
    sink.hline(x + w - ii, y + row, ii - oi, color);
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
