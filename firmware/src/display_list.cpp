#include "display_list.h"
#include <stdio.h>
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

// Сигнал полосками по макету 08.11: четыре столбика 3 px, высоты 4/8/12/16, шаг 5,
// низ на y+16. Горят level+1 (уровень −1..3 даёт 0..4 столбика).
static void drawWifiBars(PixelSink &sink, int16_t x, int16_t y, int8_t level, uint16_t on,
                         uint16_t off) {
  for (int i = 0; i < 4; ++i) {
    const int16_t h = 4 * (i + 1);
    drawRect(sink, x + 5 * i, y + 16 - h, 3, h, i <= level ? on : off, true);
  }
}

// Цвета макета 08.11 для того, что плата рисует сама.
static const uint16_t C_TEXT2 = 0xA534;   // #A5A6A5
static const uint16_t C_TEXT3 = 0x6B4D;   // #6B696B
static const uint16_t C_LINE  = 0x2945;   // #292829
static const uint16_t C_SURF2 = 0x2104;   // #212021
static const uint16_t C_INK   = 0x1082;   // #101010
static const uint16_t C_WARN  = 0xF562;   // #F7AE10

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
      const int16_t r = L["r"] | 0;
      if (r > 0 && (L["fill"] | false)) fillRoundRect(sink, x, y, L["w"] | 0, L["h"] | 0, r, c);
      else drawRect(sink, x, y, L["w"] | 0, L["h"] | 0, c, L["fill"] | false);

    } else if (!strcmp(t, "line")) {
      drawLine(sink, x, y, L["x2"] | 0, L["y2"] | 0, c);

    } else if (!strcmp(t, "text") || !strcmp(t, "clock")) {
      const char *s = !strcmp(t, "clock") ? ctx.clockStr : (L["s"] | "");
      const GfxFont &f = fontOr(L["f"] | "f5", FONT_F5);
      const int16_t boxW = L["w"] | 0;
      // "el": не влезло в w — обрезать по ширине с «…».
      char fit[256];
      int tw;
      if ((L["el"] | false) && boxW > 0) {
        tw = fitText(f, s, boxW, fit, sizeof(fit));
        s = fit;
      } else {
        tw = textWidth(f, s);
      }
      const int16_t bx = alignX(L["a"] | "l", x, boxW, tw, ctx.w);
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
      const int16_t r = L["r"] | 8;
      // Нажатая кнопка — единственная обратная связь, которую устройство даёт само,
      // не дожидаясь ответа сервера: инверсия или, с "pr":"frame", рамка 2 px цвета "pc".
      const char *id = L["id"] | "";
      const bool pressed = ctx.pressedId && *id && !strcmp(ctx.pressedId, id);
      const bool frame = !strcmp(L["pr"] | "", "frame");
      if (pressed && !frame) {
        const uint16_t tmp = bg; bg = fg; fg = tmp;
      }
      fillRoundRect(sink, x, y, bw, bh, r, bg);
      drawText(sink, x + (bw - textWidth(f, label)) / 2,
               y + (bh - f.yAdvance) / 2, f, fg, label);
      if (pressed && frame)
        strokeRoundRect(sink, x, y, bw, bh, r, 2, parseColor(L["pc"] | "", 0xFFFF));

    } else if (!strcmp(t, "zone")) {
      // Невидимая зона касания поверх плитки, нарисованной слоями под ней.
      // Нажата — рамка 2 px по краю плитки.
      const char *id = L["id"] | "";
      if (ctx.pressedId && *id && !strcmp(ctx.pressedId, id))
        strokeRoundRect(sink, x, y, L["w"] | 0, L["h"] | 0, L["r"] | 12, 2,
                        parseColor(L["pc"] | "", 0xFFFF));

    } else if (!strcmp(t, "wifi")) {
      // Силу сигнала знает только устройство — сервер даёт лишь место и цвета.
      const uint16_t off = parseColor(L["bg"] | "", 0x4A49);
      if (!strcmp(L["st"] | "", "bars")) drawWifiBars(sink, x, y, ctx.wifi, c, off);
      else drawWifi(sink, x, y, ctx.wifi, c, off);

    } else if (!strcmp(t, "bar")) {
      const int16_t bw = L["w"] | ctx.w, bh = L["h"] | 3, r = L["r"] | 0;
      fillRoundRect(sink, x, y, bw, bh, r, parseColor(L["bg"] | "", 0x2104));
      float p = L["p"] | 0.0f;
      if (p > 0.0f) {
        if (p > 1.0f) p = 1.0f;
        int16_t filled = static_cast<int16_t>(bw * p + 0.5f);
        if (filled < 1) filled = 1;           // ненулевое значение всегда видно
        if (r > 0 && filled < 2 * r) filled = 2 * r;   // скруглённая — хотя бы кружок
        fillRoundRect(sink, x, y, filled, bh, r, c);
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
  // На большом экране — жёлтая плашка поверх шапки (макет 08.11): «Нет связи ·
  // данные от 13:58» и часы. Шапку она закрывает целиком — экран без связи сразу видно.
  char label[48];
  if (ctx.staleSince && *ctx.staleSince)
    snprintf(label, sizeof(label), "Нет связи \xC2\xB7 данные от %s", ctx.staleSince);
  else
    snprintf(label, sizeof(label), "Нет связи");
  const GfxFont &f = FONT_B16;
  drawRect(sink, 0, 0, ctx.w, 48, C_WARN, true);
  drawText(sink, 16, 15, f, C_INK, label);
  drawText(sink, ctx.w - 16 - textWidth(f, ctx.clockStr), 15, f, C_INK, ctx.clockStr);
}

bool hitTest(JsonObjectConst page, int16_t x, int16_t y, ButtonHit &out) {
  // Берётся последняя кнопка или зона под пальцем, у которой есть путь: зона лежит
  // поверх плитки, а кнопка без пути — просто плашка и касание не перехватывает.
  bool hit = false;
  for (JsonObjectConst L : page["layers"].as<JsonArrayConst>()) {
    if (strcmp(L["t"] | "", "button") && strcmp(L["t"] | "", "zone")) continue;
    const int16_t bx = L["x"] | 0, by = L["y"] | 0;
    const int16_t bw = L["w"] | 0, bh = L["h"] | 0;
    if (x < bx || y < by || x >= bx + bw || y >= by + bh) continue;

    JsonObjectConst a = L["action"];
    // Не `| nullptr`: в ArduinoJson 7 это всегда даёт nullptr, и ни одна
    // кнопка не нажимается. as<const char*>() — строка или nullptr.
    const char *path = a["path"].as<const char *>();
    if (!path) continue;
    out.id = L["id"] | "";
    out.method = a["method"] | "POST";
    out.path = path;
    hit = true;
  }
  return hit;
}

void renderStatusScreen(PixelSink &sink, const RenderCtx &ctx, const StatusScreen &s) {
  drawRect(sink, 0, 0, ctx.w, ctx.h, 0, true);
  // Шапка без названия: Wi-Fi, часы, линия.
  drawWifiBars(sink, 386, 17, ctx.wifi, C_TEXT2, C_LINE);
  drawText(sink, 464 - textWidth(FONT_B16, ctx.clockStr), 15, FONT_B16, C_TEXT2, ctx.clockStr);
  drawLine(sink, 16, 47, 463, 47, C_LINE);
  // Значок в круге 64, заголовок b28, две строки m16.
  fillRoundRect(sink, 208, 150, 64, 64, 32, C_SURF2);
  drawText(sink, 208 + (64 - textWidth(FONT_B28, s.icon)) / 2, 168, FONT_B28, s.iconColor, s.icon);
  drawText(sink, (ctx.w - textWidth(FONT_B28, s.title)) / 2, 236, FONT_B28, 0xFFFF, s.title);
  drawText(sink, (ctx.w - textWidth(FONT_M16, s.line1)) / 2, 280, FONT_M16, C_TEXT2, s.line1);
  drawText(sink, (ctx.w - textWidth(FONT_M16, s.line2)) / 2, 302, FONT_M16, C_TEXT3, s.line2);
  if (!s.button) return;
  const bool pressed = ctx.pressedId && !strcmp(ctx.pressedId, "status-btn");
  fillRoundRect(sink, STATUS_BTN_X, STATUS_BTN_Y, STATUS_BTN_W, STATUS_BTN_H, 12,
                pressed ? 0xFFFF : C_SURF2);
  drawText(sink, STATUS_BTN_X + (STATUS_BTN_W - textWidth(FONT_B16, s.button)) / 2,
           STATUS_BTN_Y + (STATUS_BTN_H - FONT_B16.yAdvance) / 2, FONT_B16,
           pressed ? C_INK : 0xFFFF, s.button);
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
