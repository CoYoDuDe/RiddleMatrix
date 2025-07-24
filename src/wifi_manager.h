#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "config.h"
#include "matrix_display.h"
#include "web_manager.h"
#include <ESP8266WiFi.h>

bool wifiDisabled = false;
extern bool triggerActive; 

// **WiFi-Manager: Lade WiFi-Daten aus EEPROM, falls vorhanden**
void loadWiFiConfig() {
    Serial.println(F("🔄 Lade WiFi-Konfiguration aus EEPROM..."));
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, wifi_ssid);
    EEPROM.get(50, wifi_password);
    EEPROM.get(100, hostname);

    // **Falls EEPROM leer, bleiben die Werte aus config.h bestehen**
    if (wifi_ssid.length() == 0 || wifi_ssid == " " || wifi_ssid == "\0") {
        Serial.println(F("⚠️ Kein gültiges WiFi im EEPROM gefunden. Nutze Standardwerte aus config.h."));
    } else {
        Serial.println(F("✅ WiFi-Konfiguration geladen!"));
    }
}

// **❌ WiFi-Symbol entfernen, wenn die Verbindung abbricht**
void clearWiFiSymbol() {
    if (!triggerActive) {
        Serial.println(F("🚫 WiFi-Symbol wird entfernt."));
        display.fillScreen(display.color565(0, 0, 0));
        display.display();
    } else {
        Serial.println(F("⏳ WiFi-Symbol bleibt, weil ein Buchstabe aktiv ist."));
    }
}

// **📶 WiFi-Symbol anzeigen, wenn verbunden**
#define SCALE_FACTOR 2

void drawWiFiSymbol() {
    if (wifiConnected) {  
        Serial.println(F("📶 WiFi-Symbol wird angezeigt."));
        
        if (!triggerActive) {
            display.fillScreen(display.color565(0, 0, 255));
            
            int x_pos = (64 - (32 * SCALE_FACTOR)) / 2;
            int y_pos = (64 - (32 * SCALE_FACTOR)) / 2;

            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 32; x++) {
                    uint8_t rowValue = pgm_read_byte(&letterData['~'][y * 4 + (x / 8)]);
                    if (rowValue & (1 << (7 - (x % 8)))) {
                        display.fillRect(x_pos + x * SCALE_FACTOR, y_pos + y * SCALE_FACTOR, SCALE_FACTOR, SCALE_FACTOR, display.color565(0, 0, 0));
                    }
                }
            }
            display.display();
        } else {
            Serial.println(F("⏳ WiFi-Symbol NICHT angezeigt, weil ein Buchstabe aktiv ist."));
        }
    }
}

// **🌐 WiFi verbinden**
void connectWiFi() {
    Serial.println(F("🌐 Verbinde mit WiFi..."));
    WiFi.hostname(hostname.c_str());
    WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < 3200) {
        attempt++;
        delay(50);
        if (attempt % 40 == 0) Serial.print(F("."));
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F("\n✅ WiFi verbunden!"));
        Serial.print(F("IP-Adresse: "));
        Serial.println(WiFi.localIP());
        wifiConnected = true;
        drawWiFiSymbol();
        setupWebServer();
    } else {
        Serial.println(F("\n⛔ WiFi Timeout! Verbindung fehlgeschlagen. WiFi bleibt aus."));
        wifiConnected = false;
        WiFi.disconnect();
        WiFi.mode(WIFI_OFF);  
    }
}

// **🔄 WiFi Reconnect**
void checkWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        if (!wifiDisabled) {
            Serial.println(F("⚠️ WLAN-Verbindung verloren. Schalte WiFi & Webserver aus..."));
            wifiConnected = false;
            
            if (!triggerActive) {
                clearWiFiSymbol();
            }
            
            WiFi.disconnect();
            WiFi.mode(WIFI_OFF);
            
            server.end();
            Serial.println(F("🌐 Webserver gestoppt. Neustart erforderlich für neue Verbindung."));
            
            wifiDisabled = true;
        }
    } else {
        if (!wifiConnected) {
            Serial.println(F("✅ WLAN verbunden!"));
            Serial.print(F("IP-Adresse: "));
            Serial.println(WiFi.localIP());

            wifiConnected = true;
            wifiDisabled = false;

            if (!triggerActive) {
                drawWiFiSymbol();
            }
        }
    }
}

#endif