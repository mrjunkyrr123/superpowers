// Панель 480x480 на Guition 4848S040: дашборд RECOROOM. Пока без сети —
// раскладка зашита в прошивку (layout_embedded.h).
//
// Кадр собирается в теневом буфере в PSRAM и уходит на экран целиком —
// по пикселю в живой кадровый буфер было бы медленно и с мерцанием.
#include <Arduino.h>
#include <ArduinoJson.h>

#include "board.h"
#include "layout_embedded.h"
#include "display_list.h"
#include "gfx.h"

static const uint32_t FRAME_MS = 40;   // для бегущей строки; без неё — по событию

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

static JsonDocument doc;
static uint8_t  pageIdx   = 0;
static uint32_t pageStart = 0;
static bool     dirty     = true;

// Полосы экрана с бегущими строками, которые не влезают по ширине и потому
// движутся. Между полными перерисовками обновляются только они.
struct Band { int16_t y0, y1; };
static Band    bands[8];
static uint8_t bandCount = 0;

// Кнопка под пальцем. Копия id, а не указатель: в шаге B документ будет
// меняться по ответу сервера, пока палец ещё на экране.
static char      pressedId[32] = "";
static ButtonHit pressedHit;

static JsonObjectConst currentPage() {
  return doc["pages"][pageIdx].as<JsonObjectConst>();
}

static void onPageChanged() {
  pageStart = millis();
  bandCount = 0;
  for (JsonObjectConst L : currentPage()["layers"].as<JsonArrayConst>()) {
    if (strcmp(L["t"] | "", "scroll")) continue;
    const GfxFont *f = fontByName(L["f"] | "f5");
    if (!f || textWidth(*f, L["s"] | "") <= (L["w"] | SCREEN_W)) continue;  // стоит на месте
    if (bandCount == sizeof(bands) / sizeof(bands[0])) break;
    const int16_t y = L["y"] | 0;
    bands[bandCount++] = {y, static_cast<int16_t>(y + f->yAdvance)};
  }
  pressedId[0] = '\0';
  dirty = true;
}

// Пролистал руками — экран держится столько, потом автосмена продолжается.
static const uint32_t MANUAL_HOLD_MS = 60UL * 1000;
static uint32_t holdUntil = 0;          // 0 — автосмена идёт как обычно

static void turnPage(int delta) {
  const int n = doc["pages"].size();
  pageIdx = (pageIdx + delta + n) % n;
  onPageChanged();
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
  if (dwell && millis() - pageStart >= dwell) turnPage(+1);
}

// Жест: сдвиг больше SLOP — уже не нажатие, кнопка гаснет; при отпускании
// сдвиг по горизонтали от SWIPE_MIN и вдвое больше вертикального — листание.
static const int16_t SLOP = 24, SWIPE_MIN = 80;
static bool    fingerDown = false, moved = false;
static int16_t startX, startY, lastX, lastY;

static void handleTouch() {
  int16_t x, y;
  const Touch t = touchRead(x, y);
  if (t == Touch::Down) {
    if (!fingerDown) {
      fingerDown = true;
      moved = false;
      startX = x;
      startY = y;
    }
    lastX = x;
    lastY = y;
    if (!moved && (abs(x - startX) > SLOP || abs(y - startY) > SLOP)) moved = true;

    ButtonHit hit;
    const char *id = !moved && hitTest(currentPage(), x, y, hit) ? hit.id : "";
    if (strcmp(id, pressedId)) {      // палец зашёл на кнопку, ушёл с неё или поехал
      strlcpy(pressedId, id, sizeof(pressedId));
      pressedHit = hit;
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
      Serial.printf("кнопка %s -> %s %s (отправка на сервер — шаг B)\n", pressedId,
                    pressedHit.method, pressedHit.path);
      pressedId[0] = '\0';
      dirty = true;
    }
  }
}

static RenderCtx makeCtx() {
  RenderCtx ctx;
  ctx.w = SCREEN_W;
  ctx.h = SCREEN_H;
  ctx.clockStr = "--:--";              // часов без сети нет, NTP — шаг B
  ctx.pageElapsedMs = millis() - pageStart;
  ctx.pressedId = pressedId[0] ? pressedId : nullptr;
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

static void draw() {
  const uint32_t t0 = millis();
  sink.y0 = 0;
  sink.y1 = SCREEN_H;
  renderPage(sink, currentPage(), makeCtx());
  const uint32_t t1 = millis();
  present(frame);
  report(false, t1 - t0, millis() - t1);
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

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== panel-4848: дашборд, раскладка без сети ===");

  frame = static_cast<uint16_t *>(
      heap_caps_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
  if (!boardBegin() || !frame) {
    Serial.println("ОШИБКА: нет экрана или PSRAM под кадр — дальше не идём");
    while (true) delay(1000);
  }

  const DeserializationError err = deserializeJson(doc, EMBEDDED_LAYOUT);
  if (err || doc["pages"].size() == 0) {
    Serial.printf("ОШИБКА: раскладка не разобралась: %s\n", err.c_str());
    while (true) delay(1000);
  }
  Serial.printf("раскладка: %u стр.\n", (unsigned)doc["pages"].size());

  onPageChanged();
  draw();
  dirty = false;
  backlight(doc["brightness"] | 70);   // свет — после первого кадра, без вспышки мусора
}

void loop() {
  static uint32_t lastFrame = 0;
  handleTouch();
  rotate();

  const uint32_t now = millis();
  if (dirty) {
    draw();
    dirty = false;
    lastFrame = now;
  } else if (bandCount && now - lastFrame >= FRAME_MS) {
    drawBands();
    lastFrame = now;
  }
  delay(10);
}
