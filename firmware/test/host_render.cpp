// Хостовая сборка слоя отрисовки: тот же gfx.cpp/fonts.cpp, что и в прошивке,
// но без панели. Печатает растр в stdout, чтобы сверить его с эталонным
// рендерером на Python (mock-server/render.py) — см. test/compare.py.
//
//   host_render text  <font> <w> <h> <utf8>   текст
//   host_render rrect <w> <h> <r>             скруглённый прямоугольник
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "gfx.h"
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>

// На Windows argv приходит в кодовой странице ANSI (cp1251), и кириллица
// превращается в '?'. Берём аргумент из UTF-16 командной строки и сами
// переводим в UTF-8 — в той кодировке строки живут в прошивке.
static std::string argUtf8(int i) {
  int n = 0;
  LPWSTR *w = CommandLineToArgvW(GetCommandLineW(), &n);
  std::string out;
  if (w && i < n) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
    out.resize(len > 0 ? len - 1 : 0);
    if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, &out[0], len, nullptr, nullptr);
  }
  LocalFree(w);
  return out;
}
#endif

struct Buf : PixelSink {
  int w, h;
  std::vector<uint16_t> p;
  Buf(int w_, int h_, uint16_t bg = 0) : w(w_), h(h_), p(w_ * h_, bg) {}
  void px(int16_t x, int16_t y, uint16_t c) override {
    if (x >= 0 && y >= 0 && x < w && y < h) p[y * w + x] = c;
  }
  uint16_t get(int16_t x, int16_t y) override {
    return (x >= 0 && y >= 0 && x < w && y < h) ? p[y * w + x] : 0;
  }
  // '#' — точка закрашена, '.' — пусто.
  void dump() const {
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) putchar(p[y * w + x] ? '#' : '.');
      putchar('\n');
    }
  }
  // Точный цвет RGB565 — для сглаженного шрифта.
  void dumpHex() const {
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) printf("%04X", p[y * w + x]);
      putchar('\n');
    }
  }
};

static std::string arg(char **argv, int i) {
#ifdef _WIN32
  (void)argv;
  return argUtf8(i);
#else
  return argv[i];
#endif
}

int main(int argc, char **argv) {
  if (argc >= 6 && !strcmp(argv[1], "text")) {
    const GfxFont *f = fontByName(argv[2]);
    if (!f) { fprintf(stderr, "unknown font %s\n", argv[2]); return 2; }
    Buf b(atoi(argv[3]), atoi(argv[4]));
    const std::string text = arg(argv, 5);
    const char *s = text.c_str();
    drawText(b, 0, 0, *f, 0xFFFF, s);
    printf("width=%d\n", textWidth(*f, s));
    b.dump();
    return 0;
  }
  // aa <font> <w> <h> <фон 565 hex> <цвет 565 hex> <utf8> — сглаживание по фону, точные цвета
  if (argc >= 8 && !strcmp(argv[1], "aa")) {
    const GfxFont *f = fontByName(argv[2]);
    if (!f) { fprintf(stderr, "unknown font %s\n", argv[2]); return 2; }
    Buf b(atoi(argv[3]), atoi(argv[4]), static_cast<uint16_t>(strtol(argv[5], nullptr, 16)));
    const std::string text = arg(argv, 7);
    drawText(b, 0, 0, *f, static_cast<uint16_t>(strtol(argv[6], nullptr, 16)), text.c_str());
    b.dumpHex();
    return 0;
  }
  // fit <font> <ширина> <utf8> — строка после fitText и её ширина
  if (argc >= 5 && !strcmp(argv[1], "fit")) {
    const GfxFont *f = fontByName(argv[2]);
    if (!f) { fprintf(stderr, "unknown font %s\n", argv[2]); return 2; }
    const std::string text = arg(argv, 4);
    char out[256];
    const int w = fitText(*f, text.c_str(), atoi(argv[3]), out, sizeof(out));
    printf("%d\n%s\n", w, out);
    return 0;
  }
  if (argc >= 5 && !strcmp(argv[1], "rrect")) {
    Buf b(atoi(argv[2]), atoi(argv[3]));
    fillRoundRect(b, 0, 0, b.w, b.h, atoi(argv[4]), 1);
    b.dump();
    return 0;
  }
  // stroke <w> <h> <r> <t> — рамка внутри скруглённого прямоугольника
  if (argc >= 6 && !strcmp(argv[1], "stroke")) {
    Buf b(atoi(argv[2]), atoi(argv[3]));
    strokeRoundRect(b, 0, 0, b.w, b.h, atoi(argv[4]), atoi(argv[5]), 1);
    b.dump();
    return 0;
  }
  fprintf(stderr, "usage: host_render text <font> <w> <h> <utf8>\n"
                  "       host_render aa <font> <w> <h> <bg565> <fg565> <utf8>\n"
                  "       host_render fit <font> <maxw> <utf8>\n"
                  "       host_render rrect <w> <h> <r>\n"
                  "       host_render stroke <w> <h> <r> <t>\n");
  return 2;
}
