#!/usr/bin/env bash
# Бэкап всех конфигов и баз в backups/YYYY-MM-DD_HHMM.tar.gz
# Запуск: bash scripts/backup.sh   (лучше повесить в cron раз в сутки)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p backups
STAMP="$(date +%Y-%m-%d_%H%M)"
tar -czf "backups/${STAMP}.tar.gz" \
  --exclude='homeassistant/config/*.log*' \
  --exclude='mosquitto/log' \
  --exclude='zigbee2mqtt/data/log' \
  .env homeassistant zigbee2mqtt mosquitto
# храним последние 7
ls -1t backups/*.tar.gz | tail -n +8 | xargs -r rm --
echo "OK: backups/${STAMP}.tar.gz"
