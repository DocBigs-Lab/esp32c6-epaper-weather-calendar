// Vorlage: nach include/secrets.h kopieren und ausfüllen.
// secrets.h wird nicht weitergegeben (siehe .gitignore).
#pragma once

#define WIFI_SSID     "MeinWLAN"
#define WIFI_PASSWORD "geheim"

// Optional feste IP statt DHCP (spart pro Aufwachen die DHCP-Anfrage).
// Adresse außerhalb des DHCP-Bereichs des Routers wählen oder dort für das Board reservieren.
// Auskommentiert lassen = DHCP. WLAN_MASKE und WLAN_DNS sind optional
// (Standard 255.255.255.0 bzw. das Gateway als DNS-Server).
// #define WLAN_IP       "192.168.178.50"
// #define WLAN_GATEWAY  "192.168.178.1"
// #define WLAN_MASKE    "255.255.255.0"
// #define WLAN_DNS      "192.168.178.1"

// Home Assistant (Außentemperatur und Termine). Leer lassen = ohne HA.
// Falls homeassistant.local nicht aufgelöst wird: IP-Adresse eintragen.
#define HA_URL        "http://homeassistant.local:8123"
#define HA_TOKEN      ""   // Long-Lived Access Token aus dem HA-Profil

// Entity-IDs aus HA (Einstellungen -> Geräte & Dienste -> Entitäten).
// Leer lassen = Funktion aus (nur Vorhersage bzw. keine Termine).
#define HA_SENSOR_AUSSEN "sensor.aussentemperatur"
// Kalender, mehrere jeweils mit Komma trennen:
// HA_KALENDER_MUELL: Abfuhrtermine -> am Vortag Banner "Heute Abend rausstellen"
// HA_KALENDER:       alle übrigen Termine
#define HA_KALENDER_MUELL "calendar.muell"
#define HA_KALENDER       "calendar.familie"

// Ort für die Wettervorhersage (Breiten-/Längengrad, z. B. aus Google Maps)
#define ORT_LAT          52.5200f
#define ORT_LON          13.4050f
