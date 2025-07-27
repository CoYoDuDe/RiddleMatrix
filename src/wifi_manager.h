#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "config.h"
#include "web_manager.h"
#include <ESP8266WiFi.h>

extern bool wifiDisabled;
extern bool triggerActive;

// **❌ WiFi-Symbol entfernen, wenn die Verbindung abbricht**
void clearWiFiSymbol();

// **📶 WiFi-Symbol anzeigen, wenn verbunden**
void drawWiFiSymbol();

// **🌐 WiFi verbinden**
void connectWiFi() {
    Serial.println(F("🌐 Verbinde mit WiFi..."));
    WiFi.hostname(hostname);
    WiFi.begin(wifi_ssid, wifi_password);

    unsigned long startAttempt = millis();
    unsigned long lastDot = startAttempt;
    while (WiFi.status() != WL_CONNECTED &&
           (millis() - startAttempt < (wifi_connect_timeout * 1000UL))) {
        if (millis() - lastDot >= 2000) {
            Serial.print(".");
            lastDot = millis();
        }
        delay(10);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F("\n✅ WiFi verbunden!"));
        Serial.print(F("IP-Adresse: "));
        Serial.println(WiFi.localIP());
        wifiConnected = true;
        drawWiFiSymbol();
        syncTimeWithNTP();
        setupWebServer();
    } else {
        Serial.println(F("\n⛔ WiFi Timeout! Verbindung fehlgeschlagen. WiFi bleibt aus."));
        wifiConnected = false;
        WiFi.disconnect();
        WiFi.mode(WIFI_OFF);  
    }
}

// **🔄 WiFi Reconnect**
void checkWiFi();

#endif
