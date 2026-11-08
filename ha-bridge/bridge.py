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

# Палитра «умного дома» (владелец, 08.11.2026: ближе к тёмным панелям с синим акцентом).
# Шапка — как у дашборда RECOROOM.
C = {'text': '#FFFFFF', 'text2': '#8E8F94', 'text3': '#5C5D62', 'line': '#292829',
     'card': '#16171B', 'card2': '#25262B', 'bg': '#000000', 'brand': '#EF2C52',
     'accent': '#2F7BF5', 'accent2': '#6EA4FF', 'knob_off': '#3A3B40', 'warm': '#F5A623',
     'crit': '#E7494A'}
M, W = 16, 480
TOGGLE = {'switch', 'light', 'fan', 'input_boolean', 'cover'}

# Значки — шрифт i20/i24 прошивки (Tabler Icons, tools/genfont.py: ICONS).
ICON = {
    'bulb': '', 'lamp': '', 'lamp-2': '', 'device-desktop': '',
    'droplet': '', 'wind': '', 'temperature': '', 'cloud': '',
    'plug': '', 'home': '', 'sun': '', 'moon': '',
    'briefcase': '', 'movie': '', 'door-exit': '', 'power': '',
    'air-conditioning': '', 'sofa': '', 'bed': '', 'sunrise': '',
}
# Значок по слову в названии, если в config.json не задан "icon".
GUESS = [('ламп', 'bulb'), ('ночник', 'lamp'), ('свет', 'bulb'), ('комп', 'device-desktop'),
         ('увлажн', 'droplet'), ('озон', 'wind'), ('кондиц', 'air-conditioning'),
         ('обогрев', 'temperature'), ('розет', 'plug'), ('утро', 'sunrise'), ('работ', 'briefcase'),
         ('кино', 'movie'), ('сон', 'moon'), ('ушёл', 'door-exit'), ('ушел', 'door-exit'),
         ('выкл', 'power'), ('дом', 'home')]


def icon_for(item, fallback='power'):
    name = str(item.get('icon') or '')
    if name in ICON:
        return ICON[name]
    low = str(item.get('name', '')).lower()
    return ICON[next((ic for key, ic in GUESS if key in low), fallback)]


# В шрифтах прошивки — ASCII, кириллица, «Ёё°—₽«»№·…✓−» и значки U+E000..U+F8FF.
EXTRA = 'Ёё°—₽«»№·…✓−'


def fit(s):
    out = []
    for ch in str(s):
        o = ord(ch)
        if 0x20 <= o < 0x7F or 0x410 <= o <= 0x44F or ch in EXTRA or 0xE000 <= o <= 0xF8FF:
            out.append(ch)
        elif ch == '–':
            out.append('—')
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


def text(x, y, w, s, f, c='text', a=None, el=False):
    """el — не влезло в w: плата обрежет по ширине и поставит «…»."""
    L = {'t': 'text', 'x': x, 'y': y, 'w': w, 'f': f, 'c': C[c], 's': fit(s)}
    if a:
        L['a'] = a
    if el:
        L['el'] = True
    return L


def rect(x, y, w, h, c, r=0):
    L = {'t': 'rect', 'x': x, 'y': y, 'w': w, 'h': h, 'c': C[c], 'fill': True}
    if r:
        L['r'] = r
    return L


ACT = '/api/display/home/act/'


def zone(zid, x, y, w, h, entity, r=16):
    """Невидимая зона касания поверх плитки; нажатую плата обводит рамкой 2 px."""
    return {'t': 'zone', 'id': zid, 'x': x, 'y': y, 'w': w, 'h': h, 'r': r, 'pr': 'frame',
            'pc': C['accent'], 'action': {'method': 'POST', 'path': ACT + entity}}


