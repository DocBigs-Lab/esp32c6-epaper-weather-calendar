// Einstellungen der Wetter-/Termin-Anzeige
// Persönliche Daten (WLAN, Home Assistant, Ort, Entity-IDs) stehen in include/secrets.h.
#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h fehlt - include/secrets.example.h kopieren und ausfüllen"
#endif

// --- Ort (aus secrets.h) ---
#if !defined(ORT_LAT) || !defined(ORT_LON)
#error "ORT_LAT und ORT_LON in include/secrets.h eintragen (siehe secrets.example.h)"
#endif

// --- Zeitzone und Zeitserver ---
#define TZ_INFO    "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_1      "de.pool.ntp.org"
#define NTP_2      "pool.ntp.org"

// --- Aktualisierung ---
// Tagsüber alle INTERVALL_MIN Minuten (AKTIV_VON_H bis einschließlich AKTIV_BIS_H), nachts Pause.
// Neu gezeichnet wird nur, wenn sich der Inhalt geändert hat (spart Akku, kein Flackern).
#define AKTIV_VON_H      5
#define AKTIV_BIS_H      22
#define INTERVALL_MIN    60
// Optionaler zusätzlicher fester Zeitpunkt (Stunde, Minute), z. B. kurz vor dem Losgehen.
// -1 = aus. Kostet ~0,22 mAh pro Tag, wenn dabei neu gezeichnet wird.
#define UPDATE_EXTRA_H   -1
#define UPDATE_EXTRA_M   30
// Ab dieser Stunde wird "morgen" statt "heute" angezeigt
#define MORGEN_AB_STUNDE 18
// Akkuanzeige in diesen Schritten (%), damit nicht jedes Prozent ein Neuzeichnen auslöst
#define AKKU_SCHRITT     5
// Während des Display-Refreshs (~23 s) in den Light Sleep gehen statt wach auf BUSY zu warten.
// 0 = aus (zum Vergleichen beim Messen)
#define LIGHT_SLEEP_REFRESH 1

// Stunden für die drei Vorhersage-Spalten
#define SLOT_1_H  7
#define SLOT_2_H  12
#define SLOT_3_H  17

// WLAN-Sendeleistung. Router in der Nähe (2 m): reduziert reicht und senkt den Strom beim Senden.
// Werte: WIFI_POWER_21dBm (Maximum), 19_5, 19, 18_5, 17, 15, 13, 11, 8_5, 7, 5, 2dBm.
// Zeile auskommentieren = Standard des Treibers (Maximum). Im seriellen Log steht der RSSI:
// liegt er schlechter als ca. -70 dBm oder schlägt die Verbindung fehl, Leistung erhöhen.
// #define WLAN_SENDELEISTUNG WIFI_POWER_8_5dBm

// Wiederholung bei Fehler (kein WLAN / keine Daten), Minuten
#define RETRY_MIN 30

// --- Warnschwellen (rot hervorgehoben) ---
#define WARN_FROST_C       1.0f   // Tiefstwert <= -> Glättegefahr
#define WARN_REGEN_PROZENT 50     // Regenwahrscheinlichkeit >= -> Schirm
#define REGEN_ANZEIGE_AB   10     // darunter wird die Regenwahrscheinlichkeit nicht angezeigt
#define WARN_BOEEN_KMH     50.0f  // Böen >= -> Sturmböen

// --- Home Assistant: gemessene Außentemperatur ---
// HA_SENSOR_AUSSEN in secrets.h; fehlt er, wird nur die Vorhersage angezeigt.
#ifndef HA_SENSOR_AUSSEN
#define HA_SENSOR_AUSSEN   ""
#endif
// Ältere Messwerte gelten als ungültig (z. B. Sensor-Batterie leer)
#define HA_MAX_ALTER_MIN   120

// --- Home Assistant: Kalender für die Termine ---
// HA_KALENDER_MUELL (Abfuhrtermine, am Vortag Banner "rausstellen") und HA_KALENDER (übrige
// Termine) in secrets.h; fehlen sie, werden keine Termine abgefragt.
#ifndef HA_KALENDER_MUELL
#define HA_KALENDER_MUELL  ""
#endif
#ifndef HA_KALENDER
#define HA_KALENDER        ""
#endif
#define TERMINE_TAGE       7      // so viele Tage ab heute werden abgefragt

// --- Akku (GPIO0 über Spannungsteiler 1:2, laut Seengreat-Demo) ---
#define AKKU_PIN        0
#define AKKU_TEILER     2.0f
#define AKKU_LEER_V     3.30f
#define AKKU_VOLL_V     4.15f
#define AKKU_WARN_V     3.50f
#define AKKU_USB_AB_V   4.25f   // darüber: kein Akku angeschlossen, Versorgung über USB
