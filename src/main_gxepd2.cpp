// Testbild für das 4,2"-BWR-Panel (SES-imagotag) am Seengreat ESP32-C6 E-Paper Driver Board.
// Prüft Panel, Verkabelung und Farben: pio run -e epd42 -t upload
// Pinbelegung aus den Seengreat-Demos.
#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold12pt7b.h>

// --- Board-Pins (ESP32-C6) ---
constexpr int EPD_BUSY = 2;
constexpr int EPD_DC   = 3;
constexpr int EPD_MOSI = 4;
constexpr int EPD_SCK  = 5;
constexpr int EPD_RST  = 6;
constexpr int EPD_CS   = 7;
constexpr int LED_PIN  = 8;

// --- Panel-Treiber: 4,2" 400x300 BWR, UC8176 (GDEW042Z15) ---
#define EPD_DRIVER GxEPD2_420c
#define EPD_NAME   "4.2\" UC8176"

GxEPD2_3C<EPD_DRIVER, EPD_DRIVER::HEIGHT>
    display(EPD_DRIVER(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

void drawTestScreen() {
  const int16_t w = display.width();
  const int16_t h = display.height();
  display.setRotation(0);
  display.setFont(&FreeMonoBold12pt7b);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.setTextColor(GxEPD_BLACK);
    display.setCursor(20, 40);
    display.print(EPD_NAME);

    display.setTextColor(GxEPD_RED);
    display.setCursor(20, 80);
    display.printf("%dx%d - Rot ok!", w, h);

    // Rahmen genau am Rand: zeigt, ob die Auflösung stimmt
    display.drawRect(0, 0, w, h, GxEPD_BLACK);
    display.drawRect(2, 2, w - 4, h - 4, GxEPD_RED);

    // Farbflächen
    const int16_t top = 100;
    const int16_t boxW = (w - 60) / 2;
    const int16_t boxH = h - top - 20;
    display.fillRect(20, top, boxW, boxH, GxEPD_BLACK);
    display.fillRect(40 + boxW, top, boxW, boxH, GxEPD_RED);
  } while (display.nextPage());
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  // SPI auf die Board-Pins legen (kein MISO)
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  // 115200 = Debug-Ausgabe, true = Initial-Refresh, 2 ms Reset-Puls, false = kein Pulldown-RST
  display.init(115200, true, 2, false, SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));

  Serial.printf("Panel: %s, %dx%d\n", EPD_NAME, display.width(), display.height());
  Serial.println("Zeichne Testbild ...");
  drawTestScreen();  // BWR-Refresh dauert 15-30 s, Flackern ist normal
  display.hibernate();
  Serial.println("Fertig.");
  digitalWrite(LED_PIN, LOW);
}

void loop() {}
