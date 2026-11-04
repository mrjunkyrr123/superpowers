// Конфигурация экрана — дословно из Arduino_GFX, examples/PDQgraphicstest/
// Arduino_GFX_dev_device.h, блок ESP32_4848S040_86BOX_GUITION. Проверена на
// плате в bringup-4848: поворот, цвета и оси сенсора верны без правок.
#include "board.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>

static const int PIN_BL = 38;
static const int PIN_SDA = 19, PIN_SCL = 45;

static Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC */, 39 /* CS */, 48 /* SCK */, 47 /* MOSI */,
    GFX_NOT_DEFINED /* MISO */);

static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0 /* R4 */,
    8 /* G0 */, 20 /* G1 */, 3 /* G2 */, 46 /* G3 */, 9 /* G4 */, 10 /* G5 */,
    4 /* B0 */, 5 /* B1 */, 6 /* B2 */, 7 /* B3 */, 15 /* B4 */,
    1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
    0 /* pclk_active_neg */, GFX_NOT_DEFINED /* prefer_speed: 12 МГц */, false /* big endian */,
    0 /* de_idle_high */, 0 /* pclk_idle_high */,
    // Bounce-буферы во внутренней памяти: экран читает кадр через них, а не
    // прямо из PSRAM. Без них наша запись в PSRAM (теневой буфер, копирование
    // кадра) отнимает у экрана поток, и картинка дрожит и сдвигается вбок.
    // 20 строк: с Wi-Fi на 10 строках прерывание опаздывало и в начале строк шли штрихи
    // (без Wi-Fi их не было). Кадр должен делиться на удвоенный размер буфера.
    SCREEN_W * 20 /* bounce_buffer_size_px */);

static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    SCREEN_W, SCREEN_H, rgbpanel, 0 /* rotation */, true /* auto_flush */,
    bus, GFX_NOT_DEFINED /* RST */, st7701_type9_init_operations,
    sizeof(st7701_type9_init_operations));

// ---------- GT911 ----------
static uint8_t gtAddr = 0x5D;   // бывает 0x5D или 0x14, на нашей плате 0x5D

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

Touch touchRead(int16_t &x, int16_t &y) {
  uint8_t status = 0;
  if (!gtReadReg(0x814E, &status, 1)) return Touch::NoChange;
  if (!(status & 0x80)) return Touch::NoChange;   // новых данных нет

  Touch r = Touch::Up;
  const uint8_t points = status & 0x0F;
  uint8_t p[8];
  if (points > 0 && points <= 5 && gtReadReg(0x814F, p, 8)) {
    x = p[1] | (p[2] << 8);
    y = p[3] | (p[4] << 8);
    r = Touch::Down;
  }
  const uint8_t zero = 0;            // сбросить флаг, иначе GT911 не обновит данные
  gtWriteReg(0x814E, &zero, 1);
  return r;
}

// ---------- экран ----------
bool boardBegin() {
  // 25 кГц — выше слышимого. На 5 кГц драйвер подсветки свистел: дроссель
  // и керамика звенят на частоте ШИМ.
  ledcAttach(PIN_BL, 25000, 8);
  backlight(0);                      // не светить мусором, пока экран не готов

  const bool ok = gfx->begin();
  if (ok) gfx->fillScreen(RGB565_BLACK);

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  if (gtProbe()) Serial.printf("сенсор GT911: 0x%02X\n", gtAddr);
  else Serial.println("ОШИБКА: сенсор GT911 не отвечает");
  return ok;
}

void backlight(int percent) {
  ledcWrite(PIN_BL, map(constrain(percent, 0, 100), 0, 100, 0, 255));
}

void backlightFade(int fromPercent, int toPercent, int ms) {
  const auto duty = [](int p) { return (uint32_t)map(constrain(p, 0, 100), 0, 100, 0, 255); };
  ledcFade(PIN_BL, duty(fromPercent), duty(toPercent), ms);
}

void present(const uint16_t *frame) {
  gfx->draw16bitRGBBitmap(0, 0, const_cast<uint16_t *>(frame), SCREEN_W, SCREEN_H);
}

void presentRows(const uint16_t *frame, int16_t y0, int16_t y1) {
  if (y0 < 0) y0 = 0;
  if (y1 > SCREEN_H) y1 = SCREEN_H;
  if (y1 <= y0) return;
  // Строки полной ширины лежат в кадре подряд — отдаём одним куском.
  gfx->draw16bitRGBBitmap(0, y0, const_cast<uint16_t *>(frame) + y0 * SCREEN_W,
                          SCREEN_W, y1 - y0);
}
