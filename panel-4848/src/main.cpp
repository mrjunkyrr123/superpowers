// Панель 480x480 на Guition 4848S040: дашборд RECOROOM.
//
// Раскладка (display list) приходит с сервера и кэшируется во флеш: после
// перезагрузки без сети на экране последние известные данные, а не пустота.
// Без настроек сети (ssid/url в NVS пусты) показывается демо из
// layout_embedded.h с меткой «нет связи» — цифры в нём ненастоящие.
//
// Кадр собирается в теневом буфере в PSRAM и уходит на экран целиком —
// по пикселю в живой кадровый буфер было бы медленно и с мерцанием.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "board.h"
#include "layout_embedded.h"
#include "display_list.h"
#include "gfx.h"
#include "net.h"
#include "secrets.h"
#include "storage.h"

static const uint32_t FRAME_MS          = 40;   // для бегущей строки; без неё — по событию
static const uint32_t DEFAULT_POLL      = 20;   // секунд, если сервер не сказал иначе
static const uint32_t HEARTBEAT_MS      = 5UL * 60 * 1000;  // каждый запрос — это время радио
// Радио выключено почти всё время: с ним на экране полосы (без Wi-Fi их нет, проверено).
// Включается за данными и на нажатия. После старта держим его, чтобы успеть прошить по воздуху.
static const uint32_t BOOT_ONLINE_MS    = 3UL * 60 * 1000;
static const uint32_t ACTION_HOLD_MS    = 30UL * 1000;      // второе касание — без ожидания связи
static const uint32_t NO_DATA_REBOOT_MS = 20UL * 60 * 1000;

static uint16_t *frame = nullptr;

// Пишет только в строки [y0, y1): так полосу с бегущей строкой можно
// перерисовать, не трогая остальной кадр.
struct FrameSink : PixelSink {
  int16_t y0 = 0, y1 = SCREEN_H;
  void px(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < y0 || x >= SCREEN_W || y >= y1) return;
    frame[y * SCREEN_W + x] = color;
  }
  void hline(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    if (y < y0 || y >= y1) return;
    int16_t x1 = x + w;
    if (x < 0) x = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    uint16_t *p = frame + y * SCREEN_W + x;
    for (int16_t i = x; i < x1; ++i) *p++ = color;
  }
};
static FrameSink sink;

static Config       cfg;
static JsonDocument doc;
static String       etag;
static bool     haveData  = false;
static bool     demo      = false;   // показываем зашитое демо, сети нет вовсе
static uint8_t  pageIdx   = 0;
static uint32_t pageStart = 0;
static bool     dirty     = true;

static uint32_t bootMs   = 0;
// volatile: пишет сетевая задача на другом ядре, читает loop.
static volatile uint32_t lastOkMs = 0;   // 0 — с сервера ещё ничего не приходило
static volatile uint32_t pollMs   = DEFAULT_POLL * 1000;

static int  brightness = 70, litBrightness = -1;
static char clockBuf[8] = "--:--";
static bool staleShown  = false;
static int8_t wifiShown = -1;   // дуг на значке Wi-Fi, -1 — нет связи
// Радио почти всегда выключено, поэтому значок показывает сигнал последнего сеанса.
static volatile int8_t wifiNow = -1;
static volatile bool   wifiAlways = false;  // сервер просит держать радио (прошивка по воздуху)

// Пороги как у телефонов: от −60 dBm три дуги, от −70 две, от −80 одна, слабее — точка.
static int8_t wifiLevel() {
  const int rssi = WiFi.RSSI();
  return rssi >= -60 ? 3 : rssi >= -70 ? 2 : rssi >= -80 ? 1 : 0;
}

// Полосы экрана с бегущими строками, которые не влезают по ширине и потому
// движутся. Между полными перерисовками обновляются только они.
struct Band { int16_t y0, y1; };
static Band    bands[8];
static uint8_t bandCount = 0;

// Кнопка под пальцем. Всё копией, а не указателями в документ: ответ сервера
// может заменить документ, пока палец ещё на экране.
static char pressedId[32]     = "";
static char pressedMethod[8]  = "";
static char pressedPath[128]  = "";

// Нажатие ушло, ответа ещё нет. Радио включается и запрос идёт секунды: кнопка горит
// сразу, чтобы было видно, что касание принято, а новые нажатия не принимаются — иначе
// повторный тык становится вторым касанием, а после закрытия задачи строки съезжают
// и он попадает по соседней (было 06.11: закрылось лишнее).
static const uint32_t WAIT_REPLY_MS = 8000;
static char     waitId[32] = "";
static uint32_t waitUntil  = 0;

