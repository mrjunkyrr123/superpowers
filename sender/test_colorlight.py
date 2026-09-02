#!/usr/bin/env python3
"""Проверка структуры пакетов Colorlight против описания протокола.

Карты под рукой нет, поэтому проверяется то, что проверить можно: раскладка
байтов, разбиение длинных строк на пакеты, порядок пакетов в кадре и
корректность pcap. Ошибку в смещении поля это ловит, а вот «карта поняла кадр»
— уже нет, это проверяется только на железе.

    python3 sender/test_colorlight.py
"""

import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import colorlight as cl
import send

FAILED = []


def check(name, cond, detail=''):
    if cond:
        print(f'ok   {name}')
    else:
        print(f'ПРОВАЛ {name}  {detail}')
        FAILED.append(name)


def eq(name, got, want):
    check(name, got == want, f'получено {got!r}, ожидалось {want!r}')


def test_headers():
    p = cl.brightness_packet(200)
    eq('яркость: длина', len(p), cl.BRIGHT_SIZE)
    eq('яркость: MAC назначения', p[0:6], cl.DEST_MAC)
    eq('яркость: MAC источника', p[6:12], cl.SRC_MAC)
    eq('яркость: тип пакета', p[12], cl.TYPE_BRIGHTNESS)
    d = p[cl.DATA_OFFSET:]
    eq('яркость: три канала', (d[0], d[1], d[2]), (200, 200, 200))
    eq('яркость: data[3]', d[3], 0xFF)
    check('яркость: хвост нулевой', all(b == 0 for b in d[4:]))
    eq('яркость: обрезка сверху', cl.brightness_packet(999)[cl.DATA_OFFSET], 255)
    eq('яркость: обрезка снизу', cl.brightness_packet(-5)[cl.DATA_OFFSET], 0)

    s = cl.sync_packet(180)
    eq('sync: длина', len(s), cl.SYNC_SIZE)
    eq('sync: тип пакета', s[12], cl.TYPE_SYNC)
    d = s[cl.DATA_OFFSET:]
    eq('sync: data[0] = отправитель ПК', d[0], 0x07)
    eq('sync: data[22] яркость', d[22], 180)
    eq('sync: data[23]', d[23], 0x05)
    eq('sync: data[25..27] яркость', (d[25], d[26], d[27]), (180, 180, 180))

    dp = cl.discover_packet(3)
    eq('discover: длина', len(dp), cl.DISCOVER_SIZE)
    eq('discover: тип', dp[12], cl.TYPE_DISCOVER)
    eq('discover: номер карты', dp[cl.DATA_OFFSET + 3], 3)


