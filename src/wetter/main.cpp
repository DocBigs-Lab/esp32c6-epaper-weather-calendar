// Morgen-Anzeige: Wetter + Termine auf 4,2" BWR (400x300) am Seengreat ESP32-C6
// Ablauf: aufwachen -> WLAN -> Uhrzeit (NTP) -> Wetter + Termine -> zeichnen -> Deep Sleep
// Aktualisierung stündlich 5-22 Uhr, neu gezeichnet nur bei Änderung. Ab 18 Uhr Vorschau auf morgen. Reset-Taste = sofort.
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <esp_sntp.h>
#include <GxEPD2_3C.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "config.h"
#include "wetter.h"
#include "termine.h"
#include "ha.h"
#include "symbole.h"

#include "secrets.h"  // vorhanden, sonst bricht config.h mit Fehlermeldung ab

// --- Board-Pins (Seengreat ESP32-C6) ---
constexpr int EPD_BUSY = 2, EPD_DC = 3, EPD_MOSI = 4, EPD_SCK = 5, EPD_RST = 6, EPD_CS = 7;
constexpr int LED_PIN = 8;  // grüne LED, "active low": an 3,3 V über 2 kOhm, leuchtet bei LOW
// Im Deep Sleep per Hold auf HIGH gehalten (LED HIGH = aus)
constexpr int SCHLAF_HIGH[] = {EPD_CS, EPD_RST, EPD_DC, LED_PIN};

