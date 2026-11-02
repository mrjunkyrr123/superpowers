#pragma once
#include <Arduino.h>

// Настройки живут в NVS, а не в прошивке: менять SSID или токен перепрошивкой
// на смонтированном под потолком табло — плохая идея.
// Значения из secrets.h используются как начальные, если в NVS пусто.
struct Config {
  String ssid, pass, url, token, screenId;
  bool valid() const { return ssid.length() && url.length(); }
};

void loadConfig(Config &c);
void saveConfig(const Config &c);

// Разбирает команды с Serial (show / set <поле> <значение> / save / reboot).
// Вызывать из loop(); ничего не делает, если ввода нет.
void configConsole(Config &c);

// Кэш последнего удачного ответа сервера: после перезагрузки без сети табло
// показывает последние известные данные, а не пустой экран.
bool cacheLoad(String &json, String &etag);
void cacheSave(const String &json, const String &etag);

// Где кэш: по умолчанию в NVS (табло HUB75, ответ маленький), с флагом
// сборки CACHE_LITTLEFS — файлом во флеш (панель 480x480, ответ ~14 КБ).
// Ответ длиннее CACHE_MAX просто не кэшируется.
#ifdef CACHE_LITTLEFS
static const size_t CACHE_MAX = 128 * 1024;
#else
static const size_t CACHE_MAX = 3500;   // ограничение NVS на одну строку
#endif
