// Первое включение Guition ESP32-S3-4848S040.
//
// Проверяет три вещи по отдельности: экран (тестовые таблицы), сенсор
// (точка под пальцем и координаты в Serial) и PSRAM (размер в Serial).
//
// Конфигурация экрана — дословно из Arduino_GFX, examples/PDQgraphicstest/
// Arduino_GFX_dev_device.h, блок ESP32_4848S040_86BOX_GUITION.
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>

// ---------- экран ----------
static const int PIN_BL = 38;

static Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC */, 39 /* CS */, 48 /* SCK */, 47 /* MOSI */,
    GFX_NOT_DEFINED /* MISO */);

static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0 /* R4 */,
    8 /* G0 */, 20 /* G1 */, 3 /* G2 */, 46 /* G3 */, 9 /* G4 */, 10 /* G5 */,
    4 /* B0 */, 5 /* B1 */, 6 /* B2 */, 7 /* B3 */, 15 /* B4 */,
    1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */);

static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */,
    bus, GFX_NOT_DEFINED /* RST */, st7701_type9_init_operations,
    sizeof(st7701_type9_init_operations));

// ---------- сенсор GT911 ----------
// Выводы INT и RESET у этой платы не разведены, поэтому опрашиваем по таймеру.
static const int PIN_SDA = 19, PIN_SCL = 45;
static uint8_t gtAddr = 0x5D;   // у GT911 бывает 0x5D или 0x14 — проверим оба

static bool gtWriteReg(uint16_t reg, const uint8_t *data, size_t n) {
  Wire.beginTransmission(gtAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  for (size_t i = 0; i < n; ++i) Wire.write(data[i]);
  return Wire.endTransmission() == 0;
}

static bool gtReadReg(uint16_t reg, uint8_t *data, size_t n) {
  Wire.beginTransmission(gtAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)gtAddr, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; ++i) data[i] = Wire.read();
  return true;
}

static bool gtProbe() {
  for (uint8_t a : {0x5D, 0x14}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { gtAddr = a; return true; }
  }
  return false;
}

// Возвращает true и координаты первого касания, если палец на экране.
static bool gtRead(int16_t &x, int16_t &y) {
  uint8_t status = 0;
  if (!gtReadReg(0x814E, &status, 1)) return false;
  const bool ready = status & 0x80;
  const uint8_t points = status & 0x0F;
  bool touched = false;
  if (ready && points > 0 && points <= 5) {
    uint8_t p[8];
    if (gtReadReg(0x814F, p, 8)) {
      x = p[1] | (p[2] << 8);
      y = p[3] | (p[4] << 8);
      touched = true;
    }
  }
  if (ready) {               // сбросить флаг, иначе GT911 не обновит данные
    const uint8_t zero = 0;
    gtWriteReg(0x814E, &zero, 1);
  }
  return touched;
}

// ---------- тестовые таблицы ----------
static void patternCorners() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->drawRect(0, 0, 480, 480, RGB565_DARKGREY);
  gfx->fillRect(2, 2, 100, 100, RGB565_RED);
  gfx->fillRect(378, 2, 100, 100, RGB565_GREEN);
  gfx->fillRect(2, 378, 100, 100, RGB565_BLUE);
  gfx->fillRect(378, 378, 100, 100, RGB565_YELLOW);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(3);
  gfx->setCursor(150, 220);
  gfx->print("ESP32-S3 OK");
}

static void patternRGB() {
  gfx->fillRect(0, 0, 160, 480, RGB565_RED);
  gfx->fillRect(160, 0, 160, 480, RGB565_GREEN);
  gfx->fillRect(320, 0, 160, 480, RGB565_BLUE);
}

static void patternGradient() {
  for (int x = 0; x < 480; ++x) {
    const uint8_t v = x * 255 / 479;
    gfx->drawFastVLine(x, 0, 480, gfx->color565(v, v, v));
  }
}

static void patternTouch() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(3);
  gfx->setCursor(60, 220);
  gfx->print("Touch the screen");
}

typedef void (*PatternFn)();
static const PatternFn PATTERNS[] = {patternCorners, patternRGB, patternGradient, patternTouch};
static const char *NAMES[] = {"corners: красный слева сверху", "rgb: R-G-B слева направо",
                              "gradient: плавно", "touch: водите пальцем"};

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Guition 4848S040 bring-up ===");
  Serial.printf("PSRAM: %u байт (ожидается ~8 МБ)\n", (unsigned)ESP.getPsramSize());
  Serial.printf("Flash: %u байт (ожидается 16 МБ)\n", (unsigned)ESP.getFlashChipSize());

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  if (!gfx->begin()) {
    Serial.println("ОШИБКА: экран не инициализирован. Чаще всего — PSRAM не найдена.");
  } else {
    Serial.println("экран: ok");
  }

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  if (gtProbe()) Serial.printf("сенсор GT911: найден по адресу 0x%02X\n", gtAddr);
  else Serial.println("ОШИБКА: сенсор GT911 не отвечает ни на 0x5D, ни на 0x14");
}

void loop() {
  static uint32_t lastSwitch = 0;
  static int idx = -1;
  const uint32_t now = millis();

  // Первые три таблицы меняются сами, на последней ждём касаний.
  if (idx < 3 && now - lastSwitch > (idx < 0 ? 0u : 3000u)) {
    ++idx;
    lastSwitch = now;
    Serial.printf("[%d/4] %s\n", idx + 1, NAMES[idx]);
    PATTERNS[idx]();
  }

  int16_t x, y;
  if (gtRead(x, y)) {
    Serial.printf("касание: x=%d y=%d\n", x, y);
    if (idx == 3) gfx->fillCircle(x, y, 6, RGB565_CYAN);
  }
  delay(15);
}
