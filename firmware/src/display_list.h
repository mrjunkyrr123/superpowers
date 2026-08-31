#pragma once
#include <ArduinoJson.h>
#include "gfx.h"

// Всё, что нужно для отрисовки одной страницы и чего нет в самом JSON.
struct RenderCtx {
  uint32_t pageElapsedMs = 0;   // с момента появления страницы — для скролла
  const char *clockStr = "";    // локальное время, примитив "clock"
  int16_t w = 64, h = 32;
  bool stale = false;           // данные устарели: рисуем метку в углу
};

// Рисует одну страницу display list. Неизвестные типы примитивов молча
// пропускаются — так старая прошивка не падает на новом поле с сервера.
void renderPage(PixelSink &sink, JsonObjectConst page, const RenderCtx &ctx);

// Экран-заглушка, когда нечего показывать (нет сети и нет кэша).
void renderStatus(PixelSink &sink, const RenderCtx &ctx, const char *line1,
                  const char *line2);
