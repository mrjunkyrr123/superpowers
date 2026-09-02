#!/usr/bin/env bash
# Первичная установка на Orange Pi 3 LTS с Armbian (Debian/Ubuntu).
# Запуск: sudo bash scripts/install.sh
# Что делает: ставит Docker, отключает ModemManager (он захватывает Zigbee-стик),
# создаёт .env, файл паролей MQTT и папки с правильными правами.
set -euo pipefail

cd "$(dirname "$0")/.."
PROJECT_DIR="$(pwd)"
REAL_USER="${SUDO_USER:-$USER}"

if [[ $EUID -ne 0 ]]; then
  echo "Запусти через sudo: sudo bash scripts/install.sh"; exit 1
fi

echo "==> [1/6] Обновляем систему"
apt-get update -y
apt-get install -y curl ca-certificates git jq

echo "==> [2/6] Ставим Docker (если нет)"
if ! command -v docker >/dev/null 2>&1; then
  curl -fsSL https://get.docker.com | sh
fi
usermod -aG docker "$REAL_USER" || true
systemctl enable --now docker

echo "==> [3/6] Отключаем ModemManager (мешает Zigbee-стику)"
if systemctl list-unit-files | grep -q ModemManager; then
  systemctl disable --now ModemManager || true
fi

echo "==> [4/6] Создаём .env"
if [[ ! -f .env ]]; then
  cp .env.example .env
  # генерим случайный пароль MQTT
  PASS="$(tr -dc 'A-Za-z0-9' </dev/urandom | head -c 24)"
  sed -i "s|^MQTT_PASSWORD=.*|MQTT_PASSWORD=${PASS}|" .env
  echo "    Создан .env со случайным паролем MQTT. Проверь ZIGBEE_DEVICE и ZIGBEE_ADAPTER!"
else
  echo "    .env уже есть, не трогаю"
fi
set -a; source .env; set +a

echo "==> [5/6] Папки и пароль MQTT"
mkdir -p mosquitto/data mosquitto/log zigbee2mqtt/data homeassistant/config
docker run --rm -v "${PROJECT_DIR}/mosquitto/config:/mosquitto/config" eclipse-mosquitto:2 \
  mosquitto_passwd -b -c /mosquitto/config/passwd "${MQTT_USER}" "${MQTT_PASSWORD}"
# внутри контейнера mosquitto работает под uid 1883
chown -R 1883:1883 mosquitto/config/passwd mosquitto/data mosquitto/log
chmod 0600 mosquitto/config/passwd
chown -R "$REAL_USER":"$REAL_USER" zigbee2mqtt homeassistant .env

echo "==> [6/6] Проверяем Zigbee-стик"
bash scripts/find-zigbee.sh
if [[ ! -e "${ZIGBEE_DEVICE}" ]]; then
  echo
  echo "!!! ${ZIGBEE_DEVICE} не найден. Поправь ZIGBEE_DEVICE в .env и потом: docker compose up -d"
  exit 0
fi

echo
echo "Всё готово. Запускай:"
echo "  docker compose up -d"
echo "  Home Assistant: http://$(hostname -I | awk '{print $1}'):8123"
echo "  Zigbee2MQTT:    http://$(hostname -I | awk '{print $1}'):${Z2M_PORT:-8080}"
echo "Перелогинься (или перезагрузись), чтобы docker работал без sudo."