// Das SES-Panel braucht für den vollen Refresh ~23 s, GxEPD2 wartet aber nur 20 s
// ("Busy Timeout!"). Daher Wartezeit auf 40 s erhöhen.
struct EPD_SES42 : GxEPD2_420c {
  EPD_SES42(int16_t cs, int16_t dc, int16_t rst, int16_t busy) : GxEPD2_420c(cs, dc, rst, busy) {
    _busy_timeout = 40000000;  // µs
  }
};
GxEPD2_3C<EPD_SES42, EPD_SES42::HEIGHT> display(EPD_SES42(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
U8G2_FOR_ADAFRUIT_GFX u8;

constexpr uint16_t SCHWARZ = GxEPD_BLACK, WEISS = GxEPD_WHITE, ROT = GxEPD_RED;
constexpr int16_t W = 400, H = 300;

RTC_DATA_ATTR uint8_t fehlerZaehler = 0;
RTC_DATA_ATTR uint32_t letzterInhalt = 0;  // Prüfsumme des zuletzt gezeichneten Inhalts
RTC_DATA_ATTR time_t geplanterTermin = 0;   // Weckzeitpunkt, für den zuletzt schlafen gelegt wurde

static const char *WOCHENTAG[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch",
                                  "Donnerstag", "Freitag", "Samstag"};
static const char *WOCHENTAG_KURZ[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
static const char *MONAT[] = {"Januar", "Februar", "März", "April", "Mai", "Juni", "Juli",
                              "August", "September", "Oktober", "November", "Dezember"};

// ---------------------------------------------------------------- Hilfen
static String grad(float t) {
  if (isnan(t)) return "--°";
  long r = lroundf(t);
  if (r == 0) r = 0;  // kein "-0"
  return String(r) + "°";
}

static void text(int16_t x, int16_t y, const String &s, const uint8_t *font, uint16_t farbe = SCHWARZ) {
  u8.setFont(font);
  u8.setForegroundColor(farbe);
  u8.setCursor(x, y);
  u8.print(s);
}

static int16_t breite(const String &s, const uint8_t *font) {
  u8.setFont(font);
  return u8.getUTF8Width(s.c_str());
}

// Text auf maxBreite Pixel kürzen ("...")
static String gekuerzt(String s, const uint8_t *font, int16_t maxBreite) {
  if (breite(s, font) <= maxBreite) return s;
  while (s.length() && breite(s + "...", font) > maxBreite) {
    // ganzes UTF-8-Zeichen entfernen (Umlaute sind 2 Byte)
    int i = s.length() - 1;
    while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80) i--;
    s.remove(i);
  }
  s.trim();
  return s + "...";
}

static void textRechts(int16_t xRechts, int16_t y, const String &s, const uint8_t *font, uint16_t farbe = SCHWARZ) {
  text(xRechts - breite(s, font), y, s, font, farbe);
}

static void textMitte(int16_t xMitte, int16_t y, const String &s, const uint8_t *font, uint16_t farbe = SCHWARZ) {
  text(xMitte - breite(s, font) / 2, y, s, font, farbe);
}

static float akkuSpannung() {
  uint32_t summe = 0;
  for (int i = 0; i < 16; i++) summe += analogReadMilliVolts(AKKU_PIN);
  return summe / 16.0f / 1000.0f * AKKU_TEILER;
}

// Akkustand in AKKU_SCHRITT-Schritten, -1 = kein Akku (Versorgung über USB)
static int akkuProzent(float v) {
  // Ohne Akku misst der Teiler die Ladespannung (~4,3 V); ein Li-Ion-Akku liegt bei max. ~4,2 V
  if (v < 2.5f || v > AKKU_USB_AB_V) return -1;
  const float p = (v - AKKU_LEER_V) / (AKKU_VOLL_V - AKKU_LEER_V) * 100;
  return constrain((int)roundf(p / AKKU_SCHRITT) * AKKU_SCHRITT, 0, 100);
}

static void zeichneAkku(int16_t xRechts, int16_t y, float v) {
  // y = Oberkante des Symbols
  const int prozent = akkuProzent(v);
  if (prozent < 0) {
    textRechts(xRechts, y + 12, "USB", u8g2_font_helvR10_tf);
    return;
  }
  const uint16_t farbe = v < AKKU_WARN_V ? ROT : SCHWARZ;
  const int16_t bw = 26, bh = 13, x = xRechts - bw - 3;
  display.drawRect(x, y, bw, bh, farbe);
  display.fillRect(x + bw, y + 4, 3, bh - 8, farbe);
  display.fillRect(x + 2, y + 2, (bw - 4) * prozent / 100, bh - 4, farbe);
  textRechts(x - 4, y + 12, String(prozent) + "%", u8g2_font_helvR10_tf, farbe);
}

// ---------------------------------------------------------------- Layout
// Namen der Tonnen, die morgen abgeholt werden ("" = keine), z. B. "Gelbe Tonne + Biotonne"
static String tonnenFuerMorgen(const std::vector<Termin> &termine) {
  String s;
  for (const auto &t : termine) {
    if (!t.muell || t.tageAbHeute != 1) continue;
    if (s.length()) s += " + ";
    s += t.titel;
  }
  return s;
}

// Kleines Mülltonnen-Symbol, 14 x 16 Pixel, (x, y) = links oben
static void zeichneTonne(int16_t x, int16_t y, uint16_t farbe) {
  display.fillRect(x + 5, y, 4, 2, farbe);       // Griff
  display.fillRect(x, y + 2, 14, 2, farbe);      // Deckel
  display.drawRect(x + 1, y + 5, 12, 11, farbe); // Korpus
  for (int16_t lx : {x + 4, x + 7, x + 10}) display.drawFastVLine(lx, y + 7, 7, farbe);
}

static String warnText(const Wetter &w, bool zeigeMorgen);

static void zeichneAnzeige(const tm &jetzt, bool zeigeMorgen, const Wetter &w,
                           const std::vector<Termin> &termine, bool termineOk, float akkuV,
                           float aussenGemessen) {
  const int tagIdx = zeigeMorgen ? 1 : 0;
  const Tag &t = w.tag[tagIdx];

  // Datum des angezeigten Tages
  tm tagTm = jetzt;
  tagTm.tm_mday += tagIdx;
  tagTm.tm_hour = 12;
  mktime(&tagTm);

  display.fillScreen(WEISS);

  // ---- Kopfzeile
  // Platz bis zur Akkuanzeige (ca. 80 px rechts); bei Bedarf Wochentag abkürzen
  const uint8_t *kopfFont = u8g2_font_helvB14_tf;
  const String vorsatz = zeigeMorgen ? "Morgen: " : "";
  const String datum = String(tagTm.tm_mday) + ". " + MONAT[tagTm.tm_mon];
  String kopf = String(WOCHENTAG[tagTm.tm_wday]) + ", " + datum;
  if (breite(vorsatz + kopf, kopfFont) > W - 8 - 84) kopf = String(WOCHENTAG_KURZ[tagTm.tm_wday]) + ", " + datum;
  int16_t x = 8;
  if (zeigeMorgen) {
    text(x, 23, vorsatz, kopfFont, ROT);
    x += breite(vorsatz, kopfFont);
  }
  text(x, 23, kopf, kopfFont);
  zeichneAkku(W - 6, 9, akkuV);
  display.fillRect(0, 33, W, 2, SCHWARZ);

  // ---- Hauptblock links = JETZT (auch abends): Symbol, Temperatur, Beschreibung
  // groß die gemessene Außentemperatur aus HA, daneben klein die Vorhersage;
  // ohne Messwert nur die Vorhersage
  const int16_t jx = 110, jxMax = 216;  // Bereich zwischen Symbol und rechter Spalte
  int16_t beschrY = 118;
  zeichneSymbol(display, symbolFuer(w.aktuellCode, w.aktuellIstTag), 8, 42, 92, SCHWARZ, WEISS, ROT);
  if (!isnan(aussenGemessen)) {
    const String gross = grad(aussenGemessen);
    const String klein = "(" + grad(w.aktuellTemp) + ")";
    text(jx, 92, gross, u8g2_font_logisoso42_tf);
    const int16_t kx = jx + breite(gross, u8g2_font_logisoso42_tf) + 6;
    if (kx + breite(klein, u8g2_font_helvR14_tf) <= jxMax) {
      text(kx, 92, klein, u8g2_font_helvR14_tf);
    } else {  // z. B. "-12°": daneben kein Platz -> darunter, Beschreibung rückt nach unten
      text(jx + 2, 114, klein, u8g2_font_helvR14_tf);
      beschrY = 132;
    }
  } else {
    text(jx, 92, grad(w.aktuellTemp), u8g2_font_logisoso42_tf);
  }
  // Beschreibung des aktuellen Wetters; bei langen Texten kleinere Schrift
  {
    const char *beschr = textFuer(w.aktuellCode);
    const uint8_t *fonts[] = {u8g2_font_helvB12_tf, u8g2_font_helvB10_tf, u8g2_font_helvR10_tf,
                              u8g2_font_helvR08_tf};
    const uint8_t *f = fonts[3];
    for (const uint8_t *k : fonts) {
      if (jx + 2 + breite(beschr, k) <= jxMax) { f = k; break; }
    }
    text(jx + 2, beschrY, beschr, f);
  }

  // ---- Details rechts = angezeigter Tag (heute bzw. morgen)
  const int16_t dx = 222;
  // Etikett, auf welchen Tag sich Vorhersage und Tageszeiten beziehen
  {
    const char *etikett = zeigeMorgen ? "MORGEN" : "HEUTE";
    const uint8_t *f = u8g2_font_helvB10_tf;
    const int16_t ew = breite(etikett, f) + 12, eh = 16, ey = 41;
    const uint16_t farbe = zeigeMorgen ? ROT : SCHWARZ;
    display.fillRoundRect(dx, ey, ew, eh, 3, farbe);
    u8.setBackgroundColor(farbe);
    text(dx + 6, ey + 13, etikett, f, WEISS);
    u8.setBackgroundColor(WEISS);
  }
  int16_t dy = 72;  // Zeilen bei 72, 92, 112, 132 (Trennlinie bei 136)
  text(dx, dy, "Max " + grad(t.tMax) + "   Min " + grad(t.tMin), u8g2_font_helvR12_tf,
       t.tMin <= WARN_FROST_C ? ROT : SCHWARZ);
  dy += 20;
  char buf[48];
  snprintf(buf, sizeof(buf), "Regen %u%%  %.1f mm", t.regenProzent, t.regenMm);
  if (char *p = strrchr(buf, '.')) *p = ',';  // deutsches Dezimalkomma
  text(dx, dy, buf, u8g2_font_helvR12_tf, t.regenProzent >= WARN_REGEN_PROZENT ? ROT : SCHWARZ);
  dy += 20;
  snprintf(buf, sizeof(buf), "Böen %.0f km/h", t.boeenKmh);
  text(dx, dy, buf, u8g2_font_helvR12_tf, t.boeenKmh >= WARN_BOEEN_KMH ? ROT : SCHWARZ);
  dy += 20;
  snprintf(buf, sizeof(buf), "Sonne %s - %s", t.sonnenaufgang, t.sonnenuntergang);
  text(dx, dy, buf, u8g2_font_helvR12_tf);

  // ---- Drei Tageszeiten
  const int16_t sy = 142;
  display.drawFastHLine(8, sy - 6, W - 16, SCHWARZ);
  const uint8_t slotH[3] = {SLOT_1_H, SLOT_2_H, SLOT_3_H};
  const int16_t spalteB = (W - 16) / 3;
  for (int i = 0; i < 3; i++) {
    const Stunde &s = t.slot[i];
    const String zeit = String(slotH[i]) + " Uhr";
    const String temp = grad(s.temp);
    const bool regen = s.regenProzent >= REGEN_ANZEIGE_AB;  // bei trockenem Wetter weglassen
    snprintf(buf, sizeof(buf), "%u%%", s.regenProzent);
    // Block (Symbol + Temperatur/Regen) in der Spalte mittig ausrichten
    int16_t blockB = max(breite(zeit, u8g2_font_helvB10_tf), (int16_t)(48 + breite(temp, u8g2_font_helvB18_tf)));
    if (regen) blockB = max(blockB, (int16_t)(64 + breite(buf, u8g2_font_helvR10_tf)));
    const int16_t sx = 8 + i * spalteB + (spalteB - blockB) / 2;
    text(sx, sy + 14, zeit, u8g2_font_helvB10_tf);
    zeichneSymbol(display, symbolFuer(s.code, s.istTag), sx, sy + 20, 40, SCHWARZ, WEISS, ROT);
    text(sx + 48, sy + 46, temp, u8g2_font_helvB18_tf);
    if (regen) {
      const uint16_t farbe = s.regenProzent >= WARN_REGEN_PROZENT ? ROT : SCHWARZ;
      zeichneTropfen(display, sx + 54, sy + 52, 11, farbe);
      text(sx + 64, sy + 63, buf, u8g2_font_helvR10_tf, farbe);
    }
  }

  // ---- Warnungen (rot)
  const String warn = warnText(w, zeigeMorgen);

  int16_t ty = 216;
  if (warn.length()) {
    display.fillRect(0, ty - 4, W, 22, ROT);
    u8.setBackgroundColor(ROT);
    textMitte(W / 2, ty + 12, warn, u8g2_font_helvB12_tf, WEISS);
    u8.setBackgroundColor(WEISS);
    ty += 24;
  } else {
    display.drawFastHLine(8, ty, W - 16, SCHWARZ);
    ty += 4;
  }

  // ---- Müll: am ganzen Vortag Banner "Heute Abend rausstellen" (roter Rahmen)
  // Die Tonnen werden früh abgeholt, also muss am Abend vorher rausgestellt werden.
  const String tonnen = tonnenFuerMorgen(termine);
  if (tonnen.length()) {
    const int16_t bh = 26;
    for (int i = 0; i < 3; i++) display.drawRect(4 + i, ty + i, W - 8 - 2 * i, bh - 2 * i, ROT);
    zeichneTonne(14, ty + 5, ROT);
    const uint8_t *f = u8g2_font_helvB12_tf;
    text(36, ty + 18, gekuerzt("Heute Abend rausstellen: " + tonnen, f, W - 36 - 12), f, ROT);
    ty += bh + 2;
  }

  // ---- Termine
  // Passen nicht alle Termine, wird die letzte Zeile zu "+ N weitere Termine".
  // Rechts unten steht "Stand hh:mm": Zeilen auf dieser Höhe werden schmaler gehalten.
  snprintf(buf, sizeof(buf), "Stand %02d:%02d", jetzt.tm_hour, jetzt.tm_min);
  const String stand = buf;
  const int16_t standBreite = breite(stand, u8g2_font_helvR08_tf) + 8;
  const int ersterTag = tagIdx;  // abends: ab morgen
  std::vector<const Termin *> liste;
  for (const auto &ter : termine) {
    if (ter.tageAbHeute < ersterTag) continue;
    if (tonnen.length() && ter.muell && ter.tageAbHeute == 1) continue;  // steht schon im Banner
    liste.push_back(&ter);
  }
  const int maxZeilen = (H - ty) / 19;
  const bool zuViele = (int)liste.size() > maxZeilen;
  const int sichtbar = zuViele ? maxZeilen - 1 : (int)liste.size();
  auto zeilenY = [&](int i) { return (int16_t)(ty + 16 + i * 19); };
  auto maxBreite = [&](int16_t zy, int16_t x) {  // Platz bis zum rechten Rand bzw. bis "Stand"
    return (int16_t)(W - x - 6 - (zy > H - 16 ? standBreite : 0));
  };
  for (int i = 0; i < sichtbar; i++) {
    const Termin &ter = *liste[i];
    const int rel = ter.tageAbHeute - ersterTag;  // 0 = der angezeigte Tag
    String wann;
    if (ter.tageAbHeute == 0) wann = "Heute";
    else if (ter.tageAbHeute == 1) wann = "Morgen";
    else {
      tm d = jetzt;
      d.tm_mday += ter.tageAbHeute;
      d.tm_hour = 12;
      mktime(&d);
      wann = String(WOCHENTAG_KURZ[d.tm_wday]) + " " + d.tm_mday + ".";
    }
    const int16_t zy = zeilenY(i);
    const uint16_t farbe = rel == 0 ? ROT : SCHWARZ;
    const uint8_t *font = rel == 0 ? u8g2_font_helvB12_tf : u8g2_font_helvR12_tf;
    text(8, zy, wann, font, farbe);
    text(84, zy, gekuerzt(ter.titel, font, maxBreite(zy, 84)), font, farbe);
  }
  if (zuViele && maxZeilen > 0) {
    const int rest = (int)liste.size() - sichtbar;
    const String mehr = "+ " + String(rest) + (rest == 1 ? " weiterer Termin" : " weitere Termine");
    text(84, zeilenY(sichtbar), mehr, u8g2_font_helvR10_tf);
  }
  if (liste.empty()) {
    if (termineOk) text(8, ty + 16, "Keine Termine", u8g2_font_helvR12_tf);
    else text(8, ty + 16, "Termine nicht abrufbar (Home Assistant)", u8g2_font_helvR12_tf, ROT);
  }

  // ---- Fußzeile: Stand
  textRechts(W - 6, H - 4, stand, u8g2_font_helvR08_tf);
}

static void zeichneFehler(const char *meldung) {
  display.fillScreen(WEISS);
  text(10, 40, "Keine Aktualisierung", u8g2_font_helvB18_tf, ROT);
  text(10, 75, meldung, u8g2_font_helvR12_tf);
  text(10, 100, "Neuer Versuch in " + String(RETRY_MIN) + " Minuten.", u8g2_font_helvR12_tf);
}

// Text der roten Warnzeile ("" = keine Warnung).
// Tagesansicht: jetzt bis MORGEN_AB_STUNDE; Abendansicht: jetzt bis morgen 24 Uhr (inkl. Nacht).
static String warnText(const Wetter &w, bool zeigeMorgen) {
  const Warnlage &wl = zeigeMorgen ? w.warnAbend : w.warnTag;
  String warn;
  auto add = [&](const char *s) { warn += warn.length() ? "  ·  " : ""; warn += s; };
  if (wl.gewitter) add("Gewitter!");
  if (wl.tMin <= WARN_FROST_C || wl.glatteis) add("Glättegefahr");
  if (wl.schnee) add("Schnee");
  if (wl.regenProzent >= WARN_REGEN_PROZENT) add("Schirm mitnehmen");
  if (wl.boeenKmh >= WARN_BOEEN_KMH) add("Sturmböen");
  return warn;
}

// Prüfsumme über alles, was angezeigt wird (ohne "Stand"-Uhrzeit).
// Gleich wie beim letzten Mal -> Display nicht neu zeichnen.
static uint32_t inhaltPruefsumme(const tm &jetzt, bool zeigeMorgen, const Wetter &w,
                                 const std::vector<Termin> &termine, bool termineOk, float akkuV,
                                 float aussen) {
  const Tag &t = w.tag[zeigeMorgen ? 1 : 0];
  String s;
  s.reserve(400);
  s += String(jetzt.tm_year) + "/" + jetzt.tm_yday + "|" + zeigeMorgen + "|" + akkuProzent(akkuV);
  s += "|" + grad(aussen) + grad(w.aktuellTemp) + w.aktuellCode + w.aktuellIstTag;
  s += "|" + String(t.code) + grad(t.tMax) + grad(t.tMin) + t.regenProzent + "," + lroundf(t.regenMm * 10);
  s += "," + String(lroundf(t.boeenKmh)) + t.sonnenaufgang + t.sonnenuntergang;
  for (const auto &sl : t.slot) {
    s += "|" + grad(sl.temp) + sl.code + sl.istTag + (sl.regenProzent >= REGEN_ANZEIGE_AB ? sl.regenProzent : 0);
  }
  s += "|" + warnText(w, zeigeMorgen);
  s += termineOk ? "|T" : "|-";
  for (const auto &ter : termine) s += "|" + String(ter.tageAbHeute) + (ter.muell ? "M" : "") + ter.titel;
  uint32_t h = 2166136261u;  // FNV-1a
  for (size_t i = 0; i < s.length(); i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
  return h ? h : 1;  // 0 = "nichts gezeichnet"
}

// ---------------------------------------------------------------- Ablauf
// Wird von GxEPD2 in der Warteschleife auf BUSY aufgerufen, solange das Panel arbeitet.
// Beim UC8176 bedeutet BUSY = LOW "beschäftigt". Der ESP schläft, bis BUSY auf HIGH geht,
// spätestens aber 1 s (Sicherheitsnetz, GxEPD2 prüft danach BUSY und seinen Timeout selbst).
// CS, RST und DC werden dabei festgehalten, damit das Panel im Refresh nicht gestört wird.
static void lightSleepBisBusyFrei(const void *) {
  constexpr int halten[] = {EPD_CS, EPD_RST, EPD_DC};
  for (int pin : halten) gpio_hold_en((gpio_num_t)pin);
  gpio_sleep_set_direction((gpio_num_t)EPD_BUSY, GPIO_MODE_INPUT);
  gpio_sleep_set_pull_mode((gpio_num_t)EPD_BUSY, GPIO_PULLUP_ONLY);
  gpio_wakeup_enable((gpio_num_t)EPD_BUSY, GPIO_INTR_HIGH_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(1000000);
  esp_light_sleep_start();
  for (int pin : halten) gpio_hold_dis((gpio_num_t)pin);
}

static void lightSleepAufraeumen() {
  gpio_wakeup_disable((gpio_num_t)EPD_BUSY);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}

static void anzeigen(void (*zeichne)()) {
  // Pins vorab als Ausgang setzen, sonst meldet der Arduino-Core 3.x bei GxEPD2.init()
  // "IO x is not set as GPIO" (harmlos, aber störend im Log)
  for (int pin : {EPD_CS, EPD_DC, EPD_RST}) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.init(115200, true, 2, false, SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  // Interner Pull-up an BUSY: so funktioniert BUSY auch ohne R2 (10k auf dem Board).
  // R2 zieht im Schlaf ~0,33 mA, weil das Panel BUSY dann auf LOW hält (gemessen 0,08 V).
  pinMode(EPD_BUSY, INPUT_PULLUP);
#if LIGHT_SLEEP_REFRESH
  Serial.flush();
  display.epd2.setBusyCallback(lightSleepBisBusyFrei);
#endif
  u8.begin(display);
  u8.setFontMode(1);
  u8.setBackgroundColor(WEISS);
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  const uint32_t t0 = millis();
  do { zeichne(); } while (display.nextPage());
  display.hibernate();
#if LIGHT_SLEEP_REFRESH
  lightSleepAufraeumen();
#endif
  Serial.printf("Display fertig nach %lu ms\n", (unsigned long)(millis() - t0));
}

// Feste IP aus secrets.h setzen (WLAN_IP + WLAN_GATEWAY). Ohne diese Angaben oder bei
// ungültigen Adressen bleibt es bei DHCP.
static void festeIpSetzen() {
#if defined(WLAN_IP) && defined(WLAN_GATEWAY)
#ifndef WLAN_MASKE
#define WLAN_MASKE "255.255.255.0"
#endif
#ifndef WLAN_DNS
#define WLAN_DNS WLAN_GATEWAY
#endif
  IPAddress ip, gw, maske, dns;
  if (ip.fromString(WLAN_IP) && gw.fromString(WLAN_GATEWAY) && maske.fromString(WLAN_MASKE) &&
      dns.fromString(WLAN_DNS) && WiFi.config(ip, gw, maske, dns)) {
    Serial.printf("Feste IP %s\n", WLAN_IP);
  } else {
    Serial.println("Feste IP ungültig (secrets.h) - nutze DHCP");
  }
#endif
}

static bool wlanVerbinden() {
  WiFi.mode(WIFI_STA);
  festeIpSetzen();
#ifdef WLAN_SENDELEISTUNG
  WiFi.setTxPower(WLAN_SENDELEISTUNG);  // nach mode(), vor begin()
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(200);
  if (WiFi.status() != WL_CONNECTED) return false;
  // RSSI zeigt, wie viel Reserve bei reduzierter Sendeleistung bleibt (besser als ca. -70 dBm)
  Serial.printf("WLAN verbunden nach %lu ms, RSSI %d dBm, Sendeleistung %.1f dBm\n",
                (unsigned long)(millis() - t0), WiFi.RSSI(), (int)WiFi.getTxPower() / 4.0f);
  return true;
}

// Funk komplett aus (Treiber gestoppt), damit er während des Display-Refreshs keinen Strom zieht
static void wlanAus() {
  if (WiFi.getMode() == WIFI_OFF) return;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// Die Systemuhr läuft im Deep Sleep mit dem RC-Oszillator weiter und geht dabei ~0,7 % vor
// (gemessen: 191 s nach 7 h). getLocalTime() liefert diese Zeit sofort, noch bevor die
// NTP-Antwort da ist. Deshalb hier warten, bis NTP die Uhr wirklich gestellt hat.
static bool zeitHolen(tm &jetzt) {
  const time_t vorher = time(nullptr);
  const uint32_t t0 = millis();
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTzTime(TZ_INFO, NTP_1, NTP_2);
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && millis() - t0 < 10000) delay(20);
  if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
    // Abweichung der RTC-Zeit gegenüber NTP (nur aussagekräftig nach dem Aufwachen)
    const long abweichung = (long)(vorher + (millis() - t0) / 1000) - (long)time(nullptr);
    if (vorher > 1700000000) Serial.printf("NTP: Uhr ging %ld s vor\n", abweichung);
  } else {
    Serial.println("NTP nicht erreicht - nutze die (ungenaue) RTC-Zeit");
  }
  return getLocalTime(&jetzt, 5000) && jetzt.tm_year > (2024 - 1900);
}

// Sekunden bis zum nächsten Aktualisierungszeitpunkt:
// AKTIV_VON_H..AKTIV_BIS_H alle INTERVALL_MIN Minuten, dazu optional UPDATE_EXTRA_H:UPDATE_EXTRA_M
// Die Schlaf-Uhr (interner RC-Oszillator) geht um 1-2 % ungenau: nach 7 h Nachtpause wacht das
// Board einige Minuten zu früh auf (gemessen: Wachphase kurz vor 5:00). Liegt der geplante Termin
// höchstens 30 min in der Zukunft, gilt dieses Aufwachen als der geplante Termin. Sonst würde
// das Board für denselben Termin kurz darauf noch einmal aufwachen.
static time_t terminBasis(time_t nun) {
  const bool vomWecker = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
  if (vomWecker && geplanterTermin > nun && geplanterTermin - nun <= 30 * 60) return geplanterTermin;
  return nun;
}

// Sekunden bis zum nächsten Aktualisierungszeitpunkt nach `basis` (siehe terminBasis)
static uint64_t sekundenBisNaechstesUpdate(time_t nun, time_t basis) {
  tm b;
  localtime_r(&basis, &b);
  const time_t fruehestens = basis + 60;
  time_t best = 0;
  auto pruefe = [&](int tag, int minuteDesTages) {
    tm k = b;
    k.tm_mday += tag;
    k.tm_hour = minuteDesTages / 60;
    k.tm_min = minuteDesTages % 60;
    k.tm_sec = 0;
    k.tm_isdst = -1;
    const time_t kt = mktime(&k);
    if (kt >= fruehestens && (best == 0 || kt < best)) best = kt;
  };
  for (int tag = 0; tag <= 1; tag++) {
    for (int m = AKTIV_VON_H * 60; m <= AKTIV_BIS_H * 60; m += INTERVALL_MIN) pruefe(tag, m);
    if (UPDATE_EXTRA_H >= 0) pruefe(tag, UPDATE_EXTRA_H * 60 + UPDATE_EXTRA_M);
  }
  geplanterTermin = best;
  return (uint64_t)(best - nun);
}

static void schlafen(uint64_t sekunden) {
  wlanAus();  // ist meist schon aus
  Serial.printf("Deep Sleep für %llu s (%.1f h)\n", sekunden, sekunden / 3600.0);
  Serial.flush();

  // Pins für minimalen und stabilen Schlafstrom vorbereiten:
  // - MOSI/SCK/BUSY hochohmig. MOSI/SCK ziehen die 10k-Pull-ups des SD-Slots auf HIGH,
  //   BUSY treibt das Panel im Schlaf auf LOW (0,08 V). Darum BUSY nie als Ausgang HIGH.
  // - CS und RST HIGH halten, damit der Display-Controller im Deep Sleep bleibt.
  // - DC und LED ebenfalls HIGH halten. Hochohmig schwebten sie (DC ohne Pull, LED-Kathode
  //   bei ~1,5–2 V) und der Schlafstrom schwankte stundenweise zwischen 120 und 200 µA.
  //   DC HIGH kostet nichts (CMOS-Eingang), LED HIGH heißt LED aus.
  // Beim ESP32-C6 hält gpio_hold_en() einzelne Pins im Deep Sleep, auch HP-GPIOs wie GPIO8.
  SPI.end();
  for (int pin : {EPD_MOSI, EPD_SCK, EPD_BUSY}) pinMode(pin, INPUT);
  for (int pin : SCHLAF_HIGH) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    gpio_hold_en((gpio_num_t)pin);
  }
  esp_sleep_enable_timer_wakeup(sekunden * 1000000ULL);
  esp_deep_sleep_start();
}

// Daten für die Zeichenfunktionen (anzeigen() nimmt einen Funktionszeiger)
static tm gJetzt;
static bool gMorgen;
static Wetter gWetter;
static std::vector<Termin> gTermine;
static bool gTermineOk;
static float gAkku;
static float gAussen = NAN;
static const char *gFehler;

void setup() {
  Serial.begin(115200);
  delay(300);
  // Hold aus dem letzten Schlaf lösen, sonst lassen sich LED und Display nicht ansteuern
  for (int pin : SCHLAF_HIGH) gpio_hold_dis((gpio_num_t)pin);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // LED an, solange das Board wach ist
  analogReadResolution(12);
  gAkku = akkuSpannung();
  Serial.printf("Akku: %.2f V, Fehlerzähler: %u\n", gAkku, fehlerZaehler);

  gFehler = nullptr;
  if (!wlanVerbinden()) gFehler = "WLAN nicht erreichbar (" WIFI_SSID ").";
  else if (!zeitHolen(gJetzt)) gFehler = "Uhrzeit (NTP) nicht abrufbar.";
  else if (!wetterLaden(gWetter)) gFehler = "Wetterdaten nicht abrufbar.";

  if (gFehler) {
    Serial.println(gFehler);
    wlanAus();  // sonst sucht der Funk während der Fehleranzeige weiter nach dem Router
    // Nach Einschalten/Reset sofort melden. Beim Aufwachen aus dem Schlaf die alte
    // Anzeige bei einzelnen Aussetzern stehen lassen, erst ab dem 3. Fehler melden.
    const bool manuellerStart = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER;
    if (++fehlerZaehler >= 3 || manuellerStart) {
      anzeigen([] { zeichneFehler(gFehler); });
      letzterInhalt = 0;  // beim nächsten Erfolg auf jeden Fall neu zeichnen
    }
    geplanterTermin = 0;  // Wiederholung ist kein geplanter Termin
    schlafen(RETRY_MIN * 60ULL);
  }
  fehlerZaehler = 0;

  gTermineOk = termineLaden(gJetzt, TERMINE_TAGE, gTermine);
  // Messwert aus Home Assistant ist optional: bei Fehler bleibt es bei der Vorhersage
  gAussen = haSensorWert(HA_SENSOR_AUSSEN, mktime(&gJetzt), HA_MAX_ALTER_MIN);
  Serial.printf("Außen gemessen (HA): %.1f°C\n", gAussen);
  wlanAus();  // bevor das Display (lange) aktualisiert wird

  // Heute/morgen nach dem geplanten Termin entscheiden (falls die Schlaf-Uhr zu früh weckt)
  const time_t nun = mktime(&gJetzt);
  const time_t basis = terminBasis(nun);
  {
    tm b;
    localtime_r(&basis, &b);
    gMorgen = b.tm_hour >= MORGEN_AB_STUNDE;
  }
  if (basis != nun) Serial.printf("%ld s vor dem geplanten Termin aufgewacht\n", (long)(basis - nun));
  Serial.printf("%02d:%02d, zeige %s, %.1f°C, Code %u\n", gJetzt.tm_hour, gJetzt.tm_min,
                gMorgen ? "morgen" : "heute", gWetter.aktuellTemp, gWetter.aktuellCode);

  // Nur neu zeichnen, wenn sich etwas geändert hat (Reset-Taste zeichnet immer)
  const uint32_t inhalt = inhaltPruefsumme(gJetzt, gMorgen, gWetter, gTermine, gTermineOk, gAkku, gAussen);
  const bool manuellerStart = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER;
  if (manuellerStart || inhalt != letzterInhalt) {
    anzeigen([] { zeichneAnzeige(gJetzt, gMorgen, gWetter, gTermine, gTermineOk, gAkku, gAussen); });
    letzterInhalt = inhalt;
  } else {
    Serial.println("Inhalt unverändert, Display bleibt wie es ist");
  }

#ifdef TEST_SCHLAF_MIN
  schlafen(TEST_SCHLAF_MIN * 60ULL);
#else
  schlafen(sekundenBisNaechstesUpdate(nun, basis));
#endif
}

void loop() {}
