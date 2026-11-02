// Панель 480x480 на Guition 4848S040. Шаг A: раскладка зашита в прошивку,
// сети нет. Проверяем отрисовку display list и кнопки на живом железе.
//
// Кадр собирается в теневом буфере в PSRAM и уходит на экран целиком —
// по пикселю в живой кадровый буфер было бы медленно и с мерцанием.
#include <Arduino.h>
#include <ArduinoJson.h>

#include "board.h"
#include "demo_layout.h"
#include "display_list.h"
#include "gfx.h"

static const uint32_t FRAME_MS = 40;   // для бегущей строки; без неё — по событию

static uint16_t *frame = nullptr;

struct FrameSink : PixelSink {
  void px(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    frame[y * SCREEN_W + x] = color;
  }
};
static FrameSink sink;

static JsonDocument doc;
static uint8_t  pageIdx   = 0;
static uint32_t pageStart = 0;
static bool     animated  = false;   // на странице есть бегущая строка
static bool     dirty     = true;

// Кнопка под пальцем. Копия id, а не указатель: в шаге B документ будет
// меняться по ответу сервера, пока палец ещё на экране.
static char      pressedId[32] = "";
static ButtonHit pressedHit;

static JsonObjectConst currentPage() {
  return doc["pages"][pageIdx].as<JsonObjectConst>();
}

static void onPageChanged() {
  pageStart = millis();
  animated = false;
  for (JsonObjectConst L : currentPage()["layers"].as<JsonArrayConst>())
    if (!strcmp(L["t"] | "", "scroll")) animated = true;
  pressedId[0] = '\0';
  dirty = true;
}

// dwell 0 или одна страница — ротации нет.
static void rotate() {
  JsonArrayConst pages = doc["pages"].as<JsonArrayConst>();
  const uint32_t dwell = (currentPage()["dwell"] | 0) * 1000UL;
  if (pages.size() < 2 || dwell == 0) return;
  if (millis() - pageStart >= dwell) {
    pageIdx = (pageIdx + 1) % pages.size();
    onPageChanged();
  }
}

static void handleTouch() {
  int16_t x, y;
  const Touch t = touchRead(x, y);
  if (t == Touch::Down) {
    ButtonHit hit;
    const char *id = hitTest(currentPage(), x, y, hit) ? hit.id : "";
    if (strcmp(id, pressedId)) {      // палец зашёл на кнопку или ушёл с неё
      strlcpy(pressedId, id, sizeof(pressedId));
      pressedHit = hit;
      dirty = true;
    }
  } else if (t == Touch::Up && pressedId[0]) {
    // Срабатывает при отпускании над кнопкой: увёл палец — передумал.
    Serial.printf("кнопка %s -> %s %s (отправка на сервер — шаг B)\n", pressedId,
                  pressedHit.method, pressedHit.path);
    pressedId[0] = '\0';
    dirty = true;
  }
}

static void draw() {
  const uint32_t t0 = millis();
  RenderCtx ctx;
  ctx.w = SCREEN_W;
  ctx.h = SCREEN_H;
  ctx.clockStr = "--:--";              // часов без сети нет, NTP — шаг B
  ctx.pageElapsedMs = millis() - pageStart;
  ctx.pressedId = pressedId[0] ? pressedId : nullptr;
  renderPage(sink, currentPage(), ctx);
  const uint32_t t1 = millis();
  present(frame);
  const uint32_t t2 = millis();

  static bool reported = false;        // один раз — чтобы знать запас по времени
  if (!reported) {
    reported = true;
    Serial.printf("кадр: отрисовка %u мс, вывод %u мс\n", (unsigned)(t1 - t0),
                  (unsigned)(t2 - t1));
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== panel-4848, шаг A: раскладка без сети ===");

  frame = static_cast<uint16_t *>(
      heap_caps_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
  if (!boardBegin() || !frame) {
    Serial.println("ОШИБКА: нет экрана или PSRAM под кадр — дальше не идём");
    while (true) delay(1000);
  }

  const DeserializationError err = deserializeJson(doc, DEMO_LAYOUT);
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
  if (dirty || (animated && now - lastFrame >= FRAME_MS)) {
    draw();
    dirty = false;
    lastFrame = now;
  }
  delay(10);
}
