#pragma once
#include <ArduinoJson.h>
#include "gfx.h"

// Всё, что нужно для отрисовки одной страницы и чего нет в самом JSON.
struct RenderCtx {
  uint32_t pageElapsedMs = 0;   // с момента появления страницы — для скролла
  const char *clockStr = "";    // локальное время, примитив "clock"
  int16_t w = 64, h = 32;
  bool stale = false;           // данные устарели: рисуем метку в углу
  const char *pressedId = nullptr;  // кнопка под пальцем — рисуется инверсно (или рамкой)
  int8_t wifi = -1;             // примитив "wifi": дуг 0..3, -1 — нет связи
  const char *staleSince = "";  // время последних данных «ЧЧ:ММ» — в плашке «Нет связи»
};

// Рисует одну страницу display list. Неизвестные типы примитивов молча
// пропускаются — так старая прошивка не падает на новом поле с сервера.
void renderPage(PixelSink &sink, JsonObjectConst page, const RenderCtx &ctx);

// Кнопка, в которую попало касание. Поля указывают внутрь JsonDocument,
// поэтому действительны, пока он жив.
struct ButtonHit {
  const char *id = nullptr;
  const char *method = nullptr;
  const char *path = nullptr;   // путь относительно базового адреса из настроек
};

// Ищет кнопку под точкой касания. Возвращает false, если попадания нет.
//
// Действие описано ПУТЁМ, а не полным адресом, и это намеренно: устройство
// подставляет базовый адрес из своих настроек и physически не может постучаться
// на произвольный хост, даже если сервер пришлёт что-то неожиданное.
bool hitTest(JsonObjectConst page, int16_t x, int16_t y, ButtonHit &out);

// Экран-заглушка, когда нечего показывать (нет сети и нет кэша).
void renderStatus(PixelSink &sink, const RenderCtx &ctx, const char *line1,
                  const char *line2);

// Заглушка экрана 480x480 по макету 08.11: шапка (Wi-Fi, часы), значок в круге,
// заголовок, две строки пояснения и, если задана, кнопка (STATUS_BTN_*).
struct StatusScreen {
  const char *icon = "";        // знак в круге: «…», «!»
  uint16_t iconColor = 0xFFFF;
  const char *title = "";
  const char *line1 = "";
  const char *line2 = "";
  const char *button = nullptr; // подпись кнопки; nullptr — кнопки нет
};
void renderStatusScreen(PixelSink &sink, const RenderCtx &ctx, const StatusScreen &s);
static const int16_t STATUS_BTN_X = 80, STATUS_BTN_Y = 360, STATUS_BTN_W = 320,
                     STATUS_BTN_H = 56;