static JsonObjectConst currentPage() {
  return doc["pages"][pageIdx].as<JsonObjectConst>();
}

static void refreshBands() {
  bandCount = 0;
  for (JsonObjectConst L : currentPage()["layers"].as<JsonArrayConst>()) {
    if (strcmp(L["t"] | "", "scroll")) continue;
    const GfxFont *f = fontByName(L["f"] | "f5");
    if (!f || textWidth(*f, L["s"] | "") <= (L["w"] | SCREEN_W)) continue;  // стоит на месте
    if (bandCount == sizeof(bands) / sizeof(bands[0])) break;
    const int16_t y = L["y"] | 0;
    bands[bandCount++] = {y, static_cast<int16_t>(y + f->yAdvance)};
  }
}

static void onPageChanged() {
  pageStart = millis();
  refreshBands();
  pressedId[0] = '\0';
  dirty = true;
}

// Пролистал руками — экран держится столько, потом автосмена продолжается.
static const uint32_t MANUAL_HOLD_MS = 60UL * 1000;
static uint32_t holdUntil = 0;          // 0 — автосмена идёт как обычно

static bool fadeNext = false;   // следующий кадр — через затухание подсветки

static void turnPage(int delta) {
  const int n = doc["pages"].size();
  if (!n) return;
  pageIdx = (pageIdx + delta + n) % n;
  onPageChanged();
  fadeNext = n > 1;
}

// Разбирает display list. false — JSON битый или пустой: тогда на экране
// остаётся то, что было, а не пустота.
static bool applyBody(const char *json) {
  JsonDocument fresh;
  const DeserializationError err = deserializeJson(fresh, json);
  if (err) {
    Serial.printf("JSON: %s\n", err.c_str());
    return false;
  }
  if (!fresh["pages"].is<JsonArray>() || fresh["pages"].size() == 0) {
    Serial.println(F("JSON: нет непустого массива pages"));
    return false;
  }

  const size_t oldPages = haveData ? doc["pages"].size() : 0;
  doc = std::move(fresh);
  haveData = true;
  // Кнопки могли сдвинуться: нажатие под пальцем отменяем, ждать ответа больше нечего.
  pressedId[0] = '\0';
  waitId[0] = '\0';

  // Те же экраны с новыми цифрами — остаёмся, где были, и не сбрасываем
  // ни паузу после свайпа, ни бегущую строку. Другое число экранов — с начала.
  if (doc["pages"].size() == oldPages) {
    refreshBands();
    dirty = true;
  } else {
    pageIdx = 0;
    holdUntil = 0;
    onPageChanged();
  }

  const uint32_t poll = doc["poll_interval"] | DEFAULT_POLL;
  pollMs = constrain(poll, 5UL, 3600UL) * 1000;
  brightness = constrain(doc["brightness"] | 70, 5, 100);
  wifiAlways = doc["wifi_always"] | false;
  return true;
}

// ---------- Сеть — своя задача на ядре 0 ----------
// Шифрованный запрос на слабом Wi-Fi идёт секундами: в loop это замирание экрана и
// касаний. Задача только качает и шлёт; разбирает ответ и рисует loop — документ его.
struct Action {
  char method[8];
  char path[128];
  char id[32];
};
static QueueHandle_t     actionQ = nullptr;
static SemaphoreHandle_t bodyMux = nullptr;
static String            freshBody;        // новый ответ сервера, только под bodyMux
static volatile bool     pollNow = false;  // переспросить, не дожидаясь срока

static void pollOnce() {
  String body;
  const Fetch r = fetchDisplayList(cfg, body, etag);
  if (r == Fetch::Updated) {
    // Проверяем здесь: битый ответ не должен затереть рабочий кэш.
    JsonDocument probe;
    if (deserializeJson(probe, body) || probe["pages"].size() == 0) {
      Serial.println(F("JSON: ответ сервера битый, оставляем что было"));
      return;
    }
#ifndef NO_CACHE_WRITE_TEST   // диагностика помех: без записи во флеш
    cacheSave(body, etag);
#endif
    xSemaphoreTake(bodyMux, portMAX_DELAY);
    freshBody = body;
    xSemaphoreGive(bodyMux);
    lastOkMs = millis();
  } else if (r == Fetch::NotModified) {
    lastOkMs = millis();
  }
}

