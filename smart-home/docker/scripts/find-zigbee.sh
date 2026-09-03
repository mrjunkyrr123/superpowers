#!/usr/bin/env bash
# Ищет USB Zigbee-координатор и показывает стабильный путь для .env
set -euo pipefail

echo "=== USB-устройства ==="
lsusb || true
echo
echo "=== Serial-порты (стабильные имена, их и пиши в ZIGBEE_DEVICE) ==="
if ls /dev/serial/by-id/ >/dev/null 2>&1; then
  for f in /dev/serial/by-id/*; do
    echo "$f  ->  $(readlink -f "$f")"
  done
else
  echo "Пусто. Стик не воткнут, или его не видит система. Смотри: dmesg | tail -20"
fi
