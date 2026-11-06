#pragma once
#include <Arduino.h>
#include "storage.h"

enum class Fetch {
  Updated,      // пришёл новый display list
  NotModified,  // сервер ответил 304 — данные те же
  Failed,       // сеть или сервер недоступны
};

bool netBegin(const Config &c);   // WiFi + NTP + OTA
void netLoop();                   // OTA и переподключение WiFi
bool netOnline();

// Радио по расписанию (панель 480x480): включённый Wi-Fi мешает экрану на том же чипе.
bool netWake(const Config &c);    // включить и подключиться, до 15 с
void netSleep();                  // выключить радио совсем

// body/etag заполняются только при Fetch::Updated.
Fetch fetchDisplayList(const Config &c, String &body, String &etag);

// Действие кнопки: method на <url из настроек><path> с токеном устройства.
// path обязан начинаться с одного '/', иначе запрос не уходит. Возвращает
// HTTP-код или -1, если не отправлено.
int sendAction(const Config &c, const char *method, const char *path,
               const char *buttonId);

// Телеметрия в админку. Ошибки игнорируются: heartbeat не должен мешать показу.
void sendHeartbeat(const Config &c, const String &etag, uint32_t uptimeSec);

// Локальное время "ЧЧ:ММ". До синхронизации с NTP возвращает "--:--".
void localTimeStr(char *buf, size_t n);
