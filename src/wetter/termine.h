// Termine (Müllabfuhr etc.) aus Home-Assistant-Kalendern
//   GET {HA_URL}/api/calendars/<calendar.entity>?start=...&end=...
//   Header: Authorization: Bearer {HA_TOKEN}
// Kalender in include/secrets.h (jeweils mehrere mit Komma getrennt):
//   HA_KALENDER_MUELL  Abfuhrtermine -> am Vortag Banner "Heute Abend rausstellen"
//   HA_KALENDER        alle anderen Termine
#pragma once
#include <Arduino.h>
#include <vector>
#include <time.h>

struct Termin {
  int tageAbHeute;          // 0 = heute, 1 = morgen, ...
  int minuteDesTages = -1;  // -1 = ganztägig
  String titel;             // bei Terminen mit Uhrzeit: "15:30 Zahnarzt"
  bool muell = false;       // aus HA_KALENDER_MUELL: Tonne am Vortag rausstellen
};

// Lädt Termine ab jetzt für die nächsten `tage` Tage, sortiert nach Datum und Uhrzeit.
// `heute` ist die aktuelle Ortszeit: Termine mit Uhrzeit, die schon begonnen haben, fehlen.
// false, wenn mindestens ein Kalender nicht abgerufen werden konnte.
bool termineLaden(const tm &heute, int tage, std::vector<Termin> &out);