def test_pixels():
    w, h = 160, 80
    rgb = bytes((x + y) % 256 for y in range(h) for x in range(w * 3))
    pkts = cl.pixel_packets(rgb, w, h)
    eq('строки: пакетов на кадр', len(pkts), h)

    p = pkts[0]
    eq('строка: длина пакета', len(p), cl.DATA_OFFSET + cl.PIXEL_HEADER_SIZE + w * 3)
    eq('строка: тип', p[12], cl.TYPE_PIXEL)
    d = p[cl.DATA_OFFSET:]
    eq('строка 0: номер', struct.unpack('>H', d[0:2])[0], 0)
    eq('строка 0: смещение', struct.unpack('>H', d[2:4])[0], 0)
    eq('строка 0: пикселей', struct.unpack('>H', d[4:6])[0], w)
    eq('строка: маркеры 08 88', (d[6], d[7]), (0x08, 0x88))
    eq('строка 0: данные', d[8:], rgb[0:w * 3])

    d79 = pkts[79][cl.DATA_OFFSET:]
    eq('строка 79: номер', struct.unpack('>H', d79[0:2])[0], 79)
    eq('строка 79: данные', d79[8:], rgb[79 * w * 3:80 * w * 3])

    # Строка шире одного пакета обязана делиться: 600*3 = 1800 > 1491.
    w2 = 600
    rgb2 = bytes(range(256)) * (w2 * 3 // 256 + 1)
    rgb2 = rgb2[:w2 * 3]
    split = cl.pixel_packets(rgb2, w2, 1)
    eq('широкая строка: пакетов', len(split), 2)
    a, b = (x[cl.DATA_OFFSET:] for x in split)
    eq('часть 1: смещение', struct.unpack('>H', a[2:4])[0], 0)
    eq('часть 1: пикселей', struct.unpack('>H', a[4:6])[0], cl.MAX_PIXELS_PER_PACKET)
    eq('часть 2: смещение', struct.unpack('>H', b[2:4])[0], cl.MAX_PIXELS_PER_PACKET)
    eq('часть 2: пикселей', struct.unpack('>H', b[4:6])[0], w2 - cl.MAX_PIXELS_PER_PACKET)
    eq('широкая строка: склейка', a[8:] + b[8:], rgb2)
    check('широкая строка: влезает в MTU', all(len(x) <= 1514 for x in split),
          f'максимум {max(len(x) for x in split)} Б')

    try:
        cl.pixel_packets(b'\x00' * 10, w, h)
        check('неверный размер кадра отвергается', False)
    except ValueError:
        check('неверный размер кадра отвергается', True)


def test_frame_order():
    rgb = bytes(160 * 80 * 3)
    pkts = cl.frame_packets(rgb, 160, 80, 100)
    eq('кадр: всего пакетов', len(pkts), 1 + 80 + 1)
    eq('кадр: первым яркость', pkts[0][12], cl.TYPE_BRIGHTNESS)
    eq('кадр: последним sync', pkts[-1][12], cl.TYPE_SYNC)
    check('кадр: середина — строки',
          all(p[12] == cl.TYPE_PIXEL for p in pkts[1:-1]))


def test_pcap():
    rgb = bytes(160 * 80 * 3)
    pkts = cl.frame_packets(rgb, 160, 80)
    path = os.path.join(tempfile.mkdtemp(), 'f.pcap')
    with cl.PcapSink(path) as s:
        s.send(pkts)

    raw = open(path, 'rb').read()
    magic, vmaj, vmin, tz, sf, snap, link = struct.unpack('<IHHiIII', raw[:24])
    eq('pcap: сигнатура', magic, 0xA1B2C3D4)
    eq('pcap: версия', (vmaj, vmin), (2, 4))
    eq('pcap: тип канала = Ethernet', link, 1)

    off, count = 24, 0
    while off < len(raw):
        _, _, incl, orig = struct.unpack('<IIII', raw[off:off + 16])
        eq(f'pcap: длины пакета {count}', incl, orig)
        eq(f'pcap: тело пакета {count}', raw[off + 16:off + 16 + incl], pkts[count])
        off += 16 + incl
        count += 1
    eq('pcap: число пакетов', count, len(pkts))


def test_patterns():
    w, h = 160, 80

    def px(buf, x, y):
        i = (y * w + x) * 3
        return tuple(buf[i:i + 3])

    for name in ('red', 'green', 'blue', 'white', 'rgb', 'corners',
                 'grid', 'gradient', 'rows'):
        buf = send.pattern(name, w, h)
        eq(f'таблица {name}: размер', len(buf), w * h * 3)

    eq('таблица red: пиксель', px(send.pattern('red', w, h), 5, 5), (255, 0, 0))

    bars = send.pattern('rgb', w, h)
    eq('таблица rgb: левая полоса красная', px(bars, 10, 40), (255, 0, 0))
    eq('таблица rgb: средняя зелёная', px(bars, 80, 40), (0, 255, 0))
    eq('таблица rgb: правая синяя', px(bars, 150, 40), (0, 0, 255))

    c = send.pattern('corners', w, h)
    eq('таблица corners: левый верх красный', px(c, 3, 3), (255, 0, 0))
    eq('таблица corners: правый верх зелёный', px(c, w - 3, 3), (0, 255, 0))
    eq('таблица corners: левый низ синий', px(c, 3, h - 3), (0, 0, 255))
    eq('таблица corners: правый низ жёлтый', px(c, w - 3, h - 3), (255, 255, 0))

    g = send.pattern('gradient', w, h)
    eq('таблица gradient: слева чёрный', px(g, 0, 0), (0, 0, 0))
    eq('таблица gradient: справа белый', px(g, w - 1, 0), (255, 255, 255))

    check('таблица corners детерминирована',
          send.pattern('corners', w, h) == send.pattern('corners', w, h))


def main():
    for t in (test_headers, test_pixels, test_frame_order, test_pcap, test_patterns):
        print(f'\n--- {t.__name__} ---')
        t()
    print()
    if FAILED:
        print(f'ПРОВАЛЕНО: {len(FAILED)} — {", ".join(FAILED)}')
        return 1
    print('Все проверки пройдены.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
