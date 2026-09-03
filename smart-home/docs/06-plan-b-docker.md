# План Б: тот же стек в Docker

Когда нужен: X9s оказался с 32-битным UEFI и HAOS на него не грузится, или решил использовать Orange Pi.

Всё лежит в папке `docker/`: `docker-compose.yml` с тремя контейнерами (Home Assistant, Zigbee2MQTT, Mosquitto), скрипты установки и бэкапа, конфиги. Разница с HAOS: нет магазина аддонов и встроенных бэкапов, всё делается через консоль.

## Б1. Debian на X9s (32-битный UEFI)

Установщик Debian умеет грузиться на 32-битном UEFI и ставить 64-битную систему.

1. Скачать `debian-XX-amd64-netinst.iso`: https://www.debian.org/download
2. Записать на флешку через [Rufus](https://rufus.ie/) (режим DD image) или Etcher
3. Загрузиться с флешки (Del/Esc/F7, Secure Boot выключить). Выбрать «Install» (не Graphical)
4. По установщику: язык, сеть по проводу, имя `smarthome`, пароль root, пользователь. Разметка: «Guided - use entire disk», выбрать eMMC. **Windows сотрётся.**
5. Выбор ПО: снять всё, оставить только «SSH server» и «standard system utilities»
6. После перезагрузки зайти по SSH: `ssh user@<ip>`
7. Дальше раздел «Общий запуск» ниже

## Б2. Armbian на Orange Pi 3 LTS

1. Скачать образ: https://www.armbian.com/orange-pi-3-lts/ — Bookworm minimal/CLI, без рабочего стола
2. Записать на SD-карту (8 ГБ+, класс A1/A2) через Etcher
3. Вставить карту, Ethernet, питание 5V/3A через DC-разъём (не от телефонной зарядки)
4. `ssh root@<ip>`, пароль `1234`. Armbian попросит сменить пароль и создать пользователя
5. Перенести систему на eMMC: `sudo armbian-install` → «Boot from eMMC - system on eMMC». Выключить, вынуть карту
6. `sudo armbian-config` → Timezone
7. Дальше раздел «Общий запуск»

Ограничения: 2 ГБ памяти и 8 ГБ eMMC. В `docker/homeassistant/config/configuration.yaml` уже стоит ограничение истории и редкая запись на диск, чтобы не убить eMMC. Стик через удлинитель, сеть по проводу.

## Общий запуск

```bash
sudo apt install -y git
git clone <url-этого-репо>
cd <репо>/smart-home/docker

bash scripts/find-zigbee.sh          # показывает /dev/serial/by-id/... стика
cp .env.example .env && nano .env    # ZIGBEE_DEVICE и ZIGBEE_ADAPTER (zstack для P, ember для E)
sudo bash scripts/install.sh         # docker, пароли, права. Перелогиниться после
docker compose up -d
docker compose logs -f zigbee2mqtt   # ждём "Zigbee2MQTT started!"
```

Потом:
- `http://<ip>:8123` — создать пользователя
- Настройки → Устройства и службы → Добавить → MQTT. Брокер `localhost`, порт `1883`, логин и пароль из `.env`
- `http://<ip>:8080` — Zigbee2MQTT, спаривание по `03-zigbee.md`

Обслуживание:
```bash
docker compose ps                    # что запущено
docker compose logs -f homeassistant
bash scripts/backup.sh               # бэкап в docker/backups/
bash scripts/update.sh               # обновить образы
```

Бэкап в cron раз в сутки: `crontab -e` → `0 3 * * * cd /path/smart-home/docker && bash scripts/backup.sh`

## Если что-то не так

| Симптом | Что смотреть |
|---|---|
| Плата не грузится | Питание. 90% случаев |
| Стик не виден | `dmesg \| tail -30` после втыкания. Нет `ttyUSB` — другой порт |
| `docker: permission denied` | Не перелогинился после install.sh |
| Z2M пишет `firmware too old` | ZBDongle-E, обновить прошивку по `03-zigbee.md` |
