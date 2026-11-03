#include "net.h"
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <time.h>
#include "secrets.h"

static bool timeSynced = false;
static uint32_t lastReconnectMs = 0;

static bool otaEnabled() { return OTA_PASSWORD[0] != '\0'; }

static String endpoint(const Config &c, const char *suffix) {
  String u = c.url;
  while (u.endsWith("/")) u.remove(u.length() - 1);
  u += "/api/display/";
  u += c.screenId;
  u += suffix;
  return u;
}

bool netOnline() { return WiFi.status() == WL_CONNECTED; }

bool netBegin(const Config &c) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(c.ssid.c_str(), c.pass.c_str());

  const uint32_t deadline = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) delay(200);
  if (!netOnline()) {
    Serial.println(F("WiFi: подключиться не удалось, продолжаем без сети"));
    return false;
  }
  Serial.printf("WiFi: %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(),
                WiFi.RSSI());

  configTime(0, 0, "pool.ntp.org", "time.google.com");
  setenv("TZ", TZ_INFO, 1);
  tzset();

  // OTA без пароля — это прошивка для любого в той же сети. Пустой пароль
  // значит «OTA выключено», а не «OTA без пароля».
  if (otaEnabled()) {
#ifdef OTA_HOSTNAME
    ArduinoOTA.setHostname(OTA_HOSTNAME);   // доступна как <имя>.local, IP может меняться
#endif
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();
  } else {
    Serial.println(F("OTA выключено: OTA_PASSWORD пуст"));
  }
  return true;
}

void netLoop() {
  if (otaEnabled()) ArduinoOTA.handle();
  // WiFi.setAutoReconnect не всегда вытаскивает из зависшего состояния,
  // поэтому раз в 30 с пинаем стек вручную.
  if (!netOnline() && millis() - lastReconnectMs > 30000) {
    lastReconnectMs = millis();
    WiFi.disconnect();
    WiFi.reconnect();
  }
}

void localTimeStr(char *buf, size_t n) {
  struct tm t;
  if (!timeSynced) {
    // 2021-01-01 — признак того, что NTP ещё не отработал.
    time_t now = time(nullptr);
    timeSynced = now > 1609459200;
  }
  if (!timeSynced || !getLocalTime(&t, 0)) {
    snprintf(buf, n, "--:--");
    return;
  }
  strftime(buf, n, "%H:%M", &t);
}

// Общая обвязка HTTP: на HTTPS нужен отдельный клиент.
// ВНИМАНИЕ: при ALLOW_INSECURE_TLS сертификат сервера не проверяется. Это
// допустимо только в доверенной локальной сети. Для выхода в интернет
// подставьте сюда setCACert() с реальным корневым сертификатом.
static bool beginRequest(HTTPClient &http, WiFiClientSecure &tls,
                         WiFiClient &plain, const String &url) {
  if (url.startsWith("https:")) {
#if ALLOW_INSECURE_TLS
    tls.setInsecure();
#else
    tls.setCACert(API_ROOT_CA);
#endif
    return http.begin(tls, url);
  }
  return http.begin(plain, url);
}

Fetch fetchDisplayList(const Config &c, String &body, String &etag) {
  if (!netOnline()) return Fetch::Failed;

  WiFiClientSecure tls;
  WiFiClient plain;
  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);

  if (!beginRequest(http, tls, plain, endpoint(c, ""))) return Fetch::Failed;

  if (c.token.length()) http.addHeader("Authorization", "Bearer " + c.token);
  if (etag.length())    http.addHeader("If-None-Match", etag);
  const char *collect[] = {"ETag"};
  http.collectHeaders(collect, 1);

  const int code = http.GET();
  Fetch result;
  if (code == 200) {
    body = http.getString();
    const String newEtag = http.header("ETag");
    if (newEtag.length()) etag = newEtag;
    result = Fetch::Updated;
  } else if (code == 304) {
    result = Fetch::NotModified;
  } else {
    Serial.printf("HTTP GET -> %d\n", code);
    result = Fetch::Failed;
  }
  http.end();
  return result;
}

int sendAction(const Config &c, const char *method, const char *path,
               const char *buttonId) {
  // Только путь от корня: "//host" или "@host" после базового адреса увели
  // бы запрос на чужой сервер вместе с токеном.
  if (!path || path[0] != '/' || path[1] == '/' || strchr(path, '@')) {
    Serial.printf("действие: отклонён путь %s\n", path ? path : "(нет)");
    return -1;
  }
  if (!netOnline()) return -1;

  String url = c.url;
  while (url.endsWith("/")) url.remove(url.length() - 1);
  url += path;

  JsonDocument doc;
  doc["button"] = buttonId ? buttonId : "";
  doc["screen"] = c.screenId;
  String payload;
  serializeJson(doc, payload);

  WiFiClientSecure tls;
  WiFiClient plain;
  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);
  if (!beginRequest(http, tls, plain, url)) return -1;
  if (c.token.length()) http.addHeader("Authorization", "Bearer " + c.token);
  http.addHeader("Content-Type", "application/json");
  const int code = http.sendRequest(method && *method ? method : "POST", payload);
  http.end();
  return code;
}

// Почему устройство последний раз стартовало — видно в журнале сервера по heartbeat.
// brownout — просело питание (кабель, зарядка), panic/wdt — сбой прошивки, sw — сами.
static const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "poweron";
    case ESP_RST_EXT:      return "ext";
    case ESP_RST_SW:       return "sw";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:  return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT:      return "wdt";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    default:               return "other";
  }
}

void sendHeartbeat(const Config &c, const String &etag, uint32_t uptimeSec) {
  if (!netOnline()) return;

  JsonDocument doc;
  doc["fw"]        = FW_VERSION;
  doc["uptime"]    = uptimeSec;
  doc["rssi"]      = WiFi.RSSI();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["last_etag"] = etag;
  doc["reset"]     = resetReason();

  String payload;
  serializeJson(doc, payload);

  WiFiClientSecure tls;
  WiFiClient plain;
  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);
  if (!beginRequest(http, tls, plain, endpoint(c, "/heartbeat"))) return;
  if (c.token.length()) http.addHeader("Authorization", "Bearer " + c.token);
  http.addHeader("Content-Type", "application/json");
  http.POST(payload);
  http.end();
}
