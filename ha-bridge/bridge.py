#!/usr/bin/env python3
"""Мостик Home Assistant -> экран «Дом» настольной панели 4848 (display list).

Живёт на боксе HA, в той же сети, что и панель. Читает состояния из HA по REST и
отдаёт экран в формате прошивки (как mock-server/dashboard.py и rec-room-api);
касание плитки — команда в HA. Ключ HA не покидает дом.

    GET  /api/display/home              экран, ETag + If-None-Match -> 304
    POST /api/display/home/act/<entity> переключить устройство / запустить сцену

Пускает только панель (Bearer, sha256 в PANEL_TOKEN_SHA256) и только к сущностям
из config.json — произвольным домом панель управлять не может.
"""
import hashlib
import hmac
import json
import os
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HA_URL = os.environ.get('HA_URL', 'http://127.0.0.1:8123').rstrip('/')
HA_TOKEN = os.environ.get('HA_TOKEN', '')
PANEL_SHA = bytes.fromhex(os.environ.get('PANEL_TOKEN_SHA256', '00' * 32))
CONFIG = os.environ.get('CONFIG', os.path.join(os.path.dirname(__file__), 'config.json'))
PORT = int(os.environ.get('PORT', '8090'))

# Палитра — та же, что у дашборда RECOROOM (уже в RGB565).
C = {'text': '#FFFFFF', 'text2': '#9C9A9C', 'text3': '#6B696B', 'line': '#292829',
     'card': '#101010', 'bar': '#4A494A', 'brand': '#EF2C52', 'on': '#08A208',
     'crit': '#CE3C39'}
M, W = 16, 480
TOGGLE = {'switch', 'light', 'fan', 'input_boolean', 'cover'}

# В шрифтах прошивки — ASCII, кириллица и «Ё ё ° —».
def fit(s):
    out = []
    for ch in str(s):
        o = ord(ch)
        if 0x20 <= o < 0x7F or 0x410 <= o <= 0x44F or ch in 'Ёё°—':
            out.append(ch)
        elif ch in '–−':
            out.append('-')
    return ' '.join(''.join(out).split())


def load_config():
    with open(CONFIG, encoding='utf-8') as fh:
        return json.load(fh)


