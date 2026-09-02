#!/usr/bin/env bash
# Обновление образов. Сначала бэкап, потом тянем новые версии и перезапускаем.
set -euo pipefail
cd "$(dirname "$0")/.."
bash scripts/backup.sh
docker compose pull
docker compose up -d
docker image prune -f
docker compose ps
