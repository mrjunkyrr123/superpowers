#!/bin/sh
# Заливает образ Home Assistant OS на внутренний eMMC PiPO X9s.
#
# Запускать из root-шелла HAOS (в консоли `ha >` набрать `login`, приглашение станет `#`):
#   curl -L https://raw.githubusercontent.com/mrjunkyrr123/superpowers/claude/home-assistant-smart-home-kgdtzz/smart-home/scripts/flash-emmc.sh | sh
#
# Что делает:
#   1. Находит eMMC и убеждается, что это НЕ тот диск, с которого сейчас загружены
#   2. Спрашивает подтверждение
#   3. Качает образ HAOS и пишет его на eMMC (распаковка через unxz или через docker+alpine)
#   4. Просит выключиться и вынуть флешку
#
# Диск стирается целиком. Всё, что было на eMMC, пропадёт.

set -e

VERSION="18.2"
URL="https://github.com/home-assistant/operating-system/releases/download/${VERSION}/haos_generic-x86-64-${VERSION}.img.xz"

say() { echo "==> $*"; }
die() { echo "ОШИБКА: $*" >&2; exit 1; }

# --- 1. Ищем целевой диск ---------------------------------------------------

# С какого устройства загружены сейчас: смотрим, что примонтировано в /mnt/boot
BOOT_PART=$(awk '$2=="/mnt/boot"{print $1}' /proc/mounts | head -1)
BOOT_DISK=""
if [ -n "$BOOT_PART" ]; then
    # /dev/sda1 -> sda ; /dev/mmcblk0p1 -> mmcblk0
    BOOT_DISK=$(basename "$BOOT_PART" | sed -e 's/p\?[0-9]*$//')
fi
say "Загружены сейчас с: ${BOOT_DISK:-неизвестно}"

# Кандидаты: mmcblk0 / mmcblk1, не boot-разделы, не тот диск, с которого грузимся
TARGET=""
for d in /sys/block/mmcblk*; do
    [ -e "$d" ] || continue
    name=$(basename "$d")
    case "$name" in *boot*|*rpmb*) continue;; esac
    [ "$name" = "$BOOT_DISK" ] && continue
    sectors=$(cat "$d/size" 2>/dev/null || echo 0)
    gb=$(( sectors / 2 / 1024 / 1024 ))
    [ "$gb" -lt 8 ] && continue          # меньше 8 ГБ — не наш случай
    TARGET="$name"
    TARGET_GB="$gb"
    break
done

[ -n "$TARGET" ] || die "Не нашёл подходящий eMMC. Посмотри вывод lsblk и залей вручную."

say "Цель: /dev/${TARGET}, размер ${TARGET_GB} ГБ"

# Ни один раздел цели не должен быть примонтирован
if grep -q "^/dev/${TARGET}" /proc/mounts; then
    grep "^/dev/${TARGET}" /proc/mounts
    die "Разделы /dev/${TARGET} примонтированы. Останавливаюсь."
fi

# --- 2. Подтверждение -------------------------------------------------------

echo
echo "  ВНИМАНИЕ: /dev/${TARGET} будет стёрт целиком."
echo "  Образ: HAOS ${VERSION} (generic-x86-64)"
echo
printf "  Продолжить? Набери yes и Enter: "
read answer
[ "$answer" = "yes" ] || die "Отменено."

# --- 3. Чем распаковывать ---------------------------------------------------

if command -v unxz >/dev/null 2>&1; then
    UNXZ="unxz"
    say "Распаковка: unxz"
elif command -v xz >/dev/null 2>&1; then
    UNXZ="xz -d"
    say "Распаковка: xz -d"
elif command -v docker >/dev/null 2>&1; then
    say "unxz нет, беру alpine через docker"
    docker pull alpine >/dev/null 2>&1 || die "Не смог скачать образ alpine"
    UNXZ="docker run --rm -i alpine unxz"
    say "Распаковка: docker + alpine"
else
    die "Нечем распаковать: нет ни unxz, ни xz, ни docker"
fi

# dd с прогрессом, если умеет
if dd --help 2>&1 | grep -q status=; then
    DDOPTS="bs=4M conv=fsync status=progress"
else
    DDOPTS="bs=4M conv=fsync"
    say "Прогресса не будет, dd его не умеет. Просто жди."
fi

# --- 4. Пишем ---------------------------------------------------------------

say "Качаю и пишу на /dev/${TARGET}. Это 5-15 минут, не выключай."
# shellcheck disable=SC2086
curl -fL --retry 3 "$URL" | $UNXZ | dd of="/dev/${TARGET}" $DDOPTS

say "Сбрасываю кэши на диск"
sync
sleep 3

# --- 5. Готово --------------------------------------------------------------

echo
say "Готово. Дальше:"
echo "   1. Выключить:            poweroff"
echo "   2. Вынуть USB-флешку"
echo "   3. Включить. В BIOS поставить загрузку с eMMC, если не выберется сама"
echo
echo "   Система на eMMC чистая: пользователя надо создать заново."
