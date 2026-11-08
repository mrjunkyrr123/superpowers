#!/bin/bash
# Ставит мостик панели на бокс HA как службу systemd panel-bridge (порт 8090).
# Файлы bridge.py и config.json должны лежать в /tmp/panel-bridge/.
# Секреты — из stdin, по строке: 1) ключ HA (long-lived token), 2) sha256 ключа панели.
# config.json не перезаписывается, если уже есть: список устройств правят на месте.
set -e
read -r HA_TOKEN
read -r PANEL_SHA
HA_TOKEN=$(printf '%s' "$HA_TOKEN" | tr -cd 'A-Za-z0-9._-')
PANEL_SHA=$(printf '%s' "$PANEL_SHA" | tr -cd '0-9a-f')
[ ${#HA_TOKEN} -ge 100 ] || { echo "плохой ключ HA: длина ${#HA_TOKEN}"; exit 1; }
[ ${#PANEL_SHA} -eq 64 ] || { echo "плохой sha ключа панели: длина ${#PANEL_SHA}"; exit 1; }

D=/opt/panel-bridge
mkdir -p $D
install -m 644 /tmp/panel-bridge/bridge.py $D/bridge.py
[ -f $D/config.json ] || install -m 644 /tmp/panel-bridge/config.json $D/config.json
umask 077
cat > $D/env <<EOF
HA_URL=http://127.0.0.1:8123
HA_TOKEN=$HA_TOKEN
PANEL_TOKEN_SHA256=$PANEL_SHA
PORT=8090
CONFIG=$D/config.json
PYTHONIOENCODING=utf-8
EOF
cat > /etc/systemd/system/panel-bridge.service <<EOF
[Unit]
Description=Panel 4848 bridge to Home Assistant
After=network-online.target docker.service

[Service]
EnvironmentFile=$D/env
ExecStart=/usr/bin/python3 -u $D/bridge.py
Restart=always
RestartSec=5
DynamicUser=yes

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable --now panel-bridge >/dev/null 2>&1
systemctl restart panel-bridge
sleep 2
systemctl is-active panel-bridge
journalctl -u panel-bridge -n 3 --no-pager | cut -c1-140
