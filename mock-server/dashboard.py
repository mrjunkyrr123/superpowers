#!/usr/bin/env python3
"""Переводит макет дизайнера (rr_md/kb/бренд/design/esp-dashboard/layout.json)
в display list прошивки.

Формат дизайнера описательный (type/text/font/color, progress, marquee,
x1/y1 у линий), прошивка понимает короткий (t/s/f/c, bar, scroll, x/y).
Поле bind пока пропускается: цифры остаются примерными из макета. Подстановку
реальных данных делает сервер, когда появится источник.

    python mock-server/dashboard.py <layout.json> -o dashboard-480x480.json
"""
import json
import sys

ALIGN = {'left': 'l', 'center': 'c', 'right': 'r'}


def convert_element(e):
    t = e['type']
    if t == 'rect':
        return {'t': 'rect', 'x': e['x'], 'y': e['y'], 'w': e['w'], 'h': e['h'],
                'c': e['fill'], 'fill': True}
    if t == 'line':
        return {'t': 'line', 'x': e['x1'], 'y': e['y1'], 'x2': e['x2'], 'y2': e['y2'],
                'c': e['color']}
    if t in ('text', 'clock', 'marquee'):
        out = {'t': {'text': 'text', 'clock': 'clock', 'marquee': 'scroll'}[t],
               'x': e['x'], 'y': e['y'], 'w': e['w'], 'f': e['font'], 'c': e['color']}
        if t != 'clock':
            out['s'] = e['text']
        if t == 'marquee':
            out['sp'] = e.get('speed', 30)
        elif e.get('align', 'left') != 'left':
            out['a'] = ALIGN[e['align']]
        return out
    if t == 'progress':
        return {'t': 'bar', 'x': e['x'], 'y': e['y'], 'w': e['w'], 'h': e['h'],
                'p': e['value'], 'c': e['color'], 'bg': e['bg']}
    raise ValueError(f'неизвестный тип элемента: {t}')


def convert(layout, poll_interval=60, brightness=70):
    return {
        'v': 1,
        'w': layout['canvas']['w'],
        'h': layout['canvas']['h'],
        'poll_interval': poll_interval,
        'brightness': brightness,
        'pages': [{'id': s['id'], 'dwell': s['seconds'],
                   'layers': [convert_element(e) for e in s['elements']]}
                  for s in layout['screens']],
    }


def main():
    args = sys.argv[1:]
    out = None
    if '-o' in args:
        i = args.index('-o')
        out = args[i + 1]
        del args[i:i + 2]
    with open(args[0], encoding='utf-8') as fh:
        doc = convert(json.load(fh))
    text = json.dumps(doc, ensure_ascii=False, separators=(',', ':'))
    if out:
        with open(out, 'w', encoding='utf-8') as fh:
            fh.write(text)
        print(f'{len(doc["pages"])} экранов, {len(text.encode())} байт -> {out}')
    else:
        print(text)


if __name__ == '__main__':
    main()
