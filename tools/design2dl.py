#!/usr/bin/env python3
"""Макет дизайнера (layout.json из Figma, 08.11.2026) -> display list платы.

Нужен, чтобы увидеть макет так, как его нарисует плата (тот же рендер, тот же
шрифт без сглаживания Figma), и сверить вёрстку сервера с макетом.

    python tools/design2dl.py <layout.json> <out.json>          # все кадры
    python mock-server/render.py <out.json> -o out.png            # PNG по кадрам
    python tools/design2dl.py <layout.json> <dir> --png           # сразу PNG рядом с макетом

Соответствие элементов:
  rect -> rect (radius -> r), text/clock -> text/clock (overflow cut -> el),
  button -> button, line -> line, progress -> bar, marquee -> scroll,
  zone -> zone, 4 столбика wifi.bar* -> один wifi с st=bars.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def convert_element(e):
    t = e.get('type')
    if t == 'rect':
        L = {'t': 'rect', 'x': e['x'], 'y': e['y'], 'w': e['w'], 'h': e['h'],
             'c': e.get('fill', '#FFFFFF'), 'fill': True}
        if e.get('radius'):
            L['r'] = e['radius']
        return L
    if t in ('text', 'clock'):
        L = {'t': t, 'x': e['x'], 'y': e['y'], 'w': e.get('w', 0), 'f': e['font'],
             'c': e.get('color', '#FFFFFF'), 'a': (e.get('align') or 'left')[0], 's': e.get('text', '')}
        if e.get('overflow') == 'cut':
            L['el'] = True
        return L
    if t == 'button':
        return {'t': 'button', 'x': e['x'], 'y': e['y'], 'w': e['w'], 'h': e['h'],
                'r': e.get('radius', 12), 's': e.get('text', ''), 'f': e['font'],
                'bg': e.get('bg', '#212021'), 'c': e.get('color', '#FFFFFF')}
    if t == 'line':
        return {'t': 'line', 'x': e['x1'], 'y': e['y1'], 'x2': e['x2'], 'y2': e['y2'],
                'c': e.get('color', '#292829')}
    if t == 'progress':
        return {'t': 'bar', 'x': e['x'], 'y': e['y'], 'w': e['w'], 'h': e['h'],
                'p': e.get('value', 0), 'c': e.get('color', '#FFFFFF'),
                'bg': e.get('bg', '#212021'), 'r': e.get('radius', 0)}
    if t == 'marquee':
        return {'t': 'scroll', 'x': e['x'], 'y': e['y'], 'w': e['w'], 'f': e['font'],
                'c': e.get('color', '#FFFFFF'), 's': e.get('text', ''), 'sp': e.get('speed', 40)}
    if t == 'zone':
        return {'t': 'zone', 'x': e['x'], 'y': e['y'], 'w': e['w'], 'h': e['h'], 'r': 12,
                'id': e.get('action', ''), 'pr': 'frame'}
    return None


def convert_frame(fr):
    layers, wifi_done = [], False
    for e in fr['elements']:
        if str(e.get('bind', '')).startswith('wifi.bar'):
            if not wifi_done:
                layers.append({'t': 'wifi', 'st': 'bars', 'x': 386, 'y': 17,
                               'c': '#A5A6A5', 'bg': '#292829'})
                wifi_done = True
            continue
        L = convert_element(e)
        if L:
            layers.append(L)
    return {'id': fr['id'], 'layers': layers}


def main():
    src, out = sys.argv[1], sys.argv[2]
    with open(src, encoding='utf-8') as fh:
        frames = json.load(fh)['frames']
    dl = {'v': 1, 'w': 480, 'h': 480, 'pages': [convert_frame(f) for f in frames]}
    if '--png' not in sys.argv:
        with open(out, 'w', encoding='utf-8') as fh:
            json.dump(dl, fh, ensure_ascii=False)
        print(f'{len(dl["pages"])} кадров -> {out}')
        return
    sys.path.insert(0, os.path.join(ROOT, 'mock-server'))
    import render as R
    os.makedirs(out, exist_ok=True)
    for p in dl['pages']:
        cv = R.render_page(p, now='14:05', w=480, h=480, wifi=2)
        path = os.path.join(out, p['id'] + '.png')
        R.to_png(cv, path, scale=1)
        print(path)


if __name__ == '__main__':
    main()
