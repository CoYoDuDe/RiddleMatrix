#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "config.h"
#include "web_manager.h"
#include <ESP8266WiFi.h>

extern bool wifiDisabled;
extern bool triggerActive;
extern bool wifiSymbolVisible;

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

#endif
