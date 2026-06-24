#include "wifi_manager.h"
#include "rtc_manager.h"

// Funktionen aus wifi_manager.h implementiert

bool wifiSymbolVisible = false;
bool webServerRunning = false;

void refreshWiFiIdleTimer(const __FlashStringHelper *reason) {
    wifiStartTime = millis();

    if (reason != nullptr) {
        Serial.print(F("🔄 WiFi-Idle-Timer aktualisiert: "));
        Serial.println(reason);
    }
}

void clearWiFiSymbol() {
    if (triggerActive) {
        Serial.println(F("⏳ WiFi-Symbol bleibt, weil ein Buchstabe aktiv ist."));
        return;
    }

    if (!wifiSymbolVisible) {
        Serial.println(F("ℹ️ WiFi-Symbol ist bereits ausgeblendet."));
        return;
    }

    Serial.println(F("🚫 WiFi-Symbol wird entfernt."));
    display.fillScreen(display.color565(0, 0, 0));
    display.display();
    wifiSymbolVisible = false;
}

#define SCALE_FACTOR 2

void drawWiFiSymbol() {
    if (!wifi_status_symbol_enabled ||
        wifi_operation_mode != static_cast<uint8_t>(WiFiOperationMode::TimedManager)) {
        Serial.println(F("WiFi-Symbol ist fuer diesen WLAN-Modus deaktiviert."));
        wifiSymbolVisible = false;
        return;
    }

    if (!wifiConnected) {
        Serial.println(F("ℹ️ WiFi-Symbol wird nicht angezeigt: keine aktive WLAN-Verbindung."));
        wifiSymbolVisible = false;
        return;
    }

    if (wifiDisabled) {
        Serial.println(F("ℹ️ WiFi-Symbol bleibt deaktiviert, weil WiFi abgeschaltet ist."));
        wifiSymbolVisible = false;
        return;
    }

    if (triggerActive) {
        Serial.println(F("⏳ WiFi-Symbol NICHT angezeigt, weil ein Buchstabe aktiv ist."));
        wifiSymbolVisible = false;
        return;
    }

    if (wifiSymbolVisible) {
        Serial.println(F("ℹ️ WiFi-Symbol ist bereits aktiv – erneutes Zeichnen entfällt."));
        return;
    }

    Serial.println(F("📶 WiFi-Symbol wird angezeigt."));

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
    wifiSymbolVisible = true;
}

void disableWiFiAndServer() {
    if (wifiDisabled) {
        Serial.println(F("ℹ️ WiFi & Webserver sind bereits deaktiviert."));
        return;
    }

    Serial.println(F("⏹️ Deaktiviere WiFi & Webserver."));

    if (!triggerActive) {
        clearWiFiSymbol();
    } else {
        Serial.println(F("⏳ Aktive Anzeige – WiFi-Symbol bleibt vorerst bestehen."));
    }

    if (webServerRunning) {
        server.end();
        webServerRunning = false;
    } else {
        Serial.println(F("ℹ️ Webserver war bereits gestoppt."));
    }
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    WiFi.softAPdisconnect(true);
    Serial.println(F("ℹ️ SoftAP nach Abschaltung getrennt."));

    wifiConnected = false;
    wifiDisabled = true;
}

void connectWiFi() {
    Serial.println(F("🌐 Verbinde mit WiFi..."));
    WiFi.persistent(false);
    const bool staWithLocalAp = (wifi_operation_mode == static_cast<uint8_t>(WiFiOperationMode::StaWithLocalAp));
    WiFi.mode(staWithLocalAp ? WIFI_AP_STA : WIFI_STA);
    if (staWithLocalAp) {
        const bool apStarted = WiFi.softAP(wifi_local_ap_ssid, wifi_local_ap_password);
        Serial.print(F("Lokaler Box-AP "));
        Serial.println(apStarted ? F("gestartet.") : F("konnte nicht gestartet werden."));
    } else {
        WiFi.softAPdisconnect(true);
        Serial.println(F("STA-Modus aktiviert, SoftAP getrennt."));
    }
    Serial.println(F("ℹ️ STA-Modus aktiviert, SoftAP getrennt."));
    WiFi.hostname(hostname);
    if (wifi_static_ip_enabled) {
        IPAddress localIp;
        IPAddress gateway;
        IPAddress subnet;
        IPAddress dns;
        if (localIp.fromString(wifi_static_ip) &&
            gateway.fromString(wifi_gateway) &&
            subnet.fromString(wifi_subnet) &&
            dns.fromString(wifi_dns)) {
            WiFi.config(localIp, gateway, subnet, dns);
            Serial.print(F("Statische IP konfiguriert: "));
            Serial.println(localIp);
        } else {
            Serial.println(F("Statische IP ungueltig, nutze DHCP."));
            WiFi.config(0U, 0U, 0U);
        }
    } else {
        WiFi.config(0U, 0U, 0U);
        Serial.println(F("DHCP aktiviert."));
    }
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
        wifiDisabled = false;
        drawWiFiSymbol();
        if (!syncTimeWithNTP()) {
            Serial.println(F("⚠️ Hinweis: NTP Synchronisierung beim Verbindungsaufbau fehlgeschlagen."));
        }
        setupWebServer();
        refreshWiFiIdleTimer(F("connectWiFi"));
    } else {
        Serial.println(F("\n⛔ WiFi Timeout! Verbindung fehlgeschlagen. WiFi bleibt aus."));
        wifiConnected = false;
        if (staWithLocalAp) {
            wifiDisabled = false;
            if (!webServerRunning) {
                setupWebServer();
            }
            Serial.println(F("Lokaler Box-AP bleibt fuer Konfiguration aktiv."));
        } else {
            WiFi.disconnect();
            WiFi.mode(WIFI_OFF);
        }
    }
}

void checkWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        const bool persistentMode = (wifi_operation_mode != static_cast<uint8_t>(WiFiOperationMode::TimedManager));
        if (persistentMode && !wifiDisabled) {
            static unsigned long lastReconnectAttempt = 0;
            const unsigned long now = millis();
            if (now - lastReconnectAttempt >= 30000UL) {
                Serial.println(F("WLAN-Verbindung verloren. Permanenter Modus versucht Reconnect..."));
                lastReconnectAttempt = now;
                WiFi.disconnect();
                WiFi.begin(wifi_ssid, wifi_password);
            }
        } else if (!wifiDisabled) {
            Serial.println(F("⚠️ WLAN-Verbindung verloren. Schalte WiFi & Webserver aus..."));
            disableWiFiAndServer();
            Serial.println(F("🌐 Webserver gestoppt. Neustart erforderlich für neue Verbindung."));
        }
    } else {
        if (!wifiConnected) {
            Serial.println(F("✅ WLAN verbunden!"));
            Serial.print(F("IP-Adresse: "));
            Serial.println(WiFi.localIP());

            wifiConnected = true;
            wifiDisabled = false;
            refreshWiFiIdleTimer(F("checkWiFi reconnect"));

            if (!webServerRunning) {
                Serial.println(F("🌐 Webserver war gestoppt – starte Listener neu."));
                server.begin();
                webServerRunning = true;
            }

            if (!triggerActive) {
                drawWiFiSymbol();
            }
        }
    }
}

