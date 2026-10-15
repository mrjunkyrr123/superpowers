#!/usr/bin/env python3
"""Эталонный рендерер display list — то же, что делает прошивка.

Нужен, чтобы верстать страницы, не имея под рукой панели: рисует ту же
картинку теми же шрифтами (``fonts.json`` генерируется вместе с прошивочными
``fonts.cpp``) и выводит её в терминал или в PNG.

    python3 mock-server/render.py sample.json             # ASCII в терминал
    python3 mock-server/render.py sample.json -o out.png  # PNG, масштаб x8

Размер холста берётся из полей "w"/"h" самого display list, иначе 64x32.

Зависимостей нет — PNG пишется через stdlib.
"""

import json
import math
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
PANEL_W, PANEL_H = 64, 32

with open(os.path.join(HERE, 'fonts.json'), encoding='utf-8') as _fh:
    FONTS = json.load(_fh)


def parse_color(s, default=(255, 255, 255)):
    if not isinstance(s, str) or not s.startswith('#') or len(s) != 7:
        return default
    try:
        return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))
    except ValueError:
        return default


class Canvas:
    def __init__(self, w=PANEL_W, h=PANEL_H):
        self.w, self.h = w, h
        self.px = [[(0, 0, 0)] * w for _ in range(h)]

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def fill(self, c):
        self.px = [[c] * self.w for _ in range(self.h)]

    def rect(self, x, y, w, h, c, filled=False):
        if filled:
            for yy in range(y, y + h):
                for xx in range(x, x + w):
                    self.set(xx, yy, c)
        else:
            for xx in range(x, x + w):
                self.set(xx, y, c)
                self.set(xx, y + h - 1, c)
            for yy in range(y, y + h):
                self.set(x, yy, c)
                self.set(x + w - 1, yy, c)

    def round_rect(self, x, y, w, h, r, c):
        """Скруглённый прямоугольник. Целочисленный корень — чтобы растр
        совпадал с прошивкой бит в бит (см. fillRoundRect в gfx.cpp)."""
        if w <= 0 or h <= 0:
            return
        r = max(0, min(r, min(w, h) // 2))
        for row in range(h):
            if r <= 0:
                inset = 0
            elif row < r:
                inset = r - math.isqrt(r * r - (r - 1 - row) ** 2)
            elif row >= h - r:
                inset = r - math.isqrt(r * r - (row - (h - r)) ** 2)
            else:
                inset = 0
            for col in range(inset, w - inset):
                self.set(x + col, y + row, c)

    def line(self, x0, y0, x1, y1, c):
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            self.set(x0, y0, c)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy


def text_width(font_id, s):
    f = FONTS.get(font_id)
    if not f:
        return 0
    g = f['glyphs']
    return sum(g[str(ord(ch))]['adv'] for ch in s if str(ord(ch)) in g)


def draw_text(cv, x, y, font_id, color, s, clip=None):
    """y — верх строки. clip — (x0, x1) для обрезки по горизонтали."""
    f = FONTS.get(font_id)
    if not f:
        return 0
    base = y + f['baseline']
    cur = x
    for ch in s:
        g = f['glyphs'].get(str(ord(ch)))
        if g is None:
            g = f['glyphs'].get(str(ord('?')))
            if g is None:
                continue
        for ry, row in enumerate(g['bits']):
            for rx, bit in enumerate(row):
                if bit == '1':
                    px, py = cur + g['dx'] + rx, base + g['dy'] + ry
                    if clip and not (clip[0] <= px < clip[1]):
                        continue
                    cv.set(px, py, color)
        cur += g['adv']
    return cur - x


def _align_x(align, x, box_w, text_w, panel_w):
    if align == 'c':
        return x + (box_w - text_w) // 2 if box_w else (panel_w - text_w) // 2
    if align == 'r':
        return x + box_w - text_w if box_w else panel_w - text_w
    return x


def render_page(page, t=0.0, now='12:00', w=PANEL_W, h=PANEL_H, pressed=None):
    """page — один элемент массива pages. t — секунды с начала показа."""
    cv = Canvas(w, h)
    for L in page.get('layers', []):
        kind = L.get('t')
        c = parse_color(L.get('c'))
        if kind == 'fill':
            cv.fill(c)
        elif kind == 'rect':
            cv.rect(L['x'], L['y'], L['w'], L['h'], c, L.get('fill', False))
        elif kind == 'line':
            cv.line(L['x'], L['y'], L['x2'], L['y2'], c)
        elif kind in ('text', 'clock'):
            s = now if kind == 'clock' else str(L.get('s', ''))
            fid = L.get('f', 'f5')
            tw = text_width(fid, s)
            x = _align_x(L.get('a', 'l'), L.get('x', 0), L.get('w', 0), tw, w)
            draw_text(cv, x, L.get('y', 0), fid, c, s)
        elif kind == 'scroll':
            s = str(L.get('s', ''))
            fid = L.get('f', 'f5')
            x0, bw = L.get('x', 0), L.get('w', w)
            tw = text_width(fid, s)
            if tw <= bw:
                draw_text(cv, x0, L.get('y', 0), fid, c, s, clip=(x0, x0 + bw))
            else:
                gap = 8
                period = tw + gap
                off = int(t * L.get('sp', 16)) % period
                for base in (x0 - off, x0 - off + period):
                    draw_text(cv, base, L.get('y', 0), fid, c, s,
                              clip=(x0, x0 + bw))
        elif kind == 'button':
            bw, bh = L.get('w', 0), L.get('h', 0)
            fid = L.get('f', 'b24')
            bg = parse_color(L.get('bg'), (33, 36, 41))
            fg = c
            if pressed and pressed == L.get('id'):
                bg, fg = fg, bg
            cv.round_rect(L.get('x', 0), L.get('y', 0), bw, bh, L.get('r', 8), bg)
            label = str(L.get('s', ''))
            ya = FONTS.get(fid, {}).get('yAdvance', 0)
            draw_text(cv, L.get('x', 0) + (bw - text_width(fid, label)) // 2,
                      L.get('y', 0) + (bh - ya) // 2, fid, fg, label)

        elif kind == 'bar':
            x, y = L.get('x', 0), L.get('y', 0)
            bw, bh = L.get('w', w), L.get('h', 3)
            bg = parse_color(L.get('bg'), (24, 24, 24))
            cv.rect(x, y, bw, bh, bg, filled=True)
            p = max(0.0, min(1.0, float(L.get('p', 0))))
            if p > 0:
                cv.rect(x, y, max(1, round(bw * p)), bh, c, filled=True)
    return cv


# Градации яркости: без них тусклый фон прогресс-бара неотличим от заливки.
_RAMP = ((8, '  '), (64, '░░'), (140, '▒▒'), (256, '██'))


def hit_test(page, x, y):
    """Кнопка под точкой касания, или None. Та же логика, что hitTest в прошивке."""
    for L in page.get('layers', []):
        if L.get('t') != 'button':
            continue
        bx, by = L.get('x', 0), L.get('y', 0)
        bw, bh = L.get('w', 0), L.get('h', 0)
        if bx <= x < bx + bw and by <= y < by + bh:
            action = L.get('action') or {}
            if not action.get('path'):
                return None
            return {'id': L.get('id', ''),
                    'method': action.get('method', 'POST'),
                    'path': action['path']}
    return None


def to_ascii(cv):
    """Каждый пиксель — два символа, чтобы пропорции не плыли в терминале."""
    out = ['+' + '-' * (cv.w * 2) + '+']
    for row in cv.px:
        cells = []
        for r, g, b in row:
            lum = (r * 30 + g * 59 + b * 11) // 100
            cells.append(next(ch for lim, ch in _RAMP if lum < lim))
        out.append('|' + ''.join(cells) + '|')
    out.append('+' + '-' * (cv.w * 2) + '+')
    return '\n'.join(out)


def to_png(cv, path, scale=8):
    w, h = cv.w * scale, cv.h * scale
    raw = bytearray()
    for row in cv.px:
        line = bytearray([0])
        for p in row:
            line += bytes(p) * scale
        raw += line * scale

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b'\x89PNG\r\n\x1a\n' +
           chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
           chunk(b'IDAT', zlib.compress(bytes(raw), 9)) +
           chunk(b'IEND', b''))
    with open(path, 'wb') as fh:
        fh.write(png)


def main():
    args = [a for a in sys.argv[1:]]
    out = None
    if '-o' in args:
        i = args.index('-o')
        out = args[i + 1]
        del args[i:i + 2]
    src = args[0] if args else os.path.join(HERE, 'sample.json')
    with open(src, encoding='utf-8') as fh:
        doc = json.load(fh)
    w = doc.get('w', PANEL_W)
    h = doc.get('h', PANEL_H)
    pages = doc.get('pages', [doc])
    for i, page in enumerate(pages):
        cv = render_page(page, w=w, h=h)
        if out:
            path = out if len(pages) == 1 else out.replace('.png', f'-{i + 1}.png')
            to_png(cv, path)
            print(f'страница {i + 1} -> {path}')
        else:
            print(f'--- страница {i + 1} (dwell={page.get("dwell", "?")}с) ---')
            print(to_ascii(cv))


if __name__ == '__main__':
    main()
