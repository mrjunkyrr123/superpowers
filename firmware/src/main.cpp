// Табло HUB75 64x32: тянет display list из админки и крутит его страницы.
//
// Вся вёрстка приходит с сервера — прошивка только интерпретирует примитивы
// и ротацию. Подробности формата: docs/led-hub75/DESIGN.md
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "display_list.h"
#include "gfx.h"
#include "net.h"
#include "panel_pins.h"
#include "secrets.h"
#include "storage.h"

static const uint32_t FRAME_MS      = 40;          // 25 кадров/с — хватает скроллу
static const uint32_t HEARTBEAT_MS  = 60UL * 1000;
static const uint32_t DEFAULT_POLL  = 20;          // секунд
static const uint32_t NO_DATA_REBOOT_MS = 20UL * 60 * 1000;

static MatrixPanel_I2S_DMA *panel = nullptr;
static Config   cfg;
static JsonDocument doc;          // текущий display list
static String   etag;
static bool     haveData   = false;
static uint8_t  pageIdx    = 0;
static uint32_t pageStart  = 0;
static uint32_t lastPoll   = 0;
static uint32_t lastBeat   = 0;
static uint32_t lastOkMs   = 0;
static uint32_t pollMs     = DEFAULT_POLL * 1000;

// Мост между отрисовкой и библиотекой панели.
struct PanelSink : PixelSink {
  void px(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= PANEL_RES_X || y >= PANEL_RES_Y) return;
    panel->drawPixel(x, y, color);
  }
};
static PanelSink sink;

static void panelBegin() {
  HUB75_I2S_CFG::i2s_pins pins = {PIN_R1, PIN_G1, PIN_B1, PIN_R2, PIN_G2, PIN_B2,
                                  PIN_A,  PIN_B,  PIN_C,  PIN_D,  PIN_E,
                                  PIN_LAT, PIN_OE, PIN_CLK};
  HUB75_I2S_CFG mx(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN, pins);
  mx.double_buff = true;    // кадр собирается в тени, наружу уходит целиком
  panel = new MatrixPanel_I2S_DMA(mx);
  panel->begin();
  panel->setBrightness8(140);
  panel->clearScreen();
}

// Разбирает тело ответа. Возвращает false, если JSON битый — тогда на экране
// остаются прежние данные, а не пустота.
static bool applyBody(const String &body) {
  JsonDocument fresh;
  const DeserializationError err = deserializeJson(fresh, body);
  if (err) {
    Serial.printf("JSON: %s\n", err.c_str());
    return false;
  }
  if (!fresh["pages"].is<JsonArray>() || fresh["pages"].size() == 0) {
    Serial.println(F("JSON: нет непустого массива pages"));
    return false;
  }
  doc = fresh;
  haveData  = true;
  pageIdx   = 0;
  pageStart = millis();

  const uint32_t poll = doc["poll_interval"] | DEFAULT_POLL;
  pollMs = constrain(poll, 5UL, 3600UL) * 1000;

  const int br = doc["brightness"] | 55;
  panel->setBrightness8(map(constrain(br, 5, 100), 0, 100, 0, 255));
  return true;
}

static void poll() {
  String body;
  const Fetch r = fetchDisplayList(cfg, body, etag);
  if (r == Fetch::Updated) {
    if (applyBody(body)) {
      cacheSave(body, etag);
      lastOkMs = millis();
    }
  } else if (r == Fetch::NotModified) {
    lastOkMs = millis();
  }
}

static void rotate() {
  JsonArrayConst pages = doc["pages"].as<JsonArrayConst>();
  if (pages.size() < 2) return;
  const uint32_t dwell = (pages[pageIdx]["dwell"] | 6) * 1000UL;
  if (millis() - pageStart >= dwell) {
    pageIdx   = (pageIdx + 1) % pages.size();
    pageStart = millis();
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\nтабло HUB75 " FW_VERSION));

  panelBegin();
  loadConfig(cfg);

  RenderCtx ctx;
  renderStatus(sink, ctx, "ПОДКЛЮЧЕНИЕ", cfg.ssid.c_str());
  panel->flipDMABuffer();

  // Кэш поднимаем до сети: если WiFi не поднимется, покажем последние
  // известные данные вместо пустого экрана.
  String cached, cachedEtag;
  if (cacheLoad(cached, cachedEtag) && applyBody(cached)) {
    etag = cachedEtag;
    Serial.println(F("восстановлен кэш"));
  }

  if (!cfg.valid())
    Serial.println(F("нет настроек: задайте ssid/url через Serial (команда show)"));

  netBegin(cfg);
  poll();
  lastPoll = lastBeat = millis();
  if (!lastOkMs) lastOkMs = millis();
}

void loop() {
  configConsole(cfg);
  netLoop();

  const uint32_t now = millis();
  if (now - lastPoll >= pollMs) { lastPoll = now; poll(); }
  if (now - lastBeat >= HEARTBEAT_MS) {
    lastBeat = now;
    sendHeartbeat(cfg, etag, now / 1000);
  }

  // Программный сторожевой таймер. Чаще всего табло «зависает» не на
  // процессоре, а на намертво отвалившемся сетевом стеке — перезагрузка
  // единственное, что это лечит без выезда на объект.
  if (cfg.valid() && now - lastOkMs > NO_DATA_REBOOT_MS) {
    Serial.println(F("нет данных слишком долго — перезагрузка"));
    ESP.restart();
  }

  RenderCtx ctx;
  ctx.w = PANEL_RES_X;
  ctx.h = PANEL_RES_Y;
  ctx.stale = (now - lastOkMs) > (2 * pollMs);
  char clockBuf[8];
  localTimeStr(clockBuf, sizeof(clockBuf));
  ctx.clockStr = clockBuf;

  if (haveData) {
    rotate();
    ctx.pageElapsedMs = now - pageStart;
    renderPage(sink, doc["pages"][pageIdx].as<JsonObjectConst>(), ctx);
  } else {
    renderStatus(sink, ctx, netOnline() ? "НЕТ ДАННЫХ" : "НЕТ СЕТИ", clockBuf);
  }
  panel->flipDMABuffer();

  const uint32_t spent = millis() - now;
  if (spent < FRAME_MS) delay(FRAME_MS - spent);
}
