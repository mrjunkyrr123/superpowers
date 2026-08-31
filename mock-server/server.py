#!/usr/bin/env python3
"""Мок админки: отдаёт display list по тому же контракту, что и боевой бэкенд.

Нужен, чтобы разрабатывать прошивку и вёрстку, не дожидаясь эндпоинта в
админке, и чтобы бэкенд потом было с чем сверить.

    python3 mock-server/server.py               # порт 8080, без токена
    python3 mock-server/server.py --port 9000 --token secret

Маршруты:
    GET  /api/display/<id>            display list, ETag + If-None-Match
    POST /api/display/<id>/heartbeat  телеметрия устройства, пишется в лог
    GET  /preview.png                 все страницы одной картинкой
    GET  /                            короткая справка

Данные генерируются на лету и меняются раз в минуту — так проверяется, что
устройство реально переспрашивает и корректно обрабатывает 304.
"""

import argparse
import hashlib
import json
import os
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import render

HERE = os.path.dirname(os.path.abspath(__file__))
TOKEN = None


def build_display_list():
    """Здесь боевой бэкенд подставит реальные данные из админки.

    Смысл этой функции — показать, что вёрстка собирается на сервере: прошивка
    ни про «свободные места», ни про «переговорки» ничего не знает.
    """
    minute = int(time.time() // 60)
    free = 3 + minute % 12          # что-нибудь меняющееся
    total = 24
    occupancy = 1.0 - free / total

    colour = "#00FF88" if free > 6 else "#FFAA00" if free > 2 else "#FF3355"

    return {
        "v": 1,
        "poll_interval": 20,
        "brightness": 55,
        "pages": [
            {
                "dwell": 6,
                "layers": [
                    {"t": "text", "x": 0, "y": 0, "w": 64, "a": "c",
                     "f": "f5", "c": "#8899AA", "s": "СВОБОДНО"},
                    {"t": "text", "x": 2, "y": 9, "f": "b16", "c": colour,
                     "s": str(free)},
                    {"t": "text", "x": 14 if free < 10 else 24, "y": 16,
                     "f": "f5", "c": "#FFFFFF", "s": f"из {total}"},
                    {"t": "bar", "x": 2, "y": 27, "w": 60, "h": 3,
                     "p": round(occupancy, 2), "c": "#FF9900", "bg": "#182018"},
                ],
            },
            {
                "dwell": 6,
                "layers": [
                    {"t": "text", "x": 0, "y": 0, "w": 64, "a": "c",
                     "f": "f5", "c": "#8899AA", "s": "КОМНАТА А"},
                    {"t": "text", "x": 0, "y": 9, "w": 64, "a": "c",
                     "f": "b13", "c": "#FFAA00", "s": "14:30"},
                    {"t": "scroll", "x": 0, "y": 24, "w": 64, "f": "f5",
                     "c": "#FFFFFF", "sp": 18,
                     "s": "занята до 14:30, затем свободна"},
                ],
            },
            {
                "dwell": 8,
                "layers": [
                    {"t": "clock", "x": 0, "y": 1, "w": 64, "a": "c",
                     "f": "b16", "c": "#FFFFFF"},
                    {"t": "line", "x": 6, "y": 20, "x2": 57, "y2": 20,
                     "c": "#303840"},
                    {"t": "scroll", "x": 0, "y": 23, "w": 64, "f": "f5",
                     "c": "#66CCFF", "sp": 16,
                     "s": "Сегодня в 19:00 митап, регистрация на ресепшене"},
                ],
            },
        ],
    }


def body_and_etag():
    body = json.dumps(build_display_list(), ensure_ascii=False,
                      separators=(',', ':')).encode('utf-8')
    return body, '"%s"' % hashlib.md5(body).hexdigest()[:16]


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, fmt, *args):
        print('%s  %s' % (time.strftime('%H:%M:%S'), fmt % args))

    def _authorised(self):
        if not TOKEN:
            return True
        return self.headers.get('Authorization') == 'Bearer ' + TOKEN

    def _send(self, code, body=b'', ctype='text/plain; charset=utf-8', extra=None):
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_GET(self):
        path = self.path.split('?')[0]

        if path == '/':
            return self._send(200, __doc__.encode('utf-8'))

        if path == '/preview.png':
            doc = build_display_list()
            pages = doc['pages']
            big = render.Canvas(render.PANEL_W, render.PANEL_H * len(pages) +
                                4 * (len(pages) - 1))
            y = 0
            for pg in pages:
                cv = render.render_page(pg, now=time.strftime('%H:%M'))
                for row in range(cv.h):
                    for col in range(cv.w):
                        big.set(col, y + row, cv.px[row][col])
                y += cv.h + 4
            path_png = os.path.join(HERE, '.preview.png')
            render.to_png(big, path_png, scale=6)
            with open(path_png, 'rb') as fh:
                return self._send(200, fh.read(), 'image/png',
                                  {'Cache-Control': 'no-store'})

        parts = [p for p in path.split('/') if p]
        if len(parts) == 3 and parts[0] == 'api' and parts[1] == 'display':
            if not self._authorised():
                return self._send(401, b'unauthorised')
            body, etag = body_and_etag()
            if self.headers.get('If-None-Match') == etag:
                # 304 обязан идти без тела, иначе клиент зависнет на чтении.
                self.send_response(304)
                self.send_header('ETag', etag)
                self.end_headers()
                return
            return self._send(200, body, 'application/json; charset=utf-8',
                              {'ETag': etag})

        self._send(404, b'not found')

    def do_POST(self):
        parts = [p for p in self.path.split('?')[0].split('/') if p]
        if len(parts) == 4 and parts[0] == 'api' and parts[3] == 'heartbeat':
            if not self._authorised():
                return self._send(401, b'unauthorised')
            n = int(self.headers.get('Content-Length') or 0)
            raw = self.rfile.read(n) if n else b'{}'
            try:
                hb = json.loads(raw)
            except ValueError:
                hb = {'raw': raw[:120].decode('utf-8', 'replace')}
            print('  heartbeat %s: %s' % (parts[2], hb))
            return self._send(204)
        self._send(404, b'not found')


def main():
    global TOKEN
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=8080)
    ap.add_argument('--token', default=os.environ.get('DISPLAY_TOKEN', ''))
    args = ap.parse_args()
    TOKEN = args.token or None

    srv = ThreadingHTTPServer(('0.0.0.0', args.port), Handler)
    print('мок админки на http://0.0.0.0:%d' % args.port)
    print('  display list: /api/display/lobby-1')
    print('  превью:       /preview.png')
    print('  токен:        %s' % ('требуется' if TOKEN else 'не требуется'))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print('\nостановлен')


if __name__ == '__main__':
    main()
