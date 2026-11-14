#include "symbole.h"
#include <math.h>

static void mond(Adafruit_GFX &g, int16_t cx, int16_t cy, int16_t r, uint16_t farbe, uint16_t weiss) {
  g.fillCircle(cx, cy, r, farbe);
  g.fillCircle(cx + r * 45 / 100, cy - r * 30 / 100, r * 85 / 100, weiss);  // Sichel
}

static void sonne(Adafruit_GFX &g, int16_t cx, int16_t cy, int16_t r, uint16_t farbe) {
  g.fillCircle(cx, cy, r * 55 / 100, farbe);
  for (int i = 0; i < 8; i++) {
    const float a = i * PI / 4;
    const int16_t x0 = cx + cosf(a) * r * 0.72f, y0 = cy + sinf(a) * r * 0.72f;
    const int16_t x1 = cx + cosf(a) * r, y1 = cy + sinf(a) * r;
    g.drawLine(x0, y0, x1, y1, farbe);
    g.drawLine(x0 + 1, y0, x1 + 1, y1, farbe);
    g.drawLine(x0, y0 + 1, x1, y1 + 1, farbe);
  }
}

// Wolke in Box (x, y, b, h); 'schrumpf' verkleinert alle Formen (für Umriss-Effekt)
static void wolkenForm(Adafruit_GFX &g, int16_t x, int16_t y, int16_t b, int16_t h,
                       int16_t schrumpf, uint16_t farbe) {
  const int16_t basisY = y + h * 62 / 100;
  const int16_t rBasis = h * 38 / 100;
  g.fillRoundRect(x + schrumpf, basisY - rBasis + schrumpf, b - 2 * schrumpf,
                  2 * rBasis - 2 * schrumpf, rBasis - schrumpf, farbe);
  g.fillCircle(x + b * 35 / 100, y + h * 45 / 100, h * 32 / 100 - schrumpf, farbe);
  g.fillCircle(x + b * 62 / 100, y + h * 36 / 100, h * 36 / 100 - schrumpf, farbe);
}

static void wolke(Adafruit_GFX &g, int16_t x, int16_t y, int16_t b, int16_t h, bool gefuellt,
                  uint16_t schwarz, uint16_t weiss) {
  wolkenForm(g, x, y, b, h, 0, schwarz);
  if (!gefuellt) wolkenForm(g, x, y, b, h, 3, weiss);
}

void zeichneTropfen(Adafruit_GFX &g, int16_t cx, int16_t y, int16_t h, uint16_t farbe) {
  const int16_t r = h * 35 / 100;
  const int16_t my = y + h - r;  // Mittelpunkt des runden Teils
  g.fillCircle(cx, my, r, farbe);
  g.fillTriangle(cx, y, cx - r, my, cx + r, my, farbe);
}

void zeichneSymbol(Adafruit_GFX &g, Symbol s, int16_t x, int16_t y, int16_t n,
                   uint16_t schwarz, uint16_t weiss, uint16_t akzent) {
  // Wolke belegt den oberen Teil, darunter Platz für Niederschlag
  const int16_t wx = x + n / 20, wy = y + n / 10, wb = n * 9 / 10, wh = n * 55 / 100;
  const int16_t untenY = wy + wh + n / 20;

  switch (s) {
    case Symbol::Sonne:
      sonne(g, x + n / 2, y + n / 2, n * 45 / 100, akzent);
      break;

    case Symbol::Mond:
      mond(g, x + n / 2, y + n / 2, n * 38 / 100, schwarz, weiss);
      break;

    case Symbol::MondWolke:
      mond(g, x + n * 60 / 100, y + n * 32 / 100, n * 26 / 100, schwarz, weiss);
      wolke(g, x, y + n * 38 / 100, n * 75 / 100, n * 50 / 100, false, schwarz, weiss);
      break;

    case Symbol::Heiter:
      sonne(g, x + n * 62 / 100, y + n * 35 / 100, n * 32 / 100, akzent);
      wolke(g, x, y + n * 38 / 100, n * 75 / 100, n * 50 / 100, false, schwarz, weiss);
      break;

    case Symbol::Bewoelkt:
      sonne(g, x + n * 66 / 100, y + n * 30 / 100, n * 26 / 100, akzent);
      wolke(g, x, y + n * 30 / 100, n * 85 / 100, n * 58 / 100, false, schwarz, weiss);
      break;

    case Symbol::Bedeckt:
      // zwei Wolken mit Umriss, die hintere versetzt
      wolke(g, x + n * 25 / 100, y + n * 12 / 100, n * 70 / 100, n * 42 / 100, false, schwarz, weiss);
      wolke(g, x, y + n * 34 / 100, n * 85 / 100, n * 52 / 100, false, schwarz, weiss);
      break;

    case Symbol::Nebel:
      for (int i = 0; i < 5; i++) {
        const int16_t ly = y + n * (20 + i * 15) / 100;
        const int16_t ein = (i % 2) ? n / 10 : 0;
        g.fillRect(x + ein, ly, n - ein - n / 10, 3, schwarz);
      }
      break;

    case Symbol::Niesel:
    case Symbol::Regen:
    case Symbol::Schauer: {
      if (s == Symbol::Schauer) sonne(g, x + n * 70 / 100, y + n * 22 / 100, n * 22 / 100, akzent);
      wolke(g, wx, wy, wb, wh, s != Symbol::Niesel, schwarz, weiss);
      const int tropfen = (s == Symbol::Niesel) ? 3 : 4;
      const int16_t laenge = (s == Symbol::Niesel) ? n / 10 : n / 5;
      for (int i = 0; i < tropfen; i++) {
        const int16_t tx = wx + wb * (20 + i * 60 / (tropfen - 1)) / 100;
        for (int d = 0; d < 2; d++)
          g.drawLine(tx + d, untenY, tx + d - laenge / 3, untenY + laenge, schwarz);
      }
      break;
    }

    case Symbol::Schnee: {
      wolke(g, wx, wy, wb, wh, false, schwarz, weiss);
      const int16_t r = n / 14;
      for (int i = 0; i < 3; i++) {
        const int16_t sx = wx + wb * (22 + i * 28) / 100;
        const int16_t sy = untenY + r + (i % 2) * r;
        g.drawLine(sx - r, sy, sx + r, sy, schwarz);
        g.drawLine(sx, sy - r, sx, sy + r, schwarz);
        g.drawLine(sx - r * 7 / 10, sy - r * 7 / 10, sx + r * 7 / 10, sy + r * 7 / 10, schwarz);
        g.drawLine(sx - r * 7 / 10, sy + r * 7 / 10, sx + r * 7 / 10, sy - r * 7 / 10, schwarz);
      }
      break;
    }

    case Symbol::Gewitter: {
      wolke(g, wx, wy, wb, wh, true, schwarz, weiss);
      const int16_t bx = x + n / 2, by = untenY - n / 20;
      g.fillTriangle(bx, by, bx - n / 8, by + n / 4, bx + n / 40, by + n / 4, akzent);
      g.fillTriangle(bx - n / 40, by + n / 5, bx + n / 8, by + n / 5, bx - n / 12, by + n * 42 / 100, akzent);
      break;
    }
  }
}
