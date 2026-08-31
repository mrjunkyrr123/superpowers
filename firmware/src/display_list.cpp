#include "display_list.h"
#include <string.h>

static const GfxFont &fontOr(const char *name, const GfxFont &fallback) {
  const GfxFont *f = fontByName(name);
  return f ? *f : fallback;
}

// Горизонтальное выравнивание внутри бокса [x, x+boxW). Если ширина бокса не
// задана, выравнивание идёт по всей панели — так удобнее центрировать.
static int16_t alignX(const char *a, int16_t x, int16_t boxW, int textW,
                      int16_t panelW) {
  if (!a) return x;
  if (a[0] == 'c') return boxW ? x + (boxW - textW) / 2 : (panelW - textW) / 2;
  if (a[0] == 'r') return boxW ? x + boxW - textW : panelW - textW;
  return x;
}

void renderPage(PixelSink &sink, JsonObjectConst page, const RenderCtx &ctx) {
  drawRect(sink, 0, 0, ctx.w, ctx.h, 0, true);   // чистим кадр

  for (JsonObjectConst L : page["layers"].as<JsonArrayConst>()) {
    const char *t = L["t"] | "";
    const uint16_t c = parseColor(L["c"] | "", 0xFFFF);
    const int16_t x = L["x"] | 0;
    const int16_t y = L["y"] | 0;

    if (!strcmp(t, "fill")) {
      drawRect(sink, 0, 0, ctx.w, ctx.h, c, true);

    } else if (!strcmp(t, "rect")) {
      drawRect(sink, x, y, L["w"] | 0, L["h"] | 0, c, L["fill"] | false);

    } else if (!strcmp(t, "line")) {
      drawLine(sink, x, y, L["x2"] | 0, L["y2"] | 0, c);

    } else if (!strcmp(t, "text") || !strcmp(t, "clock")) {
      const char *s = !strcmp(t, "clock") ? ctx.clockStr : (L["s"] | "");
      const GfxFont &f = fontOr(L["f"] | "f5", FONT_F5);
      const int16_t bx = alignX(L["a"] | "l", x, L["w"] | 0, textWidth(f, s), ctx.w);
      drawText(sink, bx, y, f, c, s);

    } else if (!strcmp(t, "scroll")) {
      const char *s = L["s"] | "";
      const GfxFont &f = fontOr(L["f"] | "f5", FONT_F5);
      const int16_t boxW = L["w"] | ctx.w;
      const int tw = textWidth(f, s);
      if (tw <= boxW) {
        // Влезает целиком — не дёргаем текст без нужды.
        drawText(sink, x, y, f, c, s, x, x + boxW);
      } else {
        const int gap = 8;                    // просвет между повторами
        const int period = tw + gap;
        const int speed = L["sp"] | 16;       // px/с
        // 64 бита: при неротируемой странице pageElapsedMs растёт без границы
        // и произведение на скорость переполняет uint32 через ~59 часов.
        const int off = static_cast<int>(
            ((static_cast<uint64_t>(ctx.pageElapsedMs) * speed) / 1000) % period);
        drawText(sink, x - off, y, f, c, s, x, x + boxW);
        drawText(sink, x - off + period, y, f, c, s, x, x + boxW);
      }

    } else if (!strcmp(t, "bar")) {
      const int16_t bw = L["w"] | ctx.w, bh = L["h"] | 3;
      drawRect(sink, x, y, bw, bh, parseColor(L["bg"] | "", 0x2104), true);
      float p = L["p"] | 0.0f;
      if (p > 0.0f) {
        if (p > 1.0f) p = 1.0f;
        int16_t filled = static_cast<int16_t>(bw * p + 0.5f);
        if (filled < 1) filled = 1;           // ненулевое значение всегда видно
        drawRect(sink, x, y, filled, bh, c, true);
      }
    }
    // Неизвестный примитив — пропускаем.
  }

  if (ctx.stale) sink.px(ctx.w - 1, 0, parseColor("#FF0000", 0xF800));
}

void renderStatus(PixelSink &sink, const RenderCtx &ctx, const char *line1,
                  const char *line2) {
  drawRect(sink, 0, 0, ctx.w, ctx.h, 0, true);
  const uint16_t grey = parseColor("#606060", 0x6B4D);
  const int16_t y1 = ctx.h / 2 - FONT_F5.yAdvance;
  drawText(sink, (ctx.w - textWidth(FONT_F5, line1)) / 2, y1, FONT_F5, grey, line1);
  if (line2 && *line2)
    drawText(sink, (ctx.w - textWidth(FONT_F5, line2)) / 2,
             y1 + FONT_F5.yAdvance, FONT_F5, grey, line2);
}
