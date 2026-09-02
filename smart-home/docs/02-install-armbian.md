# Установка Armbian на Orange Pi 3 LTS

## 1. Скачать образ

https://www.armbian.com/orange-pi-3-lts/

Бери **Armbian Bookworm (Debian 12) minimal/CLI** или Ubuntu Noble CLI. Без рабочего стола — он нам не нужен, только память жрёт.

## 2. Записать на SD-карту

- Windows/Mac/Linux: [balenaEtcher](https://etcher.balena.io/) или Raspberry Pi Imager
- Карта минимум 8 ГБ, лучше 16-32 ГБ класса A1/A2

## 3. Первый запуск

1. Вставить карту, подключить Ethernet, питание.
2. Найти IP платы: в роутере в списке DHCP-клиентов, или `ping orangepi3-lts.local`
3. Зайти по SSH: `ssh root@<ip>`, пароль `1234`
4. Armbian попросит сменить пароль root и создать обычного пользователя. **Создай** — под root работать не будем.

## 4. Перенести систему на eMMC (по желанию, но советую)

eMMC быстрее и надёжнее SD-карты:
```bash
sudo armbian-install
```
Выбрать «Boot from eMMC - system on eMMC». Дождаться, выключить, вынуть SD, включить.

## 5. Базовая настройка

```bash
sudo armbian-config
```
- System → Timezone — выставить свой
- Network — можно задать статический IP (или зафиксируй IP на роутере по MAC — так проще)

Затем:
```bash
sudo apt update && sudo apt upgrade -y
sudo reboot
```

## 6. Клонируем проект

```bash
sudo apt install -y git
git clone <url-этого-репо>
cd <репо>/smart-home
```

Дальше — по README: `find-zigbee.sh`, `.env`, `install.sh`, `docker compose up -d`.

## Если что-то пошло не так

| Симптом | Что смотреть |
|---|---|
| Плата не грузится | Питание. 90% случаев. Попробуй другой блок |
| Нет сети | `ip a` — есть ли адрес на `eth0`. Кабель, порт роутера |
| Стик не виден | `dmesg \| tail -30` после втыкания. Если видишь `ttyUSB0` — ок. Если ничего — попробуй другой USB-порт |
| `docker: permission denied` | Не перелогинился после install.sh. `exit` и зайти снова |
