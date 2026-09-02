#!/usr/bin/env python3
"""Вывод картинки или тестовой таблицы на модуль через Colorlight 5A-75B.

    # есть ли карта в сети (первое, что стоит спросить)
    sudo python3 sender/send.py --iface eth0 --discover

    # тестовые таблицы для первого включения
    sudo python3 sender/send.py --iface eth0 --pattern corners
    sudo python3 sender/send.py --iface eth0 --pattern rgb --brightness 60

    # картинка
    sudo python3 sender/send.py --iface eth0 --image photo.jpg --fit cover

    # без карты и без root: записать кадры в pcap и посмотреть в Wireshark
    python3 sender/send.py --pcap out.pcap --pattern corners

Размер по умолчанию 160x80 — под модуль RX2.0-1515-160X80-40S.
Картинки требуют Pillow; тестовые таблицы работают без зависимостей.
"""

import argparse
import socket
import struct
import sys
import time

import colorlight as cl

RED = (255, 0, 0)
GREEN = (0, 255, 0)
BLUE = (0, 0, 255)
WHITE = (255, 255, 255)
YELLOW = (255, 255, 0)


class Frame:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.buf = bytearray(w * h * 3)

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 3
            self.buf[i:i + 3] = bytes(c)

    def fill(self, c):
        self.buf[:] = bytes(c) * (self.w * self.h)

    def rect(self, x0, y0, x1, y1, c):
        for y in range(y0, y1):
            for x in range(x0, x1):
                self.set(x, y, c)

    def border(self, c):
        for x in range(self.w):
            self.set(x, 0, c)
            self.set(x, self.h - 1, c)
        for y in range(self.h):
            self.set(0, y, c)
            self.set(self.w - 1, y, c)


def pattern(name, w, h):
    f = Frame(w, h)

    if name in ('red', 'green', 'blue', 'white'):
        f.fill({'red': RED, 'green': GREEN, 'blue': BLUE, 'white': WHITE}[name])

    elif name == 'rgb':
        # Три вертикальные полосы. Показывает порядок цветов: если полосы идут
        # не R-G-B слева направо, в настройке карты перепутан цветовой порядок.
        third = w // 3
        f.rect(0, 0, third, h, RED)
        f.rect(third, 0, third * 2, h, GREEN)
        f.rect(third * 2, 0, w, h, BLUE)

    elif name == 'corners':
        # Углы разного цвета. Мгновенно показывает поворот и зеркальность:
        # красный обязан быть слева сверху.
        f.border((40, 40, 40))
        s = min(w, h) // 4
        f.rect(1, 1, s, s, RED)
        f.rect(w - s, 1, w - 1, s, GREEN)
        f.rect(1, h - s, s, h - 1, BLUE)
        f.rect(w - s, h - s, w - 1, h - 1, YELLOW)

    elif name == 'grid':
        # Сетка 8x8. Разрывы и сдвиги строк выдают ошибку в схеме развёртки.
        f.border(WHITE)
        for x in range(0, w, 8):
            for y in range(h):
                f.set(x, y, (0, 90, 90))
        for y in range(0, h, 8):
            for x in range(w):
                f.set(x, y, (0, 90, 90))

    elif name == 'gradient':
        # Горизонтальный градиент. Полосы вместо плавного перехода —
        # недостаточная глубина цвета или неверная гамма.
        for x in range(w):
            v = round(255 * x / max(1, w - 1))
            for y in range(h):
                f.set(x, y, (v, v, v))

    elif name == 'rows':
        # Каждая строка своим оттенком: помогает найти перепутанные строки
        # при неверной развёртке.
        for y in range(h):
            v = round(255 * y / max(1, h - 1))
            for x in range(w):
                f.set(x, y, (v, 255 - v, 128))

    else:
        raise SystemExit(f'неизвестная таблица: {name}')

    return bytes(f.buf)