static void netTask(void *) {
  bool awake = netBegin(cfg);    // до 20 с — экран в это время живёт
  uint32_t holdUntil = millis() + BOOT_ONLINE_MS;
  uint32_t lastPoll = 0, lastBeat = millis();
  bool first = true;
  for (;;) {
    const uint32_t now = millis();
    const bool pollDue = first || pollNow || now - lastPoll >= pollMs;
    if (!awake && (pollDue || uxQueueMessagesWaiting(actionQ))) {
      awake = netWake(cfg);
      if (!awake) {
        wifiNow = -1;
        first = false;
        pollNow = false;
        lastPoll = now;          // не долбим точку доступа — следующая попытка по сроку
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
    }
    if (awake) {
      netLoop();
      wifiNow = netOnline() ? wifiLevel() : -1;
      Action a;
      if (xQueueReceive(actionQ, &a, 0) == pdTRUE) {
        const int code = sendAction(cfg, a.method, a.path, a.id);
        Serial.printf("  ответ сервера на %s: %d\n", a.id, code);
        holdUntil = millis() + ACTION_HOLD_MS;
        // Любой ответ сервера — повод переспросить: экран мог поменяться, а на 404
        // (задачи уже нет) панель иначе показывала бы старое до следующего опроса.
        if (code > 0) pollNow = true;
      }
      if (first || pollNow || millis() - lastPoll >= pollMs) {
        first = false;
        pollNow = false;
        lastPoll = millis();
        pollOnce();
      }
      if (millis() - lastBeat >= HEARTBEAT_MS) {
        lastBeat = millis();
        sendHeartbeat(cfg, etag, lastBeat / 1000);
      }
      // Пока данных нет вовсе, радио не гасим: на экране заглушка, полосы не важны.
      if (!wifiAlways && lastOkMs && static_cast<int32_t>(millis() - holdUntil) >= 0) {
        netSleep();
        awake = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// Забирает ответ, который принесла сетевая задача.
static void takeFreshBody() {
  if (!bodyMux) return;
  String body;
  xSemaphoreTake(bodyMux, portMAX_DELAY);
  body = freshBody;
  freshBody = String();
  xSemaphoreGive(bodyMux);
  if (body.length() && applyBody(body.c_str()))
    Serial.printf("данные обновлены: %u байт, %u экр.\n", (unsigned)body.length(),
                  (unsigned)doc["pages"].size());
}

// dwell 0 или одна страница — ротации нет.
static void rotate() {
  if (doc["pages"].size() < 2) return;
  if (holdUntil) {
    if (static_cast<int32_t>(millis() - holdUntil) < 0) return;
    holdUntil = 0;
    turnPage(+1);
    return;
  }
  const uint32_t dwell = (currentPage()["dwell"] | 0) * 1000UL;
  if (waitId[0]) return;         // ждём ответа на нажатие — экран не листаем
  if (dwell && millis() - pageStart >= dwell) turnPage(+1);
}

static void draw();

static void pressButton(const char *id) {
  Serial.printf("кнопка %s -> %s %s\n", id, pressedMethod, pressedPath);
  if (demo) {
    Serial.println(F("  демо без сети — никуда не отправлено"));
    return;
  }
  Action a;
  strlcpy(a.method, pressedMethod, sizeof(a.method));
  strlcpy(a.path, pressedPath, sizeof(a.path));
  strlcpy(a.id, id, sizeof(a.id));
  if (xQueueSend(actionQ, &a, 0) != pdTRUE) Serial.println(F("  очередь нажатий полна"));
}

// Жест: сдвиг больше SLOP — уже не нажатие, кнопка гаснет; при отпускании
// сдвиг по горизонтали от SWIPE_MIN и вдвое больше вертикального — листание.
static const int16_t SLOP = 24, SWIPE_MIN = 80;
static bool    fingerDown = false, moved = false;
static int16_t startX, startY, lastX, lastY;

static void handleTouch() {
  int16_t x, y;
  const Touch t = touchRead(x, y);
  if (!haveData) return;
  if (t == Touch::Down) {
    if (!fingerDown) {
      fingerDown = true;
      moved = false;
      startX = x;
      startY = y;
      pressedId[0] = '\0';   // отпускание могло потеряться — адрес кнопки берём заново
    }
    lastX = x;
    lastY = y;
    if (!moved && (abs(x - startX) > SLOP || abs(y - startY) > SLOP)) moved = true;

    ButtonHit hit;
    const char *id = !moved && !waitId[0] && hitTest(currentPage(), x, y, hit) ? hit.id : "";
    if (strcmp(id, pressedId)) {      // палец зашёл на кнопку, ушёл с неё или поехал
      strlcpy(pressedId, id, sizeof(pressedId));
      if (*id) {
        strlcpy(pressedMethod, hit.method, sizeof(pressedMethod));
        strlcpy(pressedPath, hit.path, sizeof(pressedPath));
      }
      dirty = true;
    }
  } else if (t == Touch::Up && fingerDown) {
    fingerDown = false;
    const int16_t dx = lastX - startX, dy = lastY - startY;
    if (moved && abs(dx) >= SWIPE_MIN && abs(dx) > 2 * abs(dy)) {
      turnPage(dx < 0 ? +1 : -1);     // влево — следующий, как в телефоне
      holdUntil = millis() + MANUAL_HOLD_MS;
      if (!holdUntil) holdUntil = 1;  // 0 занят под «паузы нет»
      Serial.printf("свайп %s -> экран %u\n", dx < 0 ? "влево" : "вправо", pageIdx + 1);
    } else if (pressedId[0]) {
      // Срабатывает при отпускании над кнопкой: увёл палец — передумал.
      // Сначала гасим подсветку, потом шлём: запрос может занять секунды.
      char id[sizeof(pressedId)];
      strlcpy(id, pressedId, sizeof(id));
      pressedId[0] = '\0';
      strlcpy(waitId, id, sizeof(waitId));
      waitUntil = millis() + WAIT_REPLY_MS;
      draw();
      pressButton(id);
    }
  }
}

// Данные не свежие: демо, ничего не приходило или давно нет ответа.
static bool isStale() {
  if (demo) return true;
  return !lastOkMs || millis() - lastOkMs > 2 * pollMs;
}

static RenderCtx makeCtx() {
  RenderCtx ctx;
  ctx.w = SCREEN_W;
  ctx.h = SCREEN_H;
  ctx.clockStr = clockBuf;
  ctx.pageElapsedMs = millis() - pageStart;
  ctx.pressedId = pressedId[0] ? pressedId : waitId[0] ? waitId : nullptr;
  ctx.stale = staleShown;
  ctx.wifi = wifiShown;
  return ctx;
}

// Раз на экран — чтобы знать запас по времени.
static void report(bool bandsOnly, uint32_t renderMs, uint32_t presentMs) {
  static int reportedPage[2] = {-1, -1};
  if (reportedPage[bandsOnly] == pageIdx) return;
  reportedPage[bandsOnly] = pageIdx;
  Serial.printf("экран %u (%s), %s: отрисовка %u мс, вывод %u мс\n", pageIdx + 1,
                currentPage()["id"] | "?", bandsOnly ? "полосы" : "кадр",
                (unsigned)renderMs, (unsigned)presentMs);
}

// Собирает кадр в теневом буфере; на экран он ещё не идёт.
static void renderFrame() {
  sink.y0 = 0;
  sink.y1 = SCREEN_H;
  const RenderCtx ctx = makeCtx();
  if (haveData) {
    renderPage(sink, currentPage(), ctx);
  } else {
    const bool online = netOnline();
    renderStatus(sink, ctx, online ? "НЕТ ДАННЫХ" : "НЕТ СЕТИ",
                 online ? cfg.url.c_str() : cfg.ssid.c_str());
  }
}

static void draw() {
  const uint32_t t0 = millis();
  renderFrame();
  const uint32_t t1 = millis();
  present(frame);
  if (haveData) report(false, t1 - t0, millis() - t1);
  // Свет — после кадра: при включении не будет вспышки мусора.
  if (brightness != litBrightness) {
    backlight(brightness);
    litBrightness = brightness;
  }
}

// Смена экрана: подсветка плавно гаснет, кадр меняется в темноте, подсветка плавно
// зажигается. Кроссфейд кадра — это сотни КБ через PSRAM за шаг, он вернёт дрожь;
// подсветку ведёт таймер, память не занята. Новый кадр собирается, пока свет гаснет.
static const int FADE_MS = 180;

static void drawFaded() {
  const int lit = litBrightness < 0 ? brightness : litBrightness;
  backlightFade(lit, 0, FADE_MS);
  const uint32_t t0 = millis();
  renderFrame();
  const uint32_t spent = millis() - t0;
  if (spent < FADE_MS) delay(FADE_MS - spent);
  present(frame);
  delay(30);                     // кадр дошёл до экрана — панель обновляется ~40 раз в с
  backlightFade(0, brightness, FADE_MS);
  litBrightness = brightness;
}

// Только полосы с бегущими строками. Страница проходится целиком, но в память
// пишутся лишь строки полосы — а запись в PSRAM и есть основная цена кадра.
static void drawBands() {
  const uint32_t t0 = millis();
  const RenderCtx ctx = makeCtx();
  uint32_t renderMs = 0;
  for (uint8_t i = 0; i < bandCount; ++i) {
    sink.y0 = bands[i].y0;
    sink.y1 = bands[i].y1;
    const uint32_t r0 = millis();
    renderPage(sink, currentPage(), ctx);
    renderMs += millis() - r0;
    presentRows(frame, bands[i].y0, bands[i].y1);
  }
  report(true, renderMs, millis() - t0 - renderMs);
}

static void showStatus(const char *line1, const char *line2) {
  RenderCtx ctx;
  ctx.w = SCREEN_W;
  ctx.h = SCREEN_H;
  sink.y0 = 0;
  sink.y1 = SCREEN_H;
  renderStatus(sink, ctx, line1, line2);
  present(frame);
  if (litBrightness < 0) {
    backlight(brightness);
    litBrightness = brightness;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(200);              // строка конфигурации с Serial не держит экран
  delay(300);
  Serial.println(F("\n=== panel-4848: дашборд " FW_VERSION " ==="));
  bootMs = millis();

  frame = static_cast<uint16_t *>(
      heap_caps_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
  if (!boardBegin() || !frame) {
    Serial.println("ОШИБКА: нет экрана или PSRAM под кадр — дальше не идём");
    while (true) delay(1000);
  }

  loadConfig(cfg);
#ifdef NO_NET_TEST
  // Диагностика помех экрана: сеть не поднимаем вовсе, настройки в NVS не трогаем.
  // Сборка: $env:PLATFORMIO_BUILD_FLAGS = '-DNO_NET_TEST' перед flash.ps1.
  cfg.ssid = "";
  Serial.println(F("NO_NET_TEST: сеть выключена для опыта"));
#endif

  if (!cfg.valid()) {
    demo = true;
    Serial.println(F("нет настроек сети — демо без сети. Команда show — справка"));
    if (!applyBody(EMBEDDED_LAYOUT)) {
      Serial.println(F("ОШИБКА: зашитая раскладка не разобралась"));
      while (true) delay(1000);
    }
  } else {
    // Кэш — до сети: если Wi-Fi не поднимется, покажем последние данные.
    String cached, cachedEtag;
    if (cacheLoad(cached, cachedEtag) && applyBody(cached.c_str())) {
      etag = cachedEtag;
      Serial.printf("восстановлен кэш: %u байт\n", (unsigned)cached.length());
    }
    if (!haveData) showStatus("ПОДКЛЮЧЕНИЕ", cfg.ssid.c_str());
    else draw();

    actionQ = xQueueCreate(4, sizeof(Action));
    bodyMux = xSemaphoreCreateMutex();
    // Ядро 0 — там же стек Wi-Fi; loop и прерывание экрана остаются на ядре 1.
    // 16 КБ стека: TLS-рукопожатие на меньшем падает.
    xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, nullptr, 0);
  }

  char t[8];
  localTimeStr(t, sizeof(t));
  strlcpy(clockBuf, t, sizeof(clockBuf));
  staleShown = isStale();
  dirty = true;
}

void loop() {
  static uint32_t lastFrame = 0;
  configConsole(cfg);

  if (!demo) {
    takeFreshBody();
    // Чаще всего «зависает» не процессор, а сетевой стек — перезагрузка
    // единственное, что это лечит без выезда на объект.
    if (millis() - (lastOkMs ? lastOkMs : bootMs) > NO_DATA_REBOOT_MS) {
      Serial.println(F("нет данных слишком долго — перезагрузка"));
      ESP.restart();
    }
  }

  handleTouch();
  rotate();
  // Сервер не ответил — отпускаем кнопку, иначе панель перестанет принимать нажатия.
  if (waitId[0] && static_cast<int32_t>(millis() - waitUntil) >= 0) {
    waitId[0] = '\0';
    dirty = true;
  }

  // Часы и метка «нет связи» меняются редко — перерисовка только по факту.
  char t[8];
  localTimeStr(t, sizeof(t));
  if (strcmp(t, clockBuf)) {
    strlcpy(clockBuf, t, sizeof(clockBuf));
    dirty = true;
  }
  const bool stale = isStale();
  if (stale != staleShown) {
    staleShown = stale;
    dirty = true;
  }
  // Перерисовка — только когда меняется число дуг.
  if (wifiNow != wifiShown) {
    wifiShown = wifiNow;
    dirty = true;
  }
  // Экран-заглушка ловит появление сети раз в секунду.
  if (!haveData && millis() - lastFrame >= 1000) dirty = true;

  const uint32_t t0 = millis();
  if (dirty) {
    dirty = false;
    if (fadeNext) drawFaded();
    else draw();
    fadeNext = false;
    lastFrame = t0;
  } else if (haveData && bandCount && t0 - lastFrame >= FRAME_MS) {
    drawBands();
    lastFrame = t0;
  }
  delay(10);
}
