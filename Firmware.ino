#include "config.h"
#include "rtc_manager.h"
#include "wifi_manager.h"
#include "matrix_display.h"
#include "trigger_handler.h"
#include "web_manager.h"

bool triggerActive = false;
unsigned long letterStartTime = 0;
Ticker display_ticker;

AsyncWebServer server(80);

void setup() {
  Serial.begin(19200);
  delay(500);
  Serial.println("🚀 Systemstart...");
  // resetEEPROM();
  // clearDisplay();

  pinMode(GPIO_RS485_ENABLE, OUTPUT);
  digitalWrite(GPIO_RS485_ENABLE, LOW);

  loadConfig();
  enableRTC();
  if (!rtc.begin()) {
    rtc_ok = false;
    Serial.println("⚠️ RTC nicht gefunden!");
  } else {
    rtc_ok = true;
    startTime = getRTCTime();
    Serial.println("⏰ Startzeit: " + startTime);
  }
  enableRS485();

  setupMatrix();
  // testLEDMatrix();
  loadLetterData();
  checkMemoryUsage();

  connectWiFi();
}

void loop() {
    static unsigned long lastDebugTime = 0;

    if (triggerActive) {  
        unsigned long elapsedTime = millis() - letterStartTime;

        // **Nur alle 1000 ms (1 Sekunde) eine Debug-Ausgabe**
        if (millis() - lastDebugTime > 1000) {
            Serial.print("⏳ Anzeige läuft... Verstrichene Zeit: ");
            Serial.println(elapsedTime / 1000);
            lastDebugTime = millis();  // **Speichert den Zeitpunkt der letzten Ausgabe**
        }

        if (elapsedTime >= (letter_display_time * 1000)) {
            Serial.println("🧹 Anzeigezeit abgelaufen, Buchstabe wird gelöscht!");
            clearDisplay();
            triggerActive = false;
        }
    }

    checkWiFi(); 
    checkTrigger();
    checkAutoDisplay();

    if (Serial.available() > 0) {
        char receivedChar = Serial.read();
        if (receivedChar == '1' || receivedChar == '2' || receivedChar == '3') {
            handleTrigger(receivedChar, false);
        } else {
            Serial.print("❌ Falscher Trigger empfangen: ");
            Serial.println(receivedChar);
        }
    }
}