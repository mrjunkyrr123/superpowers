// Хостовая сборка слоя отрисовки: тот же gfx.cpp/fonts.cpp, что и в прошивке,
// но без панели. Печатает растр в stdout, чтобы сверить его с эталонным
// рендерером на Python (mock-server/render.py) — см. test/compare.py.
//
// Сборка: c++ -std=c++11 -I../src host_render.cpp ../src/gfx.cpp ../src/fonts.cpp
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
};

int main(int argc, char **argv) {
  if (argc < 5) {
    fprintf(stderr, "usage: host_render <font> <w> <h> <utf8-text>\n");
    return 2;
  }
  const GfxFont *f = fontByName(argv[1]);
  if (!f) { fprintf(stderr, "unknown font %s\n", argv[1]); return 2; }
  const int w = atoi(argv[2]), h = atoi(argv[3]);
  Buf b(w, h);
  drawText(b, 0, 0, *f, 1, argv[4]);
  printf("width=%d\n", textWidth(*f, argv[4]));
  for (int y = 0; y < h; ++y) {
    fwrite(&b.p[y * w], 1, w, stdout);
    putchar('\n');
  }
  return 0;
}
