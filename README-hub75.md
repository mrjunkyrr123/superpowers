# Табло HUB75 64×32

Вывод данных из админки на светодиодную панель HUB75. ESP32 забирает с сервера
готовый «display list» — список примитивов отрисовки — и крутит его страницы.
Вся вёрстка живёт на сервере, прошивка её только исполняет.

Проект решения и обоснование архитектуры: [docs/led-hub75/DESIGN.md](docs/led-hub75/DESIGN.md)

```
firmware/     прошивка ESP32 (PlatformIO)
mock-server/  мок админки + эталонный рендерер и превью раскладки
tools/        генератор шрифтов
```

## Быстрый старт без железа

Посмотреть, как будут выглядеть страницы, можно прямо сейчас:

```bash
python3 mock-server/render.py mock-server/sample.json          # ASCII в терминал
python3 mock-server/render.py mock-server/sample.json -o out.png
```

Поднять мок админки:

```bash
python3 mock-server/server.py --port 8080 --token secret
# http://localhost:8080/api/display/lobby-1  — display list
# http://localhost:8080/preview.png          — все страницы картинкой
```

## Прошивка

```bash
cd firmware
cp src/secrets.example.h src/secrets.h     # заполнить SSID, адрес API, токен
pio run -t upload && pio device monitor
```

Настройки после первой прошивки правятся по Serial и хранятся в NVS — чтобы не
снимать табло с креплений ради смены пароля WiFi:

```
show
set ssid MyNetwork
set url http://192.168.1.10:8080
set token abc123
save
reboot
```

Обновление по воздуху: `pio run -t upload --upload-port <ip-табло>`.

## Проверка отрисовки

Прошивка и превью используют один и тот же растр шрифтов, и это проверяется:

```bash
python3 firmware/test/compare.py
```

Тест собирает `gfx.cpp`/`fonts.cpp` хостовым компилятором, прогоняет набор строк
и сравнивает результат пиксель в пиксель с `mock-server/render.py`. Расхождение
означает, что панель покажет не то, что показало превью.

## Шрифты

```bash
python3 tools/genfont.py     # -> firmware/src/fonts.{h,cpp} + mock-server/fonts.json
```

| id | высота | символов в строке 64 px | источник |
|----|--------|-------------------------|----------|
| `f5`  | 8 px  | 10 кириллических | нарисован попиксельно |
| `r12` | 15 px | 8  | DejaVu Sans 12 |
| `b13` | 17 px | 6  | DejaVu Sans Bold 13 |
| `b16` | 19 px | 5  | DejaVu Sans Bold 16 |

TrueType на 7–10 px даёт нечитаемую кириллицу, поэтому мелкий кегль нарисован
вручную. В нём кириллица только прописная: строчные буквы при высоте 4 px
неразличимы, поэтому `а`–`я` отображаются на глифы `А`–`Я`. Для смешанного
регистра берите `r12` и крупнее.
