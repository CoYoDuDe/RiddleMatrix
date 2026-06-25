#include "config.h"
#include "rtc_manager.h"
#include "wifi_manager.h"
#include "trigger_handler.h"
#include "web_manager.h"

bool triggerActive = false;
unsigned long letterStartTime = 0;
unsigned long wifiStartTime = 0;
Ticker display_ticker;

AsyncWebServer server(80);

bool wifiDisabled = false;
bool alreadyCleared = false;

constexpr unsigned long WIFI_IDLE_TIMEOUT_MS = 10UL * 60UL * 1000UL;

static unsigned long getChipRandomSeed() {
#if defined(ESP32)
  const uint64_t chipId = ESP.getEfuseMac();
  return static_cast<unsigned long>(chipId ^ (chipId >> 32));
#else
  return static_cast<unsigned long>(ESP.getChipId());
#endif
}

void setup() {
  Serial.begin(19200);
  delay(500);
  Serial.println(F("🚀 Systemstart..."));
  randomSeed(getChipRandomSeed() ^ micros());
  // clearDisplay();

  webServerRunning = false;

  pinMode(GPIO_RS485_ENABLE, OUTPUT);
  digitalWrite(GPIO_RS485_ENABLE, LOW);

  loadConfig();
  enableRTC();
  if (!rtc.begin()) {
    rtc_ok = false;
    Serial.println(F("⚠️ RTC nicht gefunden!"));
  } else {
    rtc_ok = true;
    startTime = getRTCTime();
    Serial.print(F("⏰ Startzeit: "));
    Serial.println(startTime);
  }
  enableRS485();

  setupMatrix();
  loadLetterData();
  checkMemoryUsage();

  connectWiFi();
  refreshWiFiIdleTimer(F("setup"));
}

void loop() {
    static unsigned long lastDebugTime = 0;
    static unsigned long lastWeekdayUpdate = 0;

    if (triggerActive) {
        unsigned long elapsedTime = millis() - letterStartTime;

        // **Nur alle 1000 ms (1 Sekunde) eine Debug-Ausgabe**
        if (millis() - lastDebugTime > 1000) {
            Serial.print(F("⏳ Anzeige läuft... Verstrichene Zeit: "));
            Serial.println(elapsedTime / 1000);
            lastDebugTime = millis();  // **Speichert den Zeitpunkt der letzten Ausgabe**
        }

        if (elapsedTime >= ((unsigned long)letter_display_time * 1000UL)) {
            Serial.println(F("🧹 Anzeigezeit abgelaufen, Zeichen/Symbol wird gelöscht!"));
            clearDisplay();
            if (!triggerActive && wifiConnected && !wifiDisabled && wifi_status_symbol_enabled) {
                Serial.println(F("🔁 Sicherheits-Check: WiFi-Symbol nach dem Löschen erneut anzeigen."));
                drawWiFiSymbol();
            }
            triggerActive = false;
        }
    }

    const unsigned long nowForWeekday = millis();
    if ((nowForWeekday - lastWeekdayUpdate) >= 500UL) {
        updateCachedWeekday();
        lastWeekdayUpdate = nowForWeekday;
    }

    checkWiFi();
    checkTrigger();
    checkAutoDisplay();
    processPendingTriggers();

    if (wifi_operation_mode == static_cast<uint8_t>(WiFiOperationMode::TimedManager) &&
        !triggerActive && wifiConnected && !wifiDisabled && wifiStartTime != 0) {
        unsigned long now = millis();
        if (now - wifiStartTime >= WIFI_IDLE_TIMEOUT_MS) {
            Serial.println(F("⏱️ Keine Anzeigeaktivität – deaktiviere WiFi & Webserver."));
            disableWiFiAndServer();
        }
    }

}
