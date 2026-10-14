// Первое включение панели HUB75: крутит тестовые таблицы, больше ничего.
//
// Каждая таблица проверяет ровно одну вещь, поэтому по тому, какая из них
// сломалась, сразу видно, что именно не так. Расшифровка — в README.md.
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "panel_pins.h"

#ifndef COLOR_DEPTH
#define COLOR_DEPTH 8
#endif

static const uint32_t DWELL_MS = 3000;

static MatrixPanel_I2S_DMA *panel = nullptr;
static uint16_t RED, GREEN, BLUE, WHITE, YELLOW, GREY, DIM;

static void patternCorners() {
  // Повороты и зеркальность. Красный обязан оказаться слева сверху.
  panel->fillScreen(0);
  panel->drawRect(0, 0, PANEL_RES_X, PANEL_RES_Y, DIM);
  const int s = min(PANEL_RES_X, PANEL_RES_Y) / 4;
  panel->fillRect(1, 1, s, s, RED);
  panel->fillRect(PANEL_RES_X - 1 - s, 1, s, s, GREEN);
  panel->fillRect(1, PANEL_RES_Y - 1 - s, s, s, BLUE);
  panel->fillRect(PANEL_RES_X - 1 - s, PANEL_RES_Y - 1 - s, s, s, YELLOW);
}

static void patternRGB() {
  // Порядок цветов. Полосы обязаны идти красная-зелёная-синяя слева направо.
  const int t = PANEL_RES_X / 3;
  panel->fillRect(0, 0, t, PANEL_RES_Y, RED);
  panel->fillRect(t, 0, t, PANEL_RES_Y, GREEN);
  panel->fillRect(t * 2, 0, PANEL_RES_X - t * 2, PANEL_RES_Y, BLUE);
}

static void patternGrid() {
  // Схема развёртки. Разрывы и съехавшие строки означают неверную развёртку.
  panel->fillScreen(0);
  panel->drawRect(0, 0, PANEL_RES_X, PANEL_RES_Y, WHITE);
  for (int x = 0; x < PANEL_RES_X; x += 8) panel->drawFastVLine(x, 0, PANEL_RES_Y, GREY);
  for (int y = 0; y < PANEL_RES_Y; y += 8) panel->drawFastHLine(0, y, PANEL_RES_X, GREY);
}

static void patternGradient() {
  // Глубина цвета. Ступени вместо плавного перехода — мало бит на канал.
  for (int x = 0; x < PANEL_RES_X; x++) {
    const uint8_t v = (uint32_t)255 * x / (PANEL_RES_X - 1);
    panel->drawFastVLine(x, 0, PANEL_RES_Y, panel->color565(v, v, v));
  }
}

static void patternRows() {
  // Порядок строк. При сбитой развёртке переход перестаёт быть монотонным.
  for (int y = 0; y < PANEL_RES_Y; y++) {
    const uint8_t v = (uint32_t)255 * y / (PANEL_RES_Y - 1);
    panel->drawFastHLine(0, y, PANEL_RES_X, panel->color565(v, 255 - v, 128));
  }
}

static void patternText() {
  // Встроенный шрифт Adafruit GFX, только латиница — этого для проверки хватает.
  panel->fillScreen(0);
  panel->setTextColor(GREEN);
  panel->setTextSize(1);
  panel->setCursor(2, 2);
  panel->print("ESP32 OK");
  panel->setTextColor(GREY);
  panel->setCursor(2, 12);
  panel->printf("%dx%d", PANEL_RES_X, PANEL_RES_Y);
}

static void patternWhite() {
  // Питание. Белое поле — максимальный ток; просадка видна как уход в жёлтый.
  panel->fillScreen(WHITE);
}

struct Pattern {
  const char *name;
  void (*draw)();
};

static const Pattern PATTERNS[] = {
    {"corners  — поворот и зеркальность", patternCorners},
    {"rgb      — порядок цветов", patternRGB},
    {"grid     — схема развёртки", patternGrid},
    {"rows     — порядок строк", patternRows},
    {"gradient — глубина цвета", patternGradient},
    {"text     — вывод текста", patternText},
    {"white    — питание под нагрузкой", patternWhite},
};
static const size_t PATTERN_COUNT = sizeof(PATTERNS) / sizeof(PATTERNS[0]);

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nпанель %dx%d, цепочка %d, глубина цвета %d бит\n",
                PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN, COLOR_DEPTH);

  HUB75_I2S_CFG::i2s_pins pins = {PIN_R1, PIN_G1, PIN_B1, PIN_R2, PIN_G2, PIN_B2,
                                  PIN_A,  PIN_B,  PIN_C,  PIN_D,  PIN_E,
                                  PIN_LAT, PIN_OE, PIN_CLK};
  HUB75_I2S_CFG mx(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN, pins);
  mx.driver = static_cast<HUB75_I2S_CFG::shift_driver>(DRV_SHIFT);
  mx.line_decoder = static_cast<HUB75_I2S_CFG::line_driver>(DRV_LINE);
  mx.clkphase = false;
  mx.setPixelColorDepthBits(COLOR_DEPTH);

  panel = new MatrixPanel_I2S_DMA(mx);
  if (!panel->begin()) {
    // Чаще всего это нехватка памяти под DMA: уменьшите COLOR_DEPTH.
    Serial.println("ОШИБКА: не удалось инициализировать панель");
    while (true) delay(1000);
  }
  // Начинаем тускло: если в питании ошибка, на малом токе она дешевле.
  panel->setBrightness8(60);
  panel->clearScreen();

  RED    = panel->color565(255, 0, 0);
  GREEN  = panel->color565(0, 255, 0);
  BLUE   = panel->color565(0, 0, 255);
  WHITE  = panel->color565(255, 255, 255);
  YELLOW = panel->color565(255, 255, 0);
  GREY   = panel->color565(0, 90, 90);
  DIM    = panel->color565(40, 40, 40);

  Serial.println("панель запущена, яркость 60/255");
}

void loop() {
  static size_t i = 0;
  const Pattern &p = PATTERNS[i % PATTERN_COUNT];
  Serial.printf("[%u/%u] %s\n", (unsigned)(i % PATTERN_COUNT) + 1,
                (unsigned)PATTERN_COUNT, p.name);
  p.draw();
  i++;
  delay(DWELL_MS);
}
