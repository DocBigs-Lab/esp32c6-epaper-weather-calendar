#include "termine.h"
#include "config.h"
#include "ha.h"
#include <algorithm>

// Lokale Mitternacht von `heute` + tage als UTC-Zeitstempel für die HA-Abfrage
static String utcZeitstempel(const tm &heute, int tage) {
  tm k = heute;
  k.tm_mday += tage;
  k.tm_hour = k.tm_min = k.tm_sec = 0;
  k.tm_isdst = -1;
  const time_t t = mktime(&k);
  tm u;
  gmtime_r(&t, &u);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &u);
  return buf;
}

// "2026-11-06" oder "2026-11-06T07:30:00+01:00" -> Tage seit 1970, optional Minute des Tages.
// Die Uhrzeit von HA ist in der HA-Zeitzone (hier dieselbe wie die Anzeige).
static bool datumLesen(const char *s, long &tag, int &minute) {
  int j, mo, t, h, mi;
  if (!s || sscanf(s, "%d-%d-%d", &j, &mo, &t) != 3) return false;
  tag = tageSeit1970(j, mo, t);
  minute = (strlen(s) >= 16 && sscanf(s + 11, "%d:%d", &h, &mi) == 2) ? h * 60 + mi : -1;
  return true;
}

static bool kalenderLaden(const String &entity, bool muell, const tm &heute, int tage,
                          std::vector<Termin> &out) {
  const String pfad = "/api/calendars/" + entity + "?start=" + utcZeitstempel(heute, 0) +
                      "&end=" + utcZeitstempel(heute, tage);
  JsonDocument doc;
  if (!haGet(pfad, doc)) return false;

  const long heuteNr = tageSeit1970(heute.tm_year + 1900, heute.tm_mon + 1, heute.tm_mday);
  const int jetztMinute = heute.tm_hour * 60 + heute.tm_min;
  int anzahl = 0, vorbei = 0;
  for (JsonObject e : doc.as<JsonArray>()) {
    const char *start = e["start"]["dateTime"] | (const char *)(e["start"]["date"] | "");
    long tag;
    int minute;
    if (!datumLesen(start, tag, minute)) continue;

    // Termine mit Uhrzeit fallen weg, sobald sie begonnen haben (auch mehrtägige von
    // gestern). Ganztägige Termine bleiben, solange sie laufen.
    if (minute >= 0 && (tag < heuteNr || (tag == heuteNr && minute < jetztMinute))) {
      vorbei++;
      continue;
    }

    Termin t;
    t.muell = muell;
    t.tageAbHeute = max(0L, tag - heuteNr);  // mehrtägig ganztägig und schon begonnen -> ab heute
    if (t.tageAbHeute >= tage) continue;
    t.minuteDesTages = minute;
    String titel = e["summary"] | "";
    titel.trim();
    if (titel.isEmpty()) titel = "(ohne Titel)";
    if (t.minuteDesTages >= 0) {
      char zeit[8];
      snprintf(zeit, sizeof(zeit), "%02d:%02d ", t.minuteDesTages / 60, t.minuteDesTages % 60);
      titel = zeit + titel;
    }
    t.titel = titel;
    out.push_back(t);
    anzahl++;
  }
  Serial.printf("Kalender %s: %d Termine (%d schon begonnen, ausgeblendet)\n", entity.c_str(),
                anzahl, vorbei);
  return true;
}

bool termineLaden(const tm &heute, int tage, std::vector<Termin> &out) {
  out.clear();
  bool ok = true;
  auto ladeListe = [&](String liste, bool muell) {
    while (liste.length()) {
      int komma = liste.indexOf(',');
      String entity = komma < 0 ? liste : liste.substring(0, komma);
      liste = komma < 0 ? "" : liste.substring(komma + 1);
      entity.trim();
      if (entity.length() && !kalenderLaden(entity, muell, heute, tage, out)) ok = false;
    }
  };
  ladeListe(HA_KALENDER_MUELL, true);
  ladeListe(HA_KALENDER, false);
  std::stable_sort(out.begin(), out.end(), [](const Termin &a, const Termin &b) {
    if (a.tageAbHeute != b.tageAbHeute) return a.tageAbHeute < b.tageAbHeute;
    return a.minuteDesTages < b.minuteDesTages;  // ganztägig (-1) zuerst
  });
  return ok;
}
