#pragma once
// Скопируйте в secrets.h и заполните. secrets.h в git не попадает.
//
// Значения ниже — только стартовые, для первой прошивки. Дальше настройки
// правятся через Serial (команда `set`, см. storage.h) и живут в NVS, чтобы
// не перепрошивать табло, смонтированное под потолком.

#define DEFAULT_WIFI_SSID  ""
#define DEFAULT_WIFI_PASS  ""

// Без завершающего слэша. К адресу дописывается /api/display/<screen>.
#define DEFAULT_API_URL    "http://192.168.1.10:8080"
#define DEFAULT_API_TOKEN  ""
#define DEFAULT_SCREEN_ID  "lobby-1"

// Часовой пояс в формате POSIX TZ. Внимание: знак инвертирован — MSK-3
// означает UTC+3. Для зоны с переходом на летнее время добавьте второе
// правило, например "CET-1CEST,M3.5.0,M10.5.0/3".
#define TZ_INFO            "MSK-3"

#define FW_VERSION         "1.0.0"

// Пароль OTA (pio run -t upload --upload-port <ip>).
#define OTA_PASSWORD       "change-me"

// TLS. 1 — сертификат сервера НЕ проверяется: допустимо только в доверенной
// локальной сети. Для запросов через интернет поставьте 0 и впишите реальный
// корневой сертификат вашего API в API_ROOT_CA.
#define ALLOW_INSECURE_TLS 1
#define API_ROOT_CA        ""
