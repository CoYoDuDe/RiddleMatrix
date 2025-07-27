#include "config.h"

// Definitionen globaler Variablen
char wifi_ssid[50] = "";
char wifi_password[50] = "";
char hostname[50] = "";
int wifi_connect_timeout = 30;

char dailyLetters[7] = {'A', 'B', 'C', 'D', 'E', 'F', 'G'};
char dailyLetterColors[7][8] = {"#FFFFFF", "#FFFFFF", "#FFFFFF", "#FFFFFF", "#FFFFFF", "#FFFFFF", "#FFFFFF"};

int display_brightness;
unsigned long letter_display_time;
unsigned long letter_trigger_delay_1;
unsigned long letter_trigger_delay_2;
unsigned long letter_trigger_delay_3;
unsigned long letter_auto_display_interval;
bool autoDisplayMode;

RTC_DS1307 rtc;
bool rtc_ok = false;
String startTime;

PxMATRIX display(64, 64, P_LAT, P_OE, P_A, P_B, P_C, P_D, P_E);

bool wifiConnected = false;

const char* daysOfTheWeek[7] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};

// Funktionsimplementierungen aus config.h
void saveConfig() {
    Serial.println(F("💾 Speichere Einstellungen in EEPROM..."));

    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(0, wifi_ssid);
    EEPROM.put(50, wifi_password);
    EEPROM.put(100, hostname);
    EEPROM.put(150, dailyLetters);
    EEPROM.put(200, dailyLetterColors);
    EEPROM.put(300, display_brightness);
    EEPROM.put(304, letter_display_time);
    EEPROM.put(308, letter_trigger_delay_1);
    EEPROM.put(312, letter_trigger_delay_2);
    EEPROM.put(316, letter_trigger_delay_3);
    EEPROM.put(320, letter_auto_display_interval);
    uint8_t autoModeByte = autoDisplayMode ? 1 : 0;
    EEPROM.put(324, autoModeByte);
    EEPROM.put(328, wifi_connect_timeout);
    EEPROM.commit();

    Serial.println(F("✅ Einstellungen erfolgreich gespeichert!"));
}

void loadConfig() {
    Serial.println(F("📂 Lade Einstellungen aus EEPROM..."));

    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, wifi_ssid);
    EEPROM.get(50, wifi_password);
    EEPROM.get(100, hostname);
    EEPROM.get(150, dailyLetters);
    EEPROM.get(200, dailyLetterColors);

    Serial.println(F("📂 Lade Farben aus EEPROM:"));
    for (int i = 0; i < 7; i++) {
        Serial.print(F("Wochentag "));
        Serial.print(i);
        Serial.print(F(" → Geladene Farbe: "));
        Serial.println(dailyLetterColors[i]);
    }

    EEPROM.get(300, display_brightness);
    EEPROM.get(304, letter_display_time);
    EEPROM.get(308, letter_trigger_delay_1);
    EEPROM.get(312, letter_trigger_delay_2);
    EEPROM.get(316, letter_trigger_delay_3);
    EEPROM.get(320, letter_auto_display_interval);
    uint8_t autoModeByte = 0;
    EEPROM.get(324, autoModeByte);
    EEPROM.get(328, wifi_connect_timeout);
    autoDisplayMode = (autoModeByte == 1);

    Serial.println(F("✅ EEPROM-Daten geladen!"));

    bool eepromUpdated = false;

    if (strlen(wifi_ssid) == 0 || wifi_ssid[0] == '\xFF') {
        Serial.println(F("🛑 Kein gültiges WiFi im EEPROM gefunden! Setze Standardwerte..."));
        strncpy(wifi_ssid, "YOUR_WIFI_SSID", sizeof(wifi_ssid));
        strncpy(wifi_password, "YOUR_WIFI_PASSWORD", sizeof(wifi_password));
        strncpy(hostname, "your-device-hostname", sizeof(hostname));
        wifi_connect_timeout = 30;
        eepromUpdated = true;
    }

    if (dailyLetters[0] == '\xFF' || dailyLetters[0] == '\0') {
        Serial.println(F("🛑 Fehler: EEPROM hat ungültige Buchstaben gespeichert. Setze Standardwerte."));
        char defaultLetters[7] = {'A', 'B', 'C', 'D', 'E', 'F', 'G'};
        memcpy(dailyLetters, defaultLetters, sizeof(defaultLetters));
        eepromUpdated = true;
    }

    for (int i = 0; i < 7; i++) {
        if (strlen(dailyLetterColors[i]) == 0 || strcmp(dailyLetterColors[i], "#000000") == 0 || strlen(dailyLetterColors[i]) < 3) {
            Serial.println(F("🛑 Ungültige Farben! Setze Standardwerte..."));
            const char* defaultColors[7] = {"#FF0000", "#00FF00", "#0000FF", "#FFFF00", "#FF00FF", "#00FFFF", "#FFA500"};
            strncpy(dailyLetterColors[i], defaultColors[i], sizeof(dailyLetterColors[i]));
            eepromUpdated = true;
        }
    }

    if (display_brightness < 1 || display_brightness > 255) {
        Serial.println(F("🛑 Ungültige Helligkeit! Setze Standardwert..."));
        display_brightness = 100;
        eepromUpdated = true;
    }

    if (letter_display_time < 1 || letter_display_time > 60) {
        Serial.println(F("🛑 Ungültige Buchstaben-Anzeigezeit! Setze Standardwert..."));
        letter_display_time = 10;
        eepromUpdated = true;
    }

    if (letter_trigger_delay_1 > 999) {
        letter_trigger_delay_1 = 180;
        eepromUpdated = true;
    }

    if (letter_trigger_delay_2 > 999) {
        letter_trigger_delay_2 = 180;
        eepromUpdated = true;
    }

    if (letter_trigger_delay_3 > 999) {
        letter_trigger_delay_3 = 180;
        eepromUpdated = true;
    }

    if (letter_auto_display_interval < 1 || letter_auto_display_interval > 999) {
        Serial.println("🛑 Ungültiges Automodus-Intervall! Setze Standardwert...");
        letter_auto_display_interval = 300;
        eepromUpdated = true;
    }

    if (wifi_connect_timeout < 1 || wifi_connect_timeout > 300) {
        Serial.println("🛑 Ungültiger WiFi-Timeout! Setze Standardwert...");
        wifi_connect_timeout = 30;
        eepromUpdated = true;
    }

    if (autoModeByte > 1) {
        Serial.println(F("⚠️ Ungültiger Wert für autoDisplayMode! Setze Standard auf false."));
        autoDisplayMode = false;
        eepromUpdated = true;
    }

    if (eepromUpdated) {
        Serial.println(F("💾 Standardwerte wurden gesetzt und gespeichert!"));
        saveConfig();
    }
}

void display_updater() {
    display.display();
}

void setupMatrix() {
    display.begin(32);
    display.setBrightness(display_brightness);
    display.setFastUpdate(false);
    display.setDriverChip(FM6126A);
    display_ticker.attach(0.005, display_updater);
    display.clearDisplay();
    display.display();
}

void checkMemoryUsage() {
    Serial.print(F("📝 Freier Speicher: "));
    Serial.println(ESP.getFreeHeap());
}

