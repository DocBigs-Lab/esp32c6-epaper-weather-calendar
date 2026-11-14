#include "wetter.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

static void kopiereUhrzeit(char *ziel, const char *iso) {
  // "2026-11-04T07:02" -> "07:02"
  const char *t = iso ? strchr(iso, 'T') : nullptr;
  if (t && strlen(t) >= 6) {
    memcpy(ziel, t + 1, 5);
    ziel[5] = '\0';
  }
}

// Warnlage aus den Stundenwerten [von, bis)
static Warnlage warnlage(JsonObject h, int von, int bis) {
  Warnlage w;
  for (int i = von; i < bis; i++) {
    const float t = h["temperature_2m"][i] | NAN;
    if (!isnan(t) && (isnan(w.tMin) || t < w.tMin)) w.tMin = t;
    w.regenProzent = max<uint8_t>(w.regenProzent, h["precipitation_probability"][i] | 0);
    w.boeenKmh = max(w.boeenKmh, h["wind_gusts_10m"][i] | 0.0f);
    const uint8_t code = h["weather_code"][i] | 0;
    w.gewitter |= symbolFuer(code) == Symbol::Gewitter;
    w.schnee |= symbolFuer(code) == Symbol::Schnee;
    w.glatteis |= istGlatteisCode(code);
  }
  return w;
}

bool wetterLaden(Wetter &w) {
  char url[400];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,weather_code,is_day"
           "&hourly=temperature_2m,weather_code,precipitation_probability,is_day,wind_gusts_10m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,"
           "precipitation_probability_max,wind_gusts_10m_max,sunrise,sunset"
           "&timezone=Europe%%2FBerlin&forecast_days=2",
           ORT_LAT, ORT_LON);

  WiFiClientSecure client;
  client.setInsecure();  // kein Zertifikats-Check; reicht für öffentliche Wetterdaten
  HTTPClient http;
  http.setTimeout(15000);
  http.useHTTP10(true);  // kein Chunked-Encoding, damit der JSON-Parser direkt vom Stream lesen kann
  if (!http.begin(client, url)) return false;

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("Open-Meteo HTTP %d\n", status);
    http.end();
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Serial.printf("JSON-Fehler: %s\n", err.c_str());
    return false;
  }

  w.aktuellTemp = doc["current"]["temperature_2m"] | NAN;
  w.aktuellCode = doc["current"]["weather_code"] | 0;
  w.aktuellIstTag = (doc["current"]["is_day"] | 1) != 0;
  // aktuelle Stunde aus "2026-11-05T10:15"
  const char *zeit = doc["current"]["time"] | "";
  const int stunde = strlen(zeit) >= 13 ? atoi(zeit + 11) : 0;

  JsonObject d = doc["daily"];
  JsonObject h = doc["hourly"];
  const uint8_t slotStunden[3] = {SLOT_1_H, SLOT_2_H, SLOT_3_H};

  for (int i = 0; i < 2; i++) {
    Tag &t = w.tag[i];
    t.code = d["weather_code"][i] | 0;
    t.tMax = d["temperature_2m_max"][i] | NAN;
    t.tMin = d["temperature_2m_min"][i] | NAN;
    t.regenMm = d["precipitation_sum"][i] | 0.0f;
    t.regenProzent = d["precipitation_probability_max"][i] | 0;
    t.boeenKmh = d["wind_gusts_10m_max"][i] | 0.0f;
    kopiereUhrzeit(t.sonnenaufgang, d["sunrise"][i]);
    kopiereUhrzeit(t.sonnenuntergang, d["sunset"][i]);

    for (int s = 0; s < 3; s++) {
      const int idx = i * 24 + slotStunden[s];  // stündliche Werte beginnen um 00:00 heute
      t.slot[s].temp = h["temperature_2m"][idx] | NAN;
      t.slot[s].code = h["weather_code"][idx] | 0;
      t.slot[s].regenProzent = h["precipitation_probability"][idx] | 0;
      t.slot[s].istTag = (h["is_day"][idx] | 1) != 0;
    }
  }
  const int ab = constrain(stunde, 0, 23);
  w.warnTag = warnlage(h, ab, max(ab + 1, (int)MORGEN_AB_STUNDE));
  w.warnAbend = warnlage(h, ab, 48);
  return !isnan(w.tag[0].tMax);
}

Symbol symbolFuer(uint8_t c, bool istTag) {
  switch (c) {
    case 0: return istTag ? Symbol::Sonne : Symbol::Mond;
    case 1: return istTag ? Symbol::Heiter : Symbol::MondWolke;
    case 2: return Symbol::Bewoelkt;
    case 3: return Symbol::Bedeckt;
    case 45: case 48: return Symbol::Nebel;
    case 51: case 53: case 55: case 56: case 57: return Symbol::Niesel;
    case 61: case 63: case 65: case 66: case 67: return Symbol::Regen;
    case 80: case 81: case 82: return Symbol::Schauer;
    case 71: case 73: case 75: case 77: case 85: case 86: return Symbol::Schnee;
    case 95: case 96: case 99: return Symbol::Gewitter;
    default: return Symbol::Bewoelkt;
  }
}

const char *textFuer(uint8_t c) {
  switch (c) {
    case 0: return "Klar";
    case 1: return "Heiter";
    case 2: return "Teils bewölkt";
    case 3: return "Bedeckt";
    case 45: return "Nebel";
    case 48: return "Reifnebel";
    case 51: return "Leichter Niesel";
    case 53: return "Nieselregen";
    case 55: return "Starker Niesel";
    case 56: case 57: return "Gefrierender Niesel";
    case 61: return "Leichter Regen";
    case 63: return "Regen";
    case 65: return "Starker Regen";
    case 66: case 67: return "Gefrierender Regen";
    case 71: return "Leichter Schneefall";
    case 73: return "Schneefall";
    case 75: return "Starker Schneefall";
    case 77: return "Schneegriesel";
    case 80: return "Leichte Schauer";
    case 81: return "Regenschauer";
    case 82: return "Starke Schauer";
    case 85: case 86: return "Schneeschauer";
    case 95: return "Gewitter";
    case 96: case 99: return "Gewitter mit Hagel";
    default: return "?";
  }
}

bool istGlatteisCode(uint8_t c) {
  return c == 48 || c == 56 || c == 57 || c == 66 || c == 67;
}
