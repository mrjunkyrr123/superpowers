#!/usr/bin/env python3
"""Сверяет отрисовку прошивки с эталонным рендерером.

Собирает gfx.cpp/fonts.cpp на хосте, гоняет через них набор строк и сравнивает
результат пиксель в пиксель с mock-server/render.py. Расхождение означает, что
табло покажет не то, что показало превью, — а это и есть та ошибка, которую
дороже всего ловить на смонтированном железе.

    python3 firmware/test/compare.py
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'mock-server'))
import render as R  # noqa: E402

BIN = os.path.join(HERE, 'host_render')

RRECT_CASES = [
    (200, 60, 12), (100, 100, 50), (60, 60, 0), (40, 20, 30),
    (3, 3, 1), (480, 80, 16), (17, 9, 4), (1, 1, 0), (72, 72, 36),
]

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
    ('b24', 'Занять на 30'),
    ('b32', 'КОМНАТА А'),
    ('b48', '7'),
    ('r20', 'Обновить сейчас'),
    ('r28', 'из 24 свободно'),
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
    out = subprocess.run([BIN, 'text', font, str(w), str(h), text],
                         check=True, capture_output=True, text=True).stdout
    lines = out.split('\n')
    width = int(lines[0].split('=')[1])
    return width, [ln for ln in lines[1:1 + h]]


def reference_render(font, text, w, h):
    cv = R.Canvas(w, h)
    R.draw_text(cv, 0, 0, font, (255, 255, 255), text)
    grid = [''.join('#' if p != (0, 0, 0) else '.' for p in row) for row in cv.px]
    return R.text_width(font, text), grid


def firmware_rrect(w, h, r):
    out = subprocess.run([BIN, 'rrect', str(w), str(h), str(r)],
                         check=True, capture_output=True, text=True).stdout
    return out.split('\n')[:h]


def reference_rrect(w, h, r):
    cv = R.Canvas(w, h)
    cv.round_rect(0, 0, w, h, r, (255, 255, 255))
    return [''.join('#' if p != (0, 0, 0) else '.' for p in row) for row in cv.px]


def check_round_rects(counter):
    """Скруглённые прямоугольники — основа кнопок. Смещение считается
    целочисленным корнем именно затем, чтобы растр совпадал бит в бит."""
    bad = 0
    for w, h, r in RRECT_CASES:
        counter[0] += 1
        fw, ref = firmware_rrect(w, h, r), reference_rrect(w, h, r)
        if fw == ref:
            print(f'ok  скруглённый {w}x{h} r={r}')
        else:
            diff = [i for i in range(h) if fw[i] != ref[i]]
            print(f'РАСХОЖДЕНИЕ скруглённый {w}x{h} r={r}: строки {diff[:4]}')
            bad += 1
    return bad


def check_hit_tests(counter):
    """Границы кнопок: четыре угла внутри, по пикселю наружу — снаружи."""
    path = os.path.join(ROOT, 'mock-server', 'sample-480x480.json')
    with open(path, encoding='utf-8') as fh:
        page = json.load(fh)['pages'][0]
    buttons = [L for L in page['layers'] if L.get('t') == 'button']
    if not buttons:
        print('РАСХОЖДЕНИЕ: в образце нет ни одной кнопки')
        return 1

    bad = 0
    for b in buttons:
        counter[0] += 1
        x, y, w, h, bid = b['x'], b['y'], b['w'], b['h'], b['id']
        for cx, cy in ((x, y), (x + w - 1, y), (x, y + h - 1), (x + w - 1, y + h - 1)):
            got = R.hit_test(page, cx, cy)
            if not got or got['id'] != bid:
                print(f'РАСХОЖДЕНИЕ {bid}: угол ({cx},{cy}) не попал в кнопку')
                bad += 1
        for ox, oy in ((x - 1, y + h // 2), (x + w, y + h // 2),
                       (x + w // 2, y - 1), (x + w // 2, y + h)):
            got = R.hit_test(page, ox, oy)
            if got and got['id'] == bid:
                print(f'РАСХОЖДЕНИЕ {bid}: точка ({ox},{oy}) снаружи, но засчитана')
                bad += 1
        p = b['action']['path']
        if not p.startswith('/') or '://' in p:
            print(f'РАСХОЖДЕНИЕ {bid}: путь {p!r} обязан быть относительным')
            bad += 1
        if not bad:
            print(f'ok  кнопка {bid}: x {x}..{x + w - 1}, y {y}..{y + h - 1}, путь {p}')

    for px, py in ((0, 0), (479, 0), (240, 200), (240, 300)):
        if R.hit_test(page, px, py):
            print(f'РАСХОЖДЕНИЕ: пустая точка ({px},{py}) попала в кнопку')
            bad += 1
    return bad


def _c565(hexstr):
    """'#RRGGBB' -> (int RGB565, кортеж RGB888 как у превью)."""
    r, g, b = R.parse_color(hexstr)
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return v, (r, g, b)


def _canvas565(cv):
    return [''.join('%04X' % (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)) for r, g, b in row)
            for row in cv.px]


# Сглаженный шрифт макета 08.11: точный цвет каждой точки на разных фонах.
AA_CASES = [
    ('m14', '#000000', '#A5A6A5', 'просрочено 3 дн · важно'),
    ('m16', '#101010', '#FFFFFF', 'Сеть: RecoRoom-Office'),
    ('b16', '#EFE3BD', '#101010', 'Лампа включено'),
    ('b20', '#391010', '#FFFFFF', 'Самара — нет связи'),
    ('b28', '#000000', '#F7AE10', 'Нет сети…'),
    ('b48', '#000000', '#FFFFFF', '38 420 ₽'),
    ('b16', '#F7AE10', '#101010', 'Нет связи · данные от 13:58 ✓ № «»−'),
]

FIT_CASES = [
    ('b16', 200, 'Подписать договор с аэропортом Кольцово'),
    ('b16', 400, 'Коротко'),
    ('m14', 120, 'Очень длинная задача про переговоры'),
    ('b13', 100, 'Старый шрифт без многоточия'),
    ('b20', 10, 'Узко'),
]

STROKE_CASES = [(224, 56, 12, 2), (64, 64, 32, 2), (40, 20, 12, 2), (6, 6, 2, 3), (100, 40, 0, 1)]


def check_aa(counter):
    bad = 0
    for font, bg, fg, text in AA_CASES:
        counter[0] += 1
        ya = R.FONTS[font]['yAdvance']
        w = R.text_width(font, text) + 4
        bgv, bgt = _c565(bg)
        fgv, fgt = _c565(fg)
        out = subprocess.run([BIN, 'aa', font, str(w), str(ya), '%04X' % bgv, '%04X' % fgv, text],
                             check=True, capture_output=True, text=True).stdout.split('\n')[:ya]
        cv = R.Canvas(w, ya)
        cv.fill(R.blend565(bgt, bgt, 15))
        cv.px = [[R._from565(*R._to565(p)) for p in row] for row in cv.px]
        R.draw_text(cv, 0, 0, font, fgt, text)
        ref = _canvas565(cv)
        diff = [i for i in range(ya) if out[i] != ref[i]]
        if diff:
            print(f'РАСХОЖДЕНИЕ сглаживания {font} {text!r}: строки {diff[:5]}')
            bad += 1
        else:
            print(f'ok  сглаживание {font} {bg}->{fg} {text[:24]!r}')
    return bad


def check_fit(counter):
    bad = 0
    for font, maxw, text in FIT_CASES:
        counter[0] += 1
        out = subprocess.run([BIN, 'fit', font, str(maxw), text], check=True,
                             capture_output=True, encoding='utf-8').stdout.split('\n')
        fw, fs = int(out[0]), out[1]
        rs, rw = R.fit_text(font, text, maxw)
        if (fw, fs) != (rw, rs):
            print(f'РАСХОЖДЕНИЕ «…» {font} {maxw}: прошивка {fs!r} {fw}, эталон {rs!r} {rw}')
            bad += 1
        elif rw > maxw and len(text) > 1 and maxw >= R.text_width(font, '…'):
            print(f'РАСХОЖДЕНИЕ «…» {font} {maxw}: {rs!r} шире места ({rw})')
            bad += 1
        else:
            print(f'ok  «…» {font} до {maxw}px: {rs!r} ({rw}px)')
    return bad


def check_strokes(counter):
    bad = 0
    for w, h, r, t in STROKE_CASES:
        counter[0] += 1
        out = subprocess.run([BIN, 'stroke', str(w), str(h), str(r), str(t)], check=True,
                             capture_output=True, text=True).stdout.split('\n')[:h]
        cv = R.Canvas(w, h)
        cv.stroke_round_rect(0, 0, w, h, r, t, (255, 255, 255))
        ref = [''.join('#' if p != (0, 0, 0) else '.' for p in row) for row in cv.px]
        if out != ref:
            print(f'РАСХОЖДЕНИЕ рамка {w}x{h} r={r} t={t}')
            bad += 1
        else:
            print(f'ok  рамка {w}x{h} r={r} t={t}')
    return bad


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
    total = [len(CASES)]
    bad += check_round_rects(total)
    bad += check_hit_tests(total)
    bad += check_aa(total)
    bad += check_fit(total)
    bad += check_strokes(total)

    print()
    if bad:
        print(f'ПРОВАЛЕНО: {bad} из {total[0]}')
        return 1
    print(f'Все {total[0]} проверок пройдены: прошивка и превью рисуют одинаково.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
