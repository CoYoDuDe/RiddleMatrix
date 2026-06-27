#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "config.h"
#include "web_manager.h"
#include <Arduino.h>
#if defined(ESP32)
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif

extern bool wifiDisabled;
extern bool triggerActive;
extern bool wifiSymbolVisible;

// **📊 Laufzeitstatus des AsyncWebServer**
// Hilft dabei, den Listener bei WLAN-Reconnects gezielt neu zu starten.
extern bool webServerRunning;

// **❌ WiFi-Symbol entfernen, wenn die Verbindung abbricht**
void clearWiFiSymbol();

// **📶 WiFi-Symbol anzeigen, wenn verbunden**
void drawWiFiSymbol();

// **🌐 WiFi verbinden**
void connectWiFi();

// **⏹️ WiFi & Webserver deaktivieren**
void disableWiFiAndServer();

// **🔄 WiFi Reconnect**
void checkWiFi();

// **⏳ Idle-Timer für aktive Web-Nutzung zurücksetzen**
void refreshWiFiIdleTimer(const __FlashStringHelper *reason = nullptr);

void maintainWiFiAccessWindow(unsigned long timeoutMs);

#endif
