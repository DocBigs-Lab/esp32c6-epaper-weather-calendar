// Home Assistant REST-API (HA_URL und HA_TOKEN aus include/secrets.h)
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>

// true, wenn HA_URL und HA_TOKEN eingetragen sind
bool haKonfiguriert();

// GET {HA_URL}{pfad}, Antwort als JSON in doc. Gibt false bei Fehler zurück.
bool haGet(const String &pfad, JsonDocument &doc);

// Tage seit 1970-01-01 für ein Kalenderdatum (unabhängig von der Zeitzone)
long tageSeit1970(int jahr, int monat, int tag);

// Zahlenwert eines Sensors (z. B. "sensor.aussen_temperatur").
// NAN, wenn nicht erreichbar, "unavailable"/"unknown" oder älter als maxAlterMin Minuten.
float haSensorWert(const char *entityId, time_t jetzt, int maxAlterMin);
