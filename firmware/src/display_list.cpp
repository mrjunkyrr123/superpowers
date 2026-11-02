#include "display_list.h"
#include <string.h>

// С этой ширины экран считается «большим» (панель 480x480): служебные
// надписи крупнее. Табло HUB75 64x32 рисуется как раньше.
static const int16_t BIG_SCREEN_W = 128;

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

// Значок Wi-Fi как в телефоне: точка и три дуги веером вверх, 21x15. level — сколько
// дуг горит (0 — только точка); -1 — связи нет, всё тусклое. Тот же расчёт —
// в mock-server/render.py, чтобы превью совпадало с платой.
static void drawWifi(PixelSink &sink, int16_t x, int16_t y, int8_t level, uint16_t on,
                     uint16_t off) {
  const int cx = x + 10, cy = y + 14;
  for (int py = y; py < y + 15; ++py) {
    for (int px = x; px < x + 21; ++px) {
      const int dx = px - cx, dy = cy - py;
      const int r2 = dx * dx + dy * dy;
      int ring;
      if (r2 <= 3) ring = 0;                                  // точка
      else if (dx > dy || -dx > dy) continue;                 // вне веера 90°
      else if (r2 >= 16 && r2 <= 30) ring = 1;
      else if (r2 >= 56 && r2 <= 90) ring = 2;
      else if (r2 >= 121 && r2 <= 182) ring = 3;
      else continue;
      sink.px(px, py, level >= ring ? on : off);
    }
  }
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

    } else if (!strcmp(t, "button")) {
      const int16_t bw = L["w"] | 0, bh = L["h"] | 0;
      const char *label = L["s"] | "";
      const GfxFont &f = fontOr(L["f"] | "b24", FONT_B24);
      uint16_t bg = parseColor(L["bg"] | "", 0x2124);
      uint16_t fg = c;
      // Нажатая кнопка рисуется инверсно: это единственная обратная связь,
      // которую устройство даёт само, не дожидаясь ответа сервера.
      const char *id = L["id"] | "";
      if (ctx.pressedId && *id && !strcmp(ctx.pressedId, id)) {
        const uint16_t tmp = bg; bg = fg; fg = tmp;
      }
      fillRoundRect(sink, x, y, bw, bh, L["r"] | 8, bg);
      drawText(sink, x + (bw - textWidth(f, label)) / 2,
               y + (bh - f.yAdvance) / 2, f, fg, label);

    } else if (!strcmp(t, "wifi")) {
      // Силу сигнала знает только устройство — сервер даёт лишь место и цвета.
      drawWifi(sink, x, y, ctx.wifi, c, parseColor(L["bg"] | "", 0x4A49));

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

  if (!ctx.stale) return;
  const uint16_t red = parseColor("#FF0000", 0xF800);
  if (ctx.w < BIG_SCREEN_W) {
    sink.px(ctx.w - 1, 0, red);           // на табло 64x32 места нет — одна точка
    return;
  }
  // На большом экране — читаемая плашка в левом нижнем углу, на чёрной
  // подложке, чтобы не сливалась с вёрсткой под ней.
  const char *label = "НЕТ СВЯЗИ";
  const GfxFont &f = FONT_R12;
  const int16_t pad = 6, tw = textWidth(f, label);
  const int16_t bh = f.yAdvance + 2 * pad, by = ctx.h - bh - 4;
  fillRoundRect(sink, 4, by, tw + 2 * pad, bh, 4, 0);
  drawText(sink, 4 + pad, by + pad, f, red, label);
}

bool hitTest(JsonObjectConst page, int16_t x, int16_t y, ButtonHit &out) {
  for (JsonObjectConst L : page["layers"].as<JsonArrayConst>()) {
    if (strcmp(L["t"] | "", "button")) continue;
    const int16_t bx = L["x"] | 0, by = L["y"] | 0;
    const int16_t bw = L["w"] | 0, bh = L["h"] | 0;
    if (x < bx || y < by || x >= bx + bw || y >= by + bh) continue;

    JsonObjectConst a = L["action"];
    out.id = L["id"] | "";
    out.method = a["method"] | "POST";
    // Не `| nullptr`: в ArduinoJson 7 это всегда даёт nullptr, и ни одна
    // кнопка не нажимается. as<const char*>() — строка или nullptr.
    out.path = a["path"].as<const char *>();
    return out.path != nullptr;   // кнопка без пути ничего не делает
  }
  return false;
}

void renderStatus(PixelSink &sink, const RenderCtx &ctx, const char *line1,
                  const char *line2) {
  drawRect(sink, 0, 0, ctx.w, ctx.h, 0, true);
  const uint16_t grey = parseColor("#606060", 0x6B4D);
  // 5 px читается на табло 64x32, а на 480x480 — уже нет.
  const bool big = ctx.w >= BIG_SCREEN_W;
  const GfxFont &f1 = big ? FONT_B24 : FONT_F5;
  const GfxFont &f2 = big ? FONT_R20 : FONT_F5;
  const int16_t y1 = ctx.h / 2 - f1.yAdvance;
  drawText(sink, (ctx.w - textWidth(f1, line1)) / 2, y1, f1, grey, line1);
  if (line2 && *line2)
    drawText(sink, (ctx.w - textWidth(f2, line2)) / 2,
             y1 + f1.yAdvance + (big ? 8 : 0), f2, grey, line2);
}