def ha(path, body=None):
    req = urllib.request.Request(HA_URL + path, method='POST' if body is not None else 'GET',
                                 data=None if body is None else json.dumps(body).encode(),
                                 headers={'Authorization': 'Bearer ' + HA_TOKEN,
                                          'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=5) as r:
        return json.loads(r.read() or b'null')


def text(x, y, w, s, f, c='text', a=None):
    L = {'t': 'text', 'x': x, 'y': y, 'w': w, 'f': f, 'c': C[c], 's': fit(s)}
    if a:
        L['a'] = a
    return L


def rect(x, y, w, h, c):
    return {'t': 'rect', 'x': x, 'y': y, 'w': w, 'h': h, 'c': C[c], 'fill': True}


def button(bid, x, y, w, h, label, bg, fg, entity, f='b16'):
    return {'t': 'button', 'id': bid, 'x': x, 'y': y, 'w': w, 'h': h, 'r': 10, 'f': f,
            's': fit(label), 'c': C[fg], 'bg': C[bg],
            'action': {'method': 'POST', 'path': '/api/display/home/act/' + entity}}


def num(v):
    try:
        x = float(v)
    except (TypeError, ValueError):
        return None
    return ('%.1f' % x).replace('.', ',')


def build(conf, st):
    """Экран «Дом»: датчики сверху, плитки устройств по две, сцены по три."""
    e = [rect(M, 18, 4, 16, 'brand'), text(28, 16, 300, conf.get('title', 'ДОМ'), 'b16'),
         {'t': 'wifi', 'x': 385, 'y': 17, 'c': C['text2'], 'bg': C['bar']},
         {'t': 'clock', 'x': 344, 'y': 16, 'w': 120, 'f': 'b16', 'c': C['text2'], 'a': 'r'},
         {'t': 'line', 'x': M, 'y': 44, 'x2': W - M - 1, 'y2': 44, 'c': C['line']}]
    y = 56
    sensors = conf.get('sensors', [])[:2]
    for i, s in enumerate(sensors):
        x = M + i * 232
        t = num(st.get(s.get('temperature'), {}).get('state'))
        h = num(st.get(s.get('humidity'), {}).get('state'))
        e += [rect(x, y, 216, 64, 'card'), text(x + 12, y + 8, 192, s.get('name', ''), 'r12', 'text2'),
              text(x + 12, y + 26, 120, (t + '°') if t else '—', 'b24'),
              text(x + 112, y + 32, 92, (h + ' %') if h else '', 'b16', 'text2', 'r')]
    if sensors:
        y += 76
    for i, d in enumerate(conf.get('devices', [])[:6]):
        x, yy = M + (i % 2) * 232, y + (i // 2) * 76
        s = st.get(d['entity'], {})
        state = s.get('state', 'unavailable')
        name = d.get('name') or s.get('attributes', {}).get('friendly_name', d['entity'])
        if state in ('unavailable', 'unknown') or not s:
            e += [rect(x, yy, 216, 64, 'card'), text(x + 12, yy + 12, 192, name, 'b16', 'text3'),
                  text(x + 12, yy + 36, 192, 'нет связи', 'r12', 'crit')]
            continue
        on = state in ('on', 'open', 'opening')
        e += [button('d%d' % i, x, yy, 216, 64, name, 'on' if on else 'card', 'text', d['entity']),
              text(x + 12, yy + 44, 192, 'вкл' if on else 'выкл', 'r12', 'text' if on else 'text3')]
    rows = (min(len(conf.get('devices', [])), 6) + 1) // 2
    y += rows * 76
    for i, sc in enumerate(conf.get('scenes', [])[:6]):
        x, yy = M + (i % 3) * 152, y + (i // 3) * 48
        if yy + 40 > 452:
            break
        e.append(button('s%d' % i, x, yy, 144, 40, sc.get('name', sc['entity']), 'line', 'text',
                        sc['entity'], 'b13'))
    return {'v': 1, 'w': W, 'h': W, 'pages': [{'id': 'home', 'layers': e}]}


def allowed(conf):
    return {d['entity'] for d in conf.get('devices', [])} | {s['entity'] for s in conf.get('scenes', [])}


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, fmt, *args):
        print('%s  %s' % (time.strftime('%H:%M:%S'), fmt % args), flush=True)

    def _ok_token(self):
        h = self.headers.get('Authorization', '')
        if not h.startswith('Bearer ') or len(h) < 23:
            return False
        return hmac.compare_digest(hashlib.sha256(h[7:].strip().encode()).digest(), PANEL_SHA)

    def _send(self, code, body=b'', ctype='application/json; charset=utf-8', extra=None):
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_GET(self):
        if self.path.split('?')[0] != '/api/display/home':
            return self._send(404)
        if not self._ok_token():
            return self._send(401)
        try:
            conf = load_config()
            st = {s['entity_id']: s for s in ha('/api/states')}
        except Exception as ex:  # HA лёг — панель покажет прежний экран из памяти
            print('HA: %s' % ex, flush=True)
            return self._send(502)
        body = json.dumps(build(conf, st), ensure_ascii=False, separators=(',', ':')).encode()
        etag = '"%s"' % hashlib.sha1(body).hexdigest()[:16]
        if self.headers.get('If-None-Match') == etag:
            self.send_response(304)
            self.send_header('ETag', etag)
            self.end_headers()
            return
        self._send(200, body, extra={'ETag': etag})

    def do_POST(self):
        n = int(self.headers.get('Content-Length') or 0)
        if n:
            self.rfile.read(n)
        parts = self.path.split('?')[0].split('/')
        if len(parts) != 6 or parts[1:5] != ['api', 'display', 'home', 'act']:
            return self._send(404)
        if not self._ok_token():
            return self._send(401)
        entity = parts[5]
        if entity not in allowed(load_config()):
            return self._send(403)
        domain = entity.split('.')[0]
        service = 'scene/turn_on' if domain == 'scene' else (domain + '/toggle' if domain in TOGGLE else None)
        if not service:
            return self._send(400)
        try:
            ha('/api/services/' + service, {'entity_id': entity})
        except Exception as ex:
            print('HA %s: %s' % (service, ex), flush=True)
            return self._send(502)
        time.sleep(0.4)  # пусть HA успеет обновить состояние: панель сразу переспросит
        print('  %s -> %s' % (entity, service), flush=True)
        self._send(200, b'{"ok":true}')


if __name__ == '__main__':
    if not HA_TOKEN:
        raise SystemExit('нет HA_TOKEN')
    print('мостик панели на :%d, HA %s' % (PORT, HA_URL), flush=True)
    ThreadingHTTPServer(('0.0.0.0', PORT), Handler).serve_forever()
