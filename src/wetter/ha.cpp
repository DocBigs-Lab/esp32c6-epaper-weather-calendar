#include "ha.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef HA_URL
#define HA_URL ""
#endif
#ifndef HA_TOKEN
#define HA_TOKEN ""
#endif

bool haKonfiguriert() { return strlen(HA_URL) > 0 && strlen(HA_TOKEN) > 0; }

bool haGet(const String &pfad, JsonDocument &doc) {
  if (!haKonfiguriert()) return false;
  const String url = String(HA_URL) + pfad;

  WiFiClient klar;
  WiFiClientSecure sicher;
  sicher.setInsecure();  // HA im Heimnetz, oft mit selbstsigniertem Zertifikat
  WiFiClient &client = url.startsWith("https") ? sicher : klar;

  HTTPClient http;
  http.setTimeout(8000);
  http.setConnectTimeout(5000);
  http.useHTTP10(true);
  if (!http.begin(client, url)) return false;
  http.addHeader("Authorization", "Bearer " HA_TOKEN);
  http.addHeader("Content-Type", "application/json");

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("HA %s: HTTP %d\n", pfad.c_str(), status);
    http.end();
    return false;
  }
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Serial.printf("HA JSON-Fehler: %s\n", err.c_str());
    return false;
  }
  return true;
}

// Algorithmus "days_from_civil" (Howard Hinnant)
long tageSeit1970(int j, int mo, int t) {
  j -= mo <= 2;
  const int era = j / 400;
  const int jahrInEra = j - era * 400;
  const int tagImJahr = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + t - 1;
  const int tagInEra = jahrInEra * 365 + jahrInEra / 4 - jahrInEra / 100 + tagImJahr;
  return era * 146097L + tagInEra - 719468L;
}

// "2026-11-05T04:12:33.123456+00:00" (UTC) -> Unix-Zeit
static time_t isoUtcZuUnix(const char *s) {
  int j, mo, t, h, mi, se;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &j, &mo, &t, &h, &mi, &se) != 6) return 0;
  return (time_t)tageSeit1970(j, mo, t) * 86400 + h * 3600 + mi * 60 + se;
}

float haSensorWert(const char *entityId, time_t jetzt, int maxAlterMin) {
  if (!entityId || !*entityId) return NAN;
  JsonDocument doc;
  if (!haGet(String("/api/states/") + entityId, doc)) return NAN;

  const char *state = doc["state"] | "";
  char *ende = nullptr;
  const float wert = strtof(state, &ende);
  if (ende == state) {  // "unavailable", "unknown", ...
    Serial.printf("HA %s: state \"%s\"\n", entityId, state);
    return NAN;
  }
  const time_t stand = isoUtcZuUnix(doc["last_updated"] | "");
  if (stand > 0 && jetzt - stand > maxAlterMin * 60L) {
    Serial.printf("HA %s: Wert %.1f ist %ld min alt, wird ignoriert\n", entityId, wert,
                  (long)((jetzt - stand) / 60));
    return NAN;
  }
  return wert;
}
