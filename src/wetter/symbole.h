// Wettersymbole aus Grafik-Grundformen (keine Bitmaps nötig)
#pragma once
#include <Adafruit_GFX.h>
#include "wetter.h"

// Regentropfen, Spitze oben bei (cx, y), Höhe h
void zeichneTropfen(Adafruit_GFX &g, int16_t cx, int16_t y, int16_t h, uint16_t farbe);

// Zeichnet ein Symbol in das Quadrat (x, y, groesse x groesse).
// farbeAkzent wird für Sonne und Blitz benutzt (z. B. Rot).
void zeichneSymbol(Adafruit_GFX &g, Symbol s, int16_t x, int16_t y, int16_t groesse,
                   uint16_t schwarz, uint16_t weiss, uint16_t farbeAkzent);
