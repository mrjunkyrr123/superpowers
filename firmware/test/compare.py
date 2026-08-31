#!/usr/bin/env python3
"""Сверяет отрисовку прошивки с эталонным рендерером.

Собирает gfx.cpp/fonts.cpp на хосте, гоняет через них набор строк и сравнивает
результат пиксель в пиксель с mock-server/render.py. Расхождение означает, что
табло покажет не то, что показало превью, — а это и есть та ошибка, которую
дороже всего ловить на смонтированном железе.

    python3 firmware/test/compare.py
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'mock-server'))
import render as R  # noqa: E402

BIN = os.path.join(HERE, 'host_render')

CASES = [
    ('f5',  'Свободно 7 из 24'),
    ('f5',  'АБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ'),
    ('f5',  'abcdefghijklmnopqrstuvwxyz'),
    ('f5',  'ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789'),
    ('f5',  '!"#$%&\'()*+,-./:;<=>?@[]^_`{|}~'),
    ('f5',  'ПЕРЕГОВОРКА А ЗАНЯТА ДО 14:30'),
    ('r12', 'Свободно 7'),
    ('r12', 'ЖЩЫЮЭ ёЁ'),
    ('b13', 'Переговорка'),
    ('b16', '14:30'),
    ('b16', 'Привет, мир!'),
    ('f5',  ''),
    ('f5',  'x' * 60),
]


def build():
    src = os.path.join(ROOT, 'firmware', 'src')
    cmd = ['c++', '-std=c++11', '-O1', '-Wall', '-Wextra', '-I', src,
           os.path.join(HERE, 'host_render.cpp'),
           os.path.join(src, 'gfx.cpp'), os.path.join(src, 'fonts.cpp'),
           '-o', BIN]
    subprocess.run(cmd, check=True)


def firmware_render(font, text, w, h):
    out = subprocess.run([BIN, font, str(w), str(h), text],
                         check=True, capture_output=True, text=True).stdout
    lines = out.split('\n')
    width = int(lines[0].split('=')[1])
    return width, [ln for ln in lines[1:1 + h]]


def reference_render(font, text, w, h):
    cv = R.Canvas(w, h)
    R.draw_text(cv, 0, 0, font, (255, 255, 255), text)
    grid = [''.join('#' if p != (0, 0, 0) else '.' for p in row) for row in cv.px]
    return R.text_width(font, text), grid


def main():
    build()
    W, H = 320, 24
    bad = 0
    for font, text in CASES:
        fw, fgrid = firmware_render(font, text, W, H)
        rw, rgrid = reference_render(font, text, W, H)
        label = f'{font} {text[:28]!r}'
        if fw != rw:
            print(f'РАСХОЖДЕНИЕ ширины  {label}: прошивка={fw} эталон={rw}')
            bad += 1
            continue
        diff = [i for i in range(H) if fgrid[i] != rgrid[i]]
        if diff:
            print(f'РАСХОЖДЕНИЕ растра  {label}: строки {diff[:5]}')
            print('  прошивка:', fgrid[diff[0]][:70])
            print('  эталон:  ', rgrid[diff[0]][:70])
            bad += 1
        else:
            print(f'ok  {label}  ширина={fw}px')
    print()
    if bad:
        print(f'ПРОВАЛЕНО: {bad} из {len(CASES)}')
        return 1
    print(f'Все {len(CASES)} проверок пройдены: прошивка и превью рисуют одинаково.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
