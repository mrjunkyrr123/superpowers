# Умный дом на Home Assistant — Orange Pi 3 LTS + Zigbee

Проект умного дома: Home Assistant в Docker на Orange Pi 3 LTS, Zigbee-устройства через Zigbee2MQTT.

## Что здесь лежит

```
smart-home/
├── docker-compose.yml       # описание трёх контейнеров: HA, Zigbee2MQTT, Mosquitto
├── .env.example             # шаблон настроек (пароли, путь к стику). Копируется в .env
├── homeassistant/config/    # конфиги Home Assistant
├── zigbee2mqtt/data/        # конфиг Zigbee2MQTT (туда же пишется база устройств)
├── mosquitto/config/        # конфиг MQTT-брокера
├── scripts/
│   ├── install.sh           # первичная установка на Armbian (один раз)
│   ├── find-zigbee.sh       # найти USB Zigbee-стик
│   ├── backup.sh            # бэкап конфигов
│   └── update.sh            # обновить контейнеры
└── docs/                    # пошаговые инструкции
```

## Как это работает (объяснение для junior)

Три программы, каждая в своём контейнере Docker (контейнер = изолированная коробка с программой и всем, что ей нужно):

1. **Zigbee2MQTT** — разговаривает с USB Zigbee-стиком. Получает от датчиков и лампочек радиосигналы и превращает их в текстовые сообщения.
2. **Mosquitto (MQTT-брокер)** — почтовый сервер для этих сообщений. Zigbee2MQTT кидает туда «датчик в кухне: 23.5°C», Home Assistant оттуда читает.
3. **Home Assistant** — мозг. Веб-интерфейс, автоматизации («если стемнело и кто-то дома — включить свет»), приложение на телефоне.

```
[Zigbee-датчик] ~радио~> [USB-стик] -> [Zigbee2MQTT] -> [Mosquitto] -> [Home Assistant] -> [ты в браузере/телефоне]
```

Почему не Home Assistant OS одним образом? Потому что для Orange Pi 3 LTS официального образа HAOS нет. Docker на Armbian — самый надёжный вариант для этой платы. Подробнее в `docs/01-hardware.md`.

## Быстрый старт

1. Прошить Armbian на Orange Pi — `docs/02-install-armbian.md`
2. Зайти по SSH, склонировать репо, перейти в папку `smart-home`
3. Найти стик и заполнить `.env`:
   ```bash
   bash scripts/find-zigbee.sh
   cp .env.example .env && nano .env      # ZIGBEE_DEVICE и ZIGBEE_ADAPTER — обязательно
   ```
4. Установка:
   ```bash
   sudo bash scripts/install.sh
   ```
5. Запуск:
   ```bash
   docker compose up -d
   docker compose logs -f zigbee2mqtt     # смотрим, что стик подхватился
   ```
6. Открыть `http://<ip-платы>:8123`, создать пользователя.
7. В HA: Настройки → Устройства и службы → Добавить интеграцию → MQTT. Брокер `localhost`, порт `1883`, логин/пароль из `.env`.
8. Открыть `http://<ip-платы>:8080` (Zigbee2MQTT), нажать «Permit join», спарить первое устройство. Оно само появится в HA.

## Полезные команды

```bash
docker compose ps                  # что запущено
docker compose logs -f homeassistant
docker compose restart zigbee2mqtt
bash scripts/backup.sh             # бэкап
bash scripts/update.sh             # обновление
```

## Дальше

- `docs/03-zigbee.md` — про координатор, каналы, что покупать
- `docs/ROADMAP.md` — план развития и как это связать с бизнесом