def disc(x, y, d, c):
    """Круг диаметра d — скруглённый прямоугольник с радиусом d/2."""
    return rect(x, y, d, d, c, d // 2)


def header(title):
    return [rect(M, 16, 4, 18, 'brand'), text(28, 15, 330, title, 'b16', el=True),
            {'t': 'wifi', 'st': 'bars', 'x': 386, 'y': 17, 'c': C['text2'], 'bg': C['line']},
            {'t': 'clock', 'x': 412, 'y': 15, 'w': 52, 'f': 'b16', 'c': C['text2'], 'a': 'r'},
            {'t': 'line', 'x': M, 'y': 47, 'x2': W - M - 1, 'y2': 47, 'c': C['line']}]


def toggle(x, y, on):
    """Переключатель-таблетка 44x24: включён — синий, кружок справа."""
    return [rect(x, y, 44, 24, 'accent' if on else 'knob_off', 12),
            disc(x + 23 if on else x + 3, y + 3, 18, 'text' if on else 'text2')]


def num(v, digits=1):
    try:
        x = float(v)
    except (TypeError, ValueError):
        return None
    return ('%.*f' % (digits, x)).replace('.', ',').replace('-', '−')


def build(conf, st):
    """Экраны дома (владелец, 08.11.2026: ближе к тёмным панелям с синим акцентом).
    «Дом»: датчики, плитки устройств по две — значок в кружке, переключатель, имя,
    состояние. «Сцены» — отдельным экраном, если они есть: шесть плиток не влезают
    на 4 дюйма вместе с устройствами."""
    e = header(conf.get('title', 'ДОМ'))
    y = 60
    sensors = conf.get('sensors', [])[:2]
    sw = 448 if len(sensors) == 1 else 220
    for i, s in enumerate(sensors):
        x = M + i * 228
        t = num(st.get(s.get('temperature'), {}).get('state'))
        h = num(st.get(s.get('humidity'), {}).get('state'), 0)
        e += [rect(x, y, sw, 72, 'card', 16),
              disc(x + 14, y + 16, 40, 'card2'),
              text(x + 22, y + 24, 24, ICON['temperature'], 'i24', 'warm'),
              text(x + 66, y + 10, sw - 80, s.get('name', ''), 'm14', 'text2', el=True),
              text(x + 66, y + 30, 100, (t + '°') if t else '—', 'b28')]
        if h:
            e += [text(x + sw - 86, y + 40, 20, ICON['droplet'], 'i20', 'accent2'),
                  text(x + sw - 62, y + 38, 50, h + ' %', 'm16', 'text2')]
    if sensors:
        y += 80
    devices = conf.get('devices', [])[:6]
    th = 96   # три ряда: 140..444, кружок значка не налезает на имя
    for i, d in enumerate(devices):
        x, yy = M + (i % 2) * 228, y + (i // 2) * (th + 8)
        s = st.get(d['entity'], {})
        state = s.get('state', 'unavailable')
        name = d.get('name') or s.get('attributes', {}).get('friendly_name', d['entity'])
        ic = icon_for(dict(d, name=name))
        if state in ('unavailable', 'unknown') or not s:
            e += [rect(x, yy, 220, th, 'card2', 16), rect(x + 1, yy + 1, 218, th - 2, 'bg', 15),
                  disc(x + 14, yy + 12, 40, 'card'),
                  text(x + 22, yy + 20, 24, ic, 'i24', 'text3'),
                  text(x + 14, yy + th - 42, 192, name, 'b16', 'text3', el=True),
                  text(x + 14, yy + th - 22, 192, 'нет связи', 'm14', 'crit')]
            continue
        on = state in ('on', 'open', 'opening')
        e += [rect(x, yy, 220, th, 'card', 16),
              disc(x + 14, yy + 12, 40, 'accent' if on else 'card2'),
              text(x + 22, yy + 20, 24, ic, 'i24', 'text' if on else 'text2')]
        e += toggle(x + 220 - 14 - 44, yy + 20, on)
        e += [text(x + 14, yy + th - 42, 192, name, 'b16', el=True),
              text(x + 14, yy + th - 22, 192, 'включено' if on else 'выключено', 'm14',
                   'accent2' if on else 'text3'),
              zone('d%d' % i, x, yy, 220, th, d['entity'])]
    pages = [{'id': 'home', 'layers': e}]

    scenes = conf.get('scenes', [])[:6]
    if scenes:
        e = header('СЦЕНЫ')
        sh = 120
        for i, sc in enumerate(scenes):
            x, yy = M + (i % 2) * 228, 60 + (i // 2) * (sh + 8)
            e += [rect(x, yy, 220, sh, 'card', 16),
                  disc(x + 16, yy + 16, 48, 'card2'),
                  text(x + 28, yy + 28, 24, icon_for(sc), 'i24', 'accent2'),
                  text(x + 16, yy + sh - 44, 188, sc.get('name', sc['entity']), 'b20', el=True),
                  zone('s%d' % i, x, yy, 220, sh, sc['entity'])]
        pages.append({'id': 'scenes', 'layers': e})
    return {'v': 1, 'w': W, 'h': W, 'pages': pages}


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
