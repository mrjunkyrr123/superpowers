// Хостовая сборка слоя отрисовки: тот же gfx.cpp/fonts.cpp, что и в прошивке,
// но без панели. Печатает растр в stdout, чтобы сверить его с эталонным
// рендерером на Python (mock-server/render.py) — см. test/compare.py.
//
//   host_render text  <font> <w> <h> <utf8>   текст
//   host_render rrect <w> <h> <r>             скруглённый прямоугольник
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "gfx.h"

struct Buf : PixelSink {
  int w, h;
  std::vector<char> p;
  Buf(int w_, int h_) : w(w_), h(h_), p(w_ * h_, '.') {}
  void px(int16_t x, int16_t y, uint16_t) override {
    if (x >= 0 && y >= 0 && x < w && y < h) p[y * w + x] = '#';
  }
  void dump() const {
    for (int y = 0; y < h; ++y) { fwrite(&p[y * w], 1, w, stdout); putchar('\n'); }
  }
};

int main(int argc, char **argv) {
  if (argc >= 6 && !strcmp(argv[1], "text")) {
    const GfxFont *f = fontByName(argv[2]);
    if (!f) { fprintf(stderr, "unknown font %s\n", argv[2]); return 2; }
    Buf b(atoi(argv[3]), atoi(argv[4]));
    drawText(b, 0, 0, *f, 1, argv[5]);
    printf("width=%d\n", textWidth(*f, argv[5]));
    b.dump();
    return 0;
  }
  if (argc >= 5 && !strcmp(argv[1], "rrect")) {
    Buf b(atoi(argv[2]), atoi(argv[3]));
    fillRoundRect(b, 0, 0, b.w, b.h, atoi(argv[4]), 1);
    b.dump();
    return 0;
  }
  fprintf(stderr, "usage: host_render text <font> <w> <h> <utf8>\n"
                  "       host_render rrect <w> <h> <r>\n");
  return 2;
}
