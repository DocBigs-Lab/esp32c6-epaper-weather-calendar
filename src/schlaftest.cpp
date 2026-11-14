// Schlafstrom-Test: misst, was das Board ohne Display, WLAN und Programmlogik im Deep Sleep braucht.
// Hochladen: pio run -e schlaftest -t upload   (danach RST drücken)
// Ablauf: LED blinkt 3x, dann Deep Sleep ohne Aufwachen, bis RST gedrückt wird.
#include <Arduino.h>
#include <esp_sleep.h>

constexpr int LED_PIN = 8;  // active low

void setup() {
  pinMode(LED_PIN, OUTPUT);
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(200);
    digitalWrite(LED_PIN, HIGH);
    delay(200);
  }
  // Alle Pins des Display-Anschlusses und die LED hochohmig
  for (int pin : {2, 3, 4, 5, 6, 7, LED_PIN}) pinMode(pin, INPUT);
  esp_deep_sleep_start();  // kein Wecker: schläft bis zum Reset
}

void loop() {}
