// Wetterdaten von Open-Meteo (kostenlos, ohne API-Schlüssel)
#pragma once
#include <Arduino.h>

enum class Symbol : uint8_t {
  Sonne, Heiter, Mond, MondWolke, Bewoelkt, Bedeckt, Nebel, Niesel, Regen, Schauer, Schnee, Gewitter
};

struct Stunde {
  float temp = NAN;
  uint8_t code = 0;
  uint8_t regenProzent = 0;
  bool istTag = true;
};

// Was für die Warnzeile zählt, ermittelt aus den Stundenwerten
struct Warnlage {
  float tMin = NAN;
  uint8_t regenProzent = 0;
  float boeenKmh = 0;
  bool gewitter = false, schnee = false, glatteis = false;
};

struct Tag {
  uint8_t code = 0;
  float tMax = NAN, tMin = NAN;
  float regenMm = 0;
  uint8_t regenProzent = 0;
  float boeenKmh = 0;
  char sonnenaufgang[6] = "--:--";
  char sonnenuntergang[6] = "--:--";
  Stunde slot[3];  // SLOT_1_H, SLOT_2_H, SLOT_3_H
};

struct Wetter {
  float aktuellTemp = NAN;
  uint8_t aktuellCode = 0;
  bool aktuellIstTag = true;
  Tag tag[2];  // 0 = heute, 1 = morgen
  // Zeiträume für die Warnzeile, jeweils ab der aktuellen Stunde:
  Warnlage warnTag;    // bis MORGEN_AB_STUNDE (Tagesansicht)
  Warnlage warnAbend;  // bis morgen 24 Uhr, inkl. der Nacht (Abendansicht)
};

bool wetterLaden(Wetter &w);

// istTag=false: bei klarem/heiterem Himmel Mond statt Sonne
Symbol symbolFuer(uint8_t wmoCode, bool istTag = true);
const char *textFuer(uint8_t wmoCode);
bool istGlatteisCode(uint8_t wmoCode);
