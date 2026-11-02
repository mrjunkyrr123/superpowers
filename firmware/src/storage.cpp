#include "storage.h"
#include <Preferences.h>
#ifdef CACHE_LITTLEFS
#include <LittleFS.h>
#endif
#include "secrets.h"

static Preferences prefs;

void loadConfig(Config &c) {
  prefs.begin("hub75", true);
  c.ssid     = prefs.getString("ssid",   DEFAULT_WIFI_SSID);
  c.pass     = prefs.getString("pass",   DEFAULT_WIFI_PASS);
  c.url      = prefs.getString("url",    DEFAULT_API_URL);
  c.token    = prefs.getString("token",  DEFAULT_API_TOKEN);
  c.screenId = prefs.getString("screen", DEFAULT_SCREEN_ID);
  prefs.end();
}

void saveConfig(const Config &c) {
  prefs.begin("hub75", false);
  prefs.putString("ssid",   c.ssid);
  prefs.putString("pass",   c.pass);
  prefs.putString("url",    c.url);
  prefs.putString("token",  c.token);
  prefs.putString("screen", c.screenId);
  prefs.end();
}

#ifdef CACHE_LITTLEFS
// Раскладка 480x480 весит ~14 КБ и в строку NVS не влезает — кэш в файле.
// Сначала пишем во временный файл, потом переименовываем: обрыв питания
// посреди записи не оставит битый кэш.
static bool fsReady() {
  static int state = -1;                 // -1 не пробовали, 0 нет, 1 есть
  if (state < 0) {
    state = LittleFS.begin(true) ? 1 : 0;  // true — отформатировать при первом запуске
    if (!state) Serial.println(F("LittleFS: не смонтировался, кэша не будет"));
  }
  return state == 1;
}

static bool readFile(const char *path, String &out) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  out = f.readString();
  f.close();
  return true;
}

static bool writeFile(const char *path, const String &data) {
  const String tmp = String(path) + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  const size_t n = f.print(data);
  f.close();
  if (n != data.length()) { LittleFS.remove(tmp); return false; }
  LittleFS.remove(path);
  return LittleFS.rename(tmp, path);
}

bool cacheLoad(String &json, String &etag) {
  if (!fsReady() || !readFile("/cache.json", json)) return false;
  if (!readFile("/cache.etag", etag)) etag = "";
  return json.length() > 0;
}

void cacheSave(const String &json, const String &etag) {
  if (json.length() > CACHE_MAX || !fsReady()) return;
  // ETag пишем первым и пустым: если упадём между файлами, кэш просто
  // перезапросится целиком, а не примет старое тело за новое.
  writeFile("/cache.etag", "");
  if (writeFile("/cache.json", json)) writeFile("/cache.etag", etag);
}
#else
bool cacheLoad(String &json, String &etag) {
  prefs.begin("hub75", true);
  json = prefs.getString("cache", "");
  etag = prefs.getString("cetag", "");
  prefs.end();
  return json.length() > 0;
}

void cacheSave(const String &json, const String &etag) {
  if (json.length() > CACHE_MAX) return;
  prefs.begin("hub75", false);
  prefs.putString("cache", json);
  prefs.putString("cetag", etag);
  prefs.end();
}
#endif

static void printConfig(const Config &c) {
  Serial.println(F("--- конфигурация ---"));
  Serial.printf("ssid   = %s\n", c.ssid.c_str());
  Serial.printf("pass   = %s\n", c.pass.length() ? "(задан)" : "(пусто)");
  Serial.printf("url    = %s\n", c.url.c_str());
  Serial.printf("token  = %s\n", c.token.length() ? "(задан)" : "(пусто)");
  Serial.printf("screen = %s\n", c.screenId.c_str());
  Serial.println(F("команды: set <ssid|pass|url|token|screen> <значение> | save | reboot | show"));
}

void configConsole(Config &c) {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (!line.length()) return;

  if (line == "show") {
    printConfig(c);
  } else if (line == "save") {
    saveConfig(c);
    Serial.println(F("сохранено в NVS"));
  } else if (line == "reboot") {
    ESP.restart();
  } else if (line.startsWith("set ")) {
    const int sp = line.indexOf(' ', 4);
    if (sp < 0) { Serial.println(F("формат: set <поле> <значение>")); return; }
    const String key = line.substring(4, sp);
    const String val = line.substring(sp + 1);
    if      (key == "ssid")   c.ssid = val;
    else if (key == "pass")   c.pass = val;
    else if (key == "url")    c.url = val;
    else if (key == "token")  c.token = val;
    else if (key == "screen") c.screenId = val;
    else { Serial.printf("неизвестное поле: %s\n", key.c_str()); return; }
    Serial.printf("%s задано, не забудьте save\n", key.c_str());
  } else {
    printConfig(c);
  }
}