def load_image(path, w, h, fit):
    try:
        from PIL import Image
    except ImportError:
        raise SystemExit('для картинок нужен Pillow:  pip install pillow')

    img = Image.open(path).convert('RGB')
    if fit == 'stretch':
        img = img.resize((w, h), Image.LANCZOS)
    else:
        src_ar, dst_ar = img.width / img.height, w / h
        # contain — вписать целиком с полями, cover — заполнить с обрезкой
        wider = src_ar > dst_ar
        scale_by_width = wider if fit == 'contain' else not wider
        if scale_by_width:
            nw, nh = w, max(1, round(w / src_ar))
        else:
            nw, nh = max(1, round(h * src_ar)), h
        img = img.resize((nw, nh), Image.LANCZOS)
        canvas = Image.new('RGB', (w, h), (0, 0, 0))
        canvas.paste(img, ((w - nw) // 2, (h - nh) // 2))
        img = canvas
    return img.tobytes()


def discover(iface, timeout=2.0):
    """Шлёт 0x07 и слушает ответы 0x08. Печатает найденные карты."""
    sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
    sock.bind((iface, 0))
    sock.settimeout(0.3)
    sock.send(cl.discover_packet())

    seen = {}
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            pkt = sock.recv(2048)
        except socket.timeout:
            continue
        if len(pkt) < 100 or pkt[12] != 0x08:
            continue
        src = ':'.join(f'{b:02x}' for b in pkt[6:12])
        data = pkt[cl.DATA_OFFSET:]
        fw = f'{data[2]}.{data[3]}'
        card = data[85] if len(data) > 85 else 0
        seen[src] = (fw, card)
    sock.close()

    if not seen:
        print('карт не найдено.')
        print('проверьте: линк на порту, питание карты, что кабель в порт J1/вход,')
        print('и что интерфейс поднят:  sudo ip link set %s up' % iface)
        return 1
    for mac, (fw, card) in seen.items():
        print(f'найдена карта: MAC {mac}  прошивка {fw}  номер {card}')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iface', help='сетевой интерфейс к карте, например eth0')
    ap.add_argument('--pcap', help='записать кадры в файл вместо отправки')
    ap.add_argument('--image', help='файл картинки')
    ap.add_argument('--pattern', help='red green blue white rgb corners grid gradient rows')
    ap.add_argument('--discover', action='store_true', help='найти карты в сети')
    ap.add_argument('--width', type=int, default=160)
    ap.add_argument('--height', type=int, default=80)
    ap.add_argument('--brightness', type=int, default=140, help='0..255, по умолчанию 140')
    ap.add_argument('--fit', choices=('contain', 'cover', 'stretch'), default='contain')
    ap.add_argument('--loop', type=float, metavar='СЕК',
                    help='повторять отправку с этим интервалом, Ctrl+C для выхода')
    args = ap.parse_args()

    if args.discover:
        if not args.iface:
            raise SystemExit('--discover требует --iface')
        return discover(args.iface)

    if not args.image and not args.pattern:
        raise SystemExit('нужен --image, --pattern или --discover')
    if not args.iface and not args.pcap:
        raise SystemExit('нужен --iface (отправка) или --pcap (запись в файл)')

    rgb = (load_image(args.image, args.width, args.height, args.fit)
           if args.image else pattern(args.pattern, args.width, args.height))

    packets = cl.frame_packets(rgb, args.width, args.height, args.brightness)
    sink = cl.PcapSink(args.pcap) if args.pcap else cl.RawSink(args.iface)

    with sink:
        sink.send(packets)
        n = 1
        try:
            while args.loop:
                time.sleep(args.loop)
                sink.send(packets)
                n += 1
        except KeyboardInterrupt:
            pass

    total = sum(len(p) for p in packets)
    where = args.pcap if args.pcap else args.iface
    print(f'кадр {args.width}x{args.height}: {len(packets)} пакетов, {total} Б '
          f'-> {where}' + (f' (повторов: {n})' if n > 1 else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
