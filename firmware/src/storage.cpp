#include "storage.h"
#include <Preferences.h>
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
