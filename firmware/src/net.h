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

// body/etag заполняются только при Fetch::Updated.
Fetch fetchDisplayList(const Config &c, String &body, String &etag);

// Телеметрия в админку. Ошибки игнорируются: heartbeat не должен мешать показу.
void sendHeartbeat(const Config &c, const String &etag, uint32_t uptimeSec);

// Локальное время "ЧЧ:ММ". До синхронизации с NTP возвращает "--:--".
void localTimeStr(char *buf, size_t n);
