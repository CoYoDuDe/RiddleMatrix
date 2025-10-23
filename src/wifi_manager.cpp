#include "wifi_manager.h"
#include "rtc_manager.h"

// Funktionen aus wifi_manager.h implementiert

bool wifiSymbolVisible = false;

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

    server.end();
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
    WiFi.mode(WIFI_STA);
    WiFi.softAPdisconnect(true);
    Serial.println(F("ℹ️ STA-Modus aktiviert, SoftAP getrennt."));
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
        WiFi.disconnect();
        WiFi.mode(WIFI_OFF);
    }
}

void checkWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        if (!wifiDisabled) {
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

            if (!triggerActive) {
                drawWiFiSymbol();
            }
        }
    }
}

