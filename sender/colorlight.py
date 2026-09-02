#!/usr/bin/env python3
"""Отправитель кадров на приёмную карту Colorlight 5A-75B / 5A-75E.

Протокол закрытый, но разобран сообществом. Реализация здесь написана по
описанию формата пакетов из Falcon Player (комментарий в
``src/channeloutput/ColorLight-5a-75.cpp``) — там он задокументирован
подробнее всего. Код FPP распространяется под GPL; здесь по его описанию
написана независимая реализация, но если будете заимствовать оттуда куски
кода — лицензия поедет за ними.

Другие источники по протоколу:
  https://hkubota.wordpress.com/2022/01/31/winter-project-colorlight-5a-75b-protocol/
  https://github.com/q3k/chubby75

Формат кадра
------------
Обычный кадр Ethernet без IP. Байт 12 — тип пакета, данные идут с байта 13,
то есть первый байт данных занимает младший байт поля ethertype.

  0x0A  яркость          77 байт
  0x55  строка пикселей  21 байт заголовка + ширина*3
  0x01  sync             112 байт, защёлкивает кадр на экране

Порядок на кадр: яркость → все строки → sync.

Зависимостей нет. Для отправки в сеть нужен root или CAP_NET_RAW.
"""

import socket
import struct
import time

DEST_MAC = bytes.fromhex('112233445566')
SRC_MAC = bytes.fromhex('222233445566')

TYPE_SYNC = 0x01
TYPE_DISCOVER = 0x07
TYPE_BRIGHTNESS = 0x0A
TYPE_PIXEL = 0x55

DATA_OFFSET = 13          # 6 dest + 6 src + 1 байт типа
SYNC_SIZE = 112
BRIGHT_SIZE = 77
DISCOVER_SIZE = 284
PIXEL_HEADER_SIZE = 8

# Максимум пикселей в одном пакете: 497*3 + 21 = 1512 байт, влезает в MTU 1500
# вместе с 14-байтовым заголовком Ethernet.
MAX_PIXELS_PER_PACKET = 497
MAX_BYTES_PER_PACKET = MAX_PIXELS_PER_PACKET * 3


def _eth(packet_type, data, total_size=None):
    """Собирает кадр Ethernet. total_size — добить нулями до этой длины."""
    buf = bytearray(DEST_MAC)
    buf += SRC_MAC
    buf.append(packet_type)
    buf += data
    if total_size is not None:
        if len(buf) > total_size:
            raise ValueError(f'пакет {len(buf)} Б длиннее заявленных {total_size} Б')
        buf += bytes(total_size - len(buf))
    return bytes(buf)


def brightness_packet(brightness):
    """0x0A — яркость, 0..255."""
    data = bytearray(BRIGHT_SIZE - DATA_OFFSET)
    b = max(0, min(255, int(brightness)))
    data[0] = data[1] = data[2] = b
    data[3] = 0xFF
    return _eth(TYPE_BRIGHTNESS, data, BRIGHT_SIZE)


def sync_packet(brightness):
    """0x01 — защёлка кадра. Значение 0x07 в data[0] — «отправитель это ПК»."""
    data = bytearray(SYNC_SIZE - DATA_OFFSET)
    b = max(0, min(255, int(brightness)))
    data[0] = 0x07
    data[22] = b
    data[23] = 0x05
    data[25] = data[26] = data[27] = b
    return _eth(TYPE_SYNC, data, SYNC_SIZE)


def discover_packet(receiver=0):
    """0x07 — опрос приёмных карт в сети."""
    data = bytearray(DISCOVER_SIZE - DATA_OFFSET)
    data[3] = receiver
    return _eth(TYPE_DISCOVER, data, DISCOVER_SIZE)


def pixel_packets(rgb, width, height):
    """0x55 — данные строк. rgb — width*height*3 байт, порядок R,G,B."""
    expected = width * height * 3
    if len(rgb) != expected:
        raise ValueError(f'ожидалось {expected} Б кадра, получено {len(rgb)}')

    row_size = width * 3
    out = []
    for row in range(height):
        base = row * row_size
        offset = 0
        while offset < row_size:
            n = min(MAX_BYTES_PER_PACKET, row_size - offset)
            head = bytearray(PIXEL_HEADER_SIZE)
            struct.pack_into('>HHH', head, 0, row, offset // 3, n // 3)
            head[6] = 0x08          # назначение неизвестно, LEDVISION шлёт так же
            head[7] = 0x88
            out.append(_eth(TYPE_PIXEL, bytes(head) + rgb[base + offset:base + offset + n]))
            offset += n
    return out


def frame_packets(rgb, width, height, brightness=200):
    """Полный кадр: яркость, строки, sync."""
    return ([brightness_packet(brightness)] +
            pixel_packets(rgb, width, height) +
            [sync_packet(brightness)])


class RawSink:
    """Отправка в сеть. Нужен root или CAP_NET_RAW на процессе."""

    def __init__(self, iface):
        self.iface = iface
        self.sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
        self.sock.bind((iface, 0))

    def send(self, packets):
        for p in packets:
            self.sock.send(p)

    def close(self):
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


class PcapSink:
    """Запись кадров в .pcap вместо отправки.

    Нужна, чтобы проверить структуру пакетов без карты и без root: открыть
    файл в Wireshark и посмотреть, что уходит. При разборе «карта не реагирует»
    это первое, с чего стоит начать.
    """

    def __init__(self, path):
        self.fh = open(path, 'wb')
        # Глобальный заголовок pcap: magic, версия 2.4, snaplen, LINKTYPE_ETHERNET
        self.fh.write(struct.pack('<IHHiIII', 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))

    def send(self, packets):
        for p in packets:
            t = time.time()
            self.fh.write(struct.pack('<IIII', int(t), int(t % 1 * 1e6),
                                      len(p), len(p)))
            self.fh.write(p)

    def close(self):
        self.fh.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
