#include "config.h"

#include <cctype>

// Definitionen globaler Variablen
char wifi_ssid[50] = "";
char wifi_password[50] = "";
char hostname[50] = "";
int wifi_connect_timeout = 30;

static const char DEFAULT_DAILY_LETTERS[NUM_TRIGGERS][NUM_DAYS] = {
    {'A', 'B', 'C', 'D', 'E', 'F', 'G'},
    {'H', 'I', 'J', 'K', 'L', 'M', 'N'},
    {'O', 'P', 'Q', 'R', 'S', 'T', 'U'}};

static const char DEFAULT_DAILY_COLORS[NUM_TRIGGERS][NUM_DAYS][COLOR_STRING_LENGTH] = {
    {"#FF0000", "#00FF00", "#0000FF", "#FFFF00", "#FF00FF", "#00FFFF", "#FFA500"},
    {"#FFFFFF", "#FFD700", "#ADFF2F", "#00CED1", "#9400D3", "#FF69B4", "#1E90FF"},
    {"#FFA07A", "#20B2AA", "#87CEFA", "#FFE4B5", "#DA70D6", "#90EE90", "#FFDAB9"}};

static const unsigned long DEFAULT_TRIGGER_DELAYS[NUM_TRIGGERS][NUM_DAYS] = {
    {0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0}};

char dailyLetters[NUM_TRIGGERS][NUM_DAYS] = {};
char dailyLetterColors[NUM_TRIGGERS][NUM_DAYS][COLOR_STRING_LENGTH] = {};
unsigned long letter_trigger_delays[NUM_TRIGGERS][NUM_DAYS] = {};

int display_brightness;
unsigned long letter_display_time;
unsigned long letter_auto_display_interval;
bool autoDisplayMode;

RTC_DS1307 rtc;
bool rtc_ok = false;
String startTime;

PxMATRIX display(64, 64, P_LAT, P_OE, P_A, P_B, P_C, P_D, P_E);

bool wifiConnected = false;

const char* daysOfTheWeek[7] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};

// Funktionsimplementierungen aus config.h
namespace {

bool isValidHexColor(const char *value) {
    if (value == nullptr) {
        return false;
    }

    size_t length = strlen(value);
    if (length != 7 || value[0] != '#') {
        return false;
    }

    for (size_t i = 1; i < length; ++i) {
        if (!isxdigit(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    return true;
}

void resetLettersToDefaults() {
    memcpy(dailyLetters, DEFAULT_DAILY_LETTERS, sizeof(dailyLetters));
    memcpy(dailyLetterColors, DEFAULT_DAILY_COLORS, sizeof(dailyLetterColors));
}

void resetTriggerDelaysToDefaults() {
    memcpy(letter_trigger_delays, DEFAULT_TRIGGER_DELAYS, sizeof(letter_trigger_delays));
}

bool isValidDelayValue(unsigned long value) {
    return value <= 999UL;
}

} // namespace

void saveConfig() {
    Serial.println(F("💾 Speichere Einstellungen in EEPROM..."));

    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(EEPROM_OFFSET_WIFI_SSID, wifi_ssid);
    EEPROM.put(EEPROM_OFFSET_WIFI_PASSWORD, wifi_password);
    EEPROM.put(EEPROM_OFFSET_HOSTNAME, hostname);
    EEPROM.put(EEPROM_OFFSET_DAILY_LETTERS, dailyLetters);
    EEPROM.put(EEPROM_OFFSET_DAILY_LETTER_COLORS, dailyLetterColors);
    EEPROM.put(EEPROM_OFFSET_DISPLAY_BRIGHTNESS, display_brightness);
    EEPROM.put(EEPROM_OFFSET_LETTER_DISPLAY_TIME, letter_display_time);
    EEPROM.put(EEPROM_OFFSET_TRIGGER_DELAY_MATRIX, letter_trigger_delays);
    EEPROM.put(EEPROM_OFFSET_AUTO_INTERVAL, letter_auto_display_interval);
    uint8_t autoModeByte = autoDisplayMode ? 1 : 0;
    EEPROM.put(EEPROM_OFFSET_AUTO_MODE, autoModeByte);
    EEPROM.put(EEPROM_OFFSET_WIFI_CONNECT_TIMEOUT, wifi_connect_timeout);
    uint16_t version = EEPROM_CONFIG_VERSION;
    EEPROM.put(EEPROM_OFFSET_CONFIG_VERSION, version);
    // Do not write to EEPROM_OFFSET_CONFIG_VERSION_LEGACY here because that
    // address overlaps with the trigger delay matrix in the current layout.
    // We only read from the legacy offset during migration to avoid
    // corrupting saved delay values.
    EEPROM.commit();

    Serial.println(F("✅ Einstellungen erfolgreich gespeichert!"));
}

void loadConfig() {
    Serial.println(F("📂 Lade Einstellungen aus EEPROM..."));

    resetLettersToDefaults();
    resetTriggerDelaysToDefaults();

    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(EEPROM_OFFSET_WIFI_SSID, wifi_ssid);
    EEPROM.get(EEPROM_OFFSET_WIFI_PASSWORD, wifi_password);
    EEPROM.get(EEPROM_OFFSET_HOSTNAME, hostname);

    uint16_t storedVersion = 0xFFFF;
    bool migratedLegacyLayout = false;
    bool configVersionStoredAtLegacyOffset = false;
    EEPROM.get(EEPROM_OFFSET_CONFIG_VERSION, storedVersion);

    if (storedVersion == 0xFFFF || storedVersion == 0x0000) {
        uint16_t legacyVersion = 0xFFFF;
        EEPROM.get(EEPROM_OFFSET_CONFIG_VERSION_LEGACY, legacyVersion);
        if (legacyVersion != 0xFFFF && legacyVersion != 0x0000) {
            storedVersion = legacyVersion;
            configVersionStoredAtLegacyOffset = true;
        }
    }

    if (storedVersion == EEPROM_CONFIG_VERSION) {
        if (configVersionStoredAtLegacyOffset) {
            Serial.println(F("ℹ️ Legacy-Konfigurationsversions-Offset gefunden – aktualisiere auf neues Layout."));
        }
        EEPROM.get(EEPROM_OFFSET_DAILY_LETTERS, dailyLetters);
        EEPROM.get(EEPROM_OFFSET_DAILY_LETTER_COLORS, dailyLetterColors);
    } else {
        Serial.println(F("ℹ️ Legacy-Layout erkannt – migriere auf mehrspurige Trigger-Konfiguration."));
        char legacyLetters[NUM_DAYS] = {};
        char legacyColors[NUM_DAYS][COLOR_STRING_LENGTH] = {};
        EEPROM.get(EEPROM_OFFSET_DAILY_LETTERS, legacyLetters);
        EEPROM.get(EEPROM_OFFSET_DAILY_LETTER_COLORS, legacyColors);

        unsigned long legacyTriggerDelays[NUM_TRIGGERS] = {};
        size_t legacyOffset = EEPROM_OFFSET_TRIGGER_DELAY_MATRIX;
        for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
            unsigned long value = 0;
            EEPROM.get(static_cast<int>(legacyOffset + trigger * sizeof(unsigned long)), value);
            legacyTriggerDelays[trigger] = value;
        }

        for (size_t day = 0; day < NUM_DAYS; ++day) {
            char letter = legacyLetters[day];
            if (letter == '\xFF' || letter == '\0') {
                letter = DEFAULT_DAILY_LETTERS[0][day];
            }

            for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                dailyLetters[trigger][day] = (trigger == 0) ? letter : DEFAULT_DAILY_LETTERS[trigger][day];
            }

            const char *legacyColor = legacyColors[day];
            bool colorValid = isValidHexColor(legacyColor);

            for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
                const char *source = (trigger == 0 && colorValid) ? legacyColor : DEFAULT_DAILY_COLORS[trigger][day];
                strncpy(dailyLetterColors[trigger][day], source, COLOR_STRING_LENGTH);
                dailyLetterColors[trigger][day][COLOR_STRING_LENGTH - 1] = '\0';
            }
        }
        for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
            unsigned long legacyValue = legacyTriggerDelays[trigger];
            bool delayValid = isValidDelayValue(legacyValue);
            for (size_t day = 0; day < NUM_DAYS; ++day) {
                unsigned long fallback = DEFAULT_TRIGGER_DELAYS[trigger][day];
                letter_trigger_delays[trigger][day] = delayValid ? legacyValue : fallback;
            }
            if (!delayValid) {
                Serial.print(F("⚠️ Ungültige Legacy-Verzögerung für Trigger "));
                Serial.print(trigger + 1);
                Serial.println(F(" – Standardwert 0 Sekunden wird verwendet."));
            }
        }
        migratedLegacyLayout = true;
    }

    Serial.println(F("📂 Geladene Farben für Trigger & Tage:"));
    for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
        for (size_t day = 0; day < NUM_DAYS; ++day) {
            Serial.print(F("Trigger "));
            Serial.print(trigger + 1);
            Serial.print(F(", Tag "));
            Serial.print(day);
            Serial.print(F(" → Farbe: "));
            Serial.println(dailyLetterColors[trigger][day]);
        }
    }

    EEPROM.get(EEPROM_OFFSET_DISPLAY_BRIGHTNESS, display_brightness);
    EEPROM.get(EEPROM_OFFSET_LETTER_DISPLAY_TIME, letter_display_time);
    if (storedVersion == EEPROM_CONFIG_VERSION) {
        EEPROM.get(EEPROM_OFFSET_TRIGGER_DELAY_MATRIX, letter_trigger_delays);
    }
    EEPROM.get(EEPROM_OFFSET_AUTO_INTERVAL, letter_auto_display_interval);
    uint8_t autoModeByte = 0;
    EEPROM.get(EEPROM_OFFSET_AUTO_MODE, autoModeByte);
    EEPROM.get(EEPROM_OFFSET_WIFI_CONNECT_TIMEOUT, wifi_connect_timeout);
    autoDisplayMode = (autoModeByte == 1);

    Serial.println(F("✅ EEPROM-Daten geladen!"));

    bool eepromUpdated = migratedLegacyLayout || configVersionStoredAtLegacyOffset;

    if (strlen(wifi_ssid) == 0 || wifi_ssid[0] == '\xFF') {
        Serial.println(F("🛑 Kein gültiges WiFi im EEPROM gefunden! Setze Standardwerte..."));
        strncpy(wifi_ssid, "YOUR_WIFI_SSID", sizeof(wifi_ssid));
        strncpy(wifi_password, "YOUR_WIFI_PASSWORD", sizeof(wifi_password));
        strncpy(hostname, "your-device-hostname", sizeof(hostname));
        wifi_connect_timeout = 30;
        eepromUpdated = true;
    }

    for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
        for (size_t day = 0; day < NUM_DAYS; ++day) {
            char letter = dailyLetters[trigger][day];
            bool letterValid = false;
            for (size_t i = 0; i < sizeof(availableLetters); ++i) {
                if (availableLetters[i] == letter) {
                    letterValid = true;
                    break;
                }
            }

            if (!letterValid) {
                Serial.println(F("🛑 Ungültiger Buchstabe entdeckt! Setze Standardwert."));
                dailyLetters[trigger][day] = DEFAULT_DAILY_LETTERS[trigger][day];
                eepromUpdated = true;
            }

            if (!isValidHexColor(dailyLetterColors[trigger][day])) {
                Serial.println(F("🛑 Ungültige Farbe! Setze Standardwert..."));
                strncpy(dailyLetterColors[trigger][day], DEFAULT_DAILY_COLORS[trigger][day], COLOR_STRING_LENGTH);
                dailyLetterColors[trigger][day][COLOR_STRING_LENGTH - 1] = '\0';
                eepromUpdated = true;
            }
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

    for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
        for (size_t day = 0; day < NUM_DAYS; ++day) {
            unsigned long &delayValue = letter_trigger_delays[trigger][day];
            if (!isValidDelayValue(delayValue)) {
                Serial.print(F("⚠️ Ungültige Verzögerung für Trigger "));
                Serial.print(trigger + 1);
                Serial.print(F(", Tag "));
                Serial.print(day);
                Serial.println(F(" – setze Standardwert 0 Sekunden."));
                delayValue = DEFAULT_TRIGGER_DELAYS[trigger][day];
                eepromUpdated = true;
            }
        }
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

