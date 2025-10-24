#include "config.h"

#include <algorithm>
#include <cctype>

// Definitionen globaler Variablen
char wifi_ssid[50] = "";
char wifi_password[50] = "";
char hostname[50] = "";
constexpr char DEFAULT_WIFI_SSID[] = "YOUR_WIFI_SSID";
constexpr char DEFAULT_WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
constexpr char DEFAULT_HOSTNAME[] = "your-device-hostname";
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

constexpr uint16_t EEPROM_OFFSET_CONFIG_VERSION_LEGACY = 400;
constexpr uint16_t EEPROM_VERSION_INVALID = 0xFFFF;

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

// Entfernt 0xFF-Bytefolgen sowie nicht druckbare Zeichen und stellt die Terminierung sicher.
template <size_t Length>
void sanitizeColorString(char (&value)[Length]) {
    static_assert(Length == COLOR_STRING_LENGTH, "Falsche Farbfeld-Länge");

    for (size_t index = 0; index < Length - 1; ++index) {
        unsigned char current = static_cast<unsigned char>(value[index]);

        if (current == 0xFF || current == '\0') {
            value[index] = '\0';
            break;
        }

        if (!std::isprint(current)) {
            value[index] = '\0';
            break;
        }
    }

    value[Length - 1] = '\0';
}

template <size_t Days>
void sanitizeColorArray(char (&colors)[Days][COLOR_STRING_LENGTH]) {
    for (size_t day = 0; day < Days; ++day) {
        sanitizeColorString(colors[day]);
    }
}

template <size_t Triggers, size_t Days>
void sanitizeColorMatrix(char (&colors)[Triggers][Days][COLOR_STRING_LENGTH]) {
    for (size_t trigger = 0; trigger < Triggers; ++trigger) {
        sanitizeColorArray(colors[trigger]);
    }
}

void resetLettersToDefaults() {
    memcpy(dailyLetters, DEFAULT_DAILY_LETTERS, sizeof(dailyLetters));
    memcpy(dailyLetterColors, DEFAULT_DAILY_COLORS, sizeof(dailyLetterColors));
    sanitizeColorMatrix(dailyLetterColors);
}

void resetTriggerDelaysToDefaults() {
    memcpy(letter_trigger_delays, DEFAULT_TRIGGER_DELAYS, sizeof(letter_trigger_delays));
}

bool isValidDelayValue(unsigned long value) {
    return value <= 999UL;
}

bool isLikelyValidVersion(uint16_t value) {
    return value != EEPROM_VERSION_INVALID && value != 0x0000 && value <= EEPROM_CONFIG_VERSION;
}

uint16_t readStoredConfigVersion(uint16_t &versionOffset) {
    uint16_t storedVersion = EEPROM_VERSION_INVALID;
    versionOffset = EEPROM_OFFSET_CONFIG_VERSION;
    EEPROM.get(EEPROM_OFFSET_CONFIG_VERSION, storedVersion);

    if (isLikelyValidVersion(storedVersion)) {
        return storedVersion;
    }

    uint16_t legacyVersion = EEPROM_VERSION_INVALID;
    EEPROM.get(EEPROM_OFFSET_CONFIG_VERSION_LEGACY, legacyVersion);
    if (isLikelyValidVersion(legacyVersion)) {
        versionOffset = EEPROM_OFFSET_CONFIG_VERSION_LEGACY;
        return legacyVersion;
    }

    return EEPROM_VERSION_INVALID;
}

void migrateLegacyLayout(uint16_t storedVersion, bool &migratedLegacyLayout) {
    Serial.print(F("ℹ️ Legacy-Layout (Version "));
    if (storedVersion == EEPROM_VERSION_INVALID) {
        Serial.print(F("unbekannt"));
    } else {
        Serial.print(storedVersion);
    }
    Serial.println(F(") erkannt – migriere auf mehrspurige Trigger-Konfiguration."));

    char legacyLetters[NUM_DAYS] = {};
    char legacyColors[NUM_DAYS][COLOR_STRING_LENGTH] = {};
    EEPROM.get(EEPROM_OFFSET_DAILY_LETTERS, legacyLetters);
    EEPROM.get(EEPROM_OFFSET_DAILY_LETTER_COLORS, legacyColors);
    sanitizeColorArray(legacyColors);

    unsigned long legacyTriggerDelays[NUM_TRIGGERS] = {};
    size_t legacyOffset = EEPROM_OFFSET_TRIGGER_DELAY_MATRIX;
    for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
        unsigned long value = 0;
        EEPROM.get(static_cast<int>(legacyOffset + trigger * EEPROM_SIZEOF_UNSIGNED_LONG), value);
        legacyTriggerDelays[trigger] = value;
    }

    for (size_t day = 0; day < NUM_DAYS; ++day) {
        char letter = legacyLetters[day];
        if (static_cast<uint8_t>(letter) == 0xFF || letter == '\0') {
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

    sanitizeColorMatrix(dailyLetterColors);

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

    // Frühere Firmware-Versionen speicherten nur drei Verzögerungswerte. Sobald eine
    // Konfiguration mit Version >= EEPROM_CONFIG_VERSION erkannt wird, stammt sie
    // bereits aus dem aktuellen Layout und darf hier eine vollständige Matrix laden.
    if (storedVersion != EEPROM_VERSION_INVALID && storedVersion >= EEPROM_CONFIG_VERSION) {
        unsigned long recoveredMatrix[NUM_TRIGGERS][NUM_DAYS] = {};
        EEPROM.get(EEPROM_OFFSET_TRIGGER_DELAY_MATRIX, recoveredMatrix);

        for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
            for (size_t day = 0; day < NUM_DAYS; ++day) {
                unsigned long recoveredValue = recoveredMatrix[trigger][day];
                if (isValidDelayValue(recoveredValue)) {
                    letter_trigger_delays[trigger][day] = recoveredValue;
                }
            }
        }
    } else {
        Serial.println(F("✅ Legacy-Verzögerungen repliziert – keine zusätzlichen Matrixdaten übernommen."));
    }

    migratedLegacyLayout = true;
}

} // namespace

void saveConfig() {
    Serial.println(F("💾 Speichere Einstellungen in EEPROM..."));

    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(EEPROM_OFFSET_WIFI_SSID, wifi_ssid);
    EEPROM.put(EEPROM_OFFSET_WIFI_PASSWORD, wifi_password);
    EEPROM.put(EEPROM_OFFSET_HOSTNAME, hostname);
    sanitizeColorMatrix(dailyLetterColors);
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
    EEPROM.commit();

    Serial.println(F("✅ Einstellungen erfolgreich gespeichert!"));
}

void loadConfig() {
    Serial.println(F("📂 Lade Einstellungen aus EEPROM..."));

    resetLettersToDefaults();
    resetTriggerDelaysToDefaults();

    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(EEPROM_OFFSET_WIFI_SSID, wifi_ssid);
    wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
    EEPROM.get(EEPROM_OFFSET_WIFI_PASSWORD, wifi_password);
    wifi_password[sizeof(wifi_password) - 1] = '\0';
    EEPROM.get(EEPROM_OFFSET_HOSTNAME, hostname);
    hostname[sizeof(hostname) - 1] = '\0';

    auto isWhitespaceOnly = [](const char *value, size_t maxLength) {
        bool hasCharacters = false;
        bool hasNonWhitespace = false;
        for (size_t index = 0; index < maxLength; ++index) {
            unsigned char current = static_cast<unsigned char>(value[index]);
            if (current == '\0') {
                break;
            }
            hasCharacters = true;
            if (std::isspace(current) == 0) {
                hasNonWhitespace = true;
            }
        }
        return hasCharacters && !hasNonWhitespace;
    };

    auto containsNonPrintable = [](const char *value, size_t maxLength) {
        for (size_t index = 0; index < maxLength; ++index) {
            unsigned char current = static_cast<unsigned char>(value[index]);
            if (current == '\0') {
                break;
            }
            if (std::isprint(current) == 0) {
                return true;
            }
        }
        return false;
    };

    auto contains0xFF = [](const char *value, size_t maxLength) {
        for (size_t index = 0; index < maxLength; ++index) {
            unsigned char current = static_cast<unsigned char>(value[index]);
            if (current == 0xFF) {
                return true;
            }
            if (current == '\0') {
                break;
            }
        }
        return false;
    };

    auto strnLength = [](const char *value, size_t maxLength) {
        size_t length = 0;
        while (length < maxLength && value[length] != '\0') {
            ++length;
        }
        return length;
    };

    bool migratedLegacyLayout = false;
    uint16_t versionOffset = EEPROM_OFFSET_CONFIG_VERSION;
    uint16_t storedVersion = readStoredConfigVersion(versionOffset);
    bool usingCurrentLayout = (storedVersion == EEPROM_CONFIG_VERSION);

    if (storedVersion == EEPROM_VERSION_INVALID) {
        Serial.println(F("ℹ️ Keine gültige Konfigurationsversion gefunden – gehe von Legacy-Layout aus."));
    } else if (versionOffset == EEPROM_OFFSET_CONFIG_VERSION_LEGACY) {
        Serial.println(F("ℹ️ Legacy-Versionskennung am historischen Offset 0x190 entdeckt."));
    }

    if (usingCurrentLayout) {
        EEPROM.get(EEPROM_OFFSET_DAILY_LETTERS, dailyLetters);
        EEPROM.get(EEPROM_OFFSET_DAILY_LETTER_COLORS, dailyLetterColors);
        sanitizeColorMatrix(dailyLetterColors);
    } else {
        migrateLegacyLayout(storedVersion, migratedLegacyLayout);
    }

    sanitizeColorMatrix(dailyLetterColors);

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
    if (usingCurrentLayout) {
        EEPROM.get(EEPROM_OFFSET_TRIGGER_DELAY_MATRIX, letter_trigger_delays);
    }
    EEPROM.get(EEPROM_OFFSET_AUTO_INTERVAL, letter_auto_display_interval);
    uint8_t autoModeByte = 0;
    EEPROM.get(EEPROM_OFFSET_AUTO_MODE, autoModeByte);
    EEPROM.get(EEPROM_OFFSET_WIFI_CONNECT_TIMEOUT, wifi_connect_timeout);
    autoDisplayMode = (autoModeByte == 1);

    Serial.println(F("✅ EEPROM-Daten geladen!"));

    bool eepromUpdated = migratedLegacyLayout;

    size_t wifiSSIDLength = strnLength(wifi_ssid, sizeof(wifi_ssid));
    bool wifiSSIDEmpty = (wifiSSIDLength == 0);
    bool wifiSSIDWhitespaceOnly = isWhitespaceOnly(wifi_ssid, sizeof(wifi_ssid));
    bool wifiSSIDHasNonPrintable = containsNonPrintable(wifi_ssid, sizeof(wifi_ssid));
    bool wifiSSIDHasFF = contains0xFF(wifi_ssid, sizeof(wifi_ssid));

    if (wifiSSIDHasFF || wifiSSIDEmpty || wifiSSIDWhitespaceOnly || wifiSSIDHasNonPrintable) {
        Serial.println(F("🛑 Kein gültiges WiFi im EEPROM gefunden! Setze Standardwerte..."));
        strncpy(wifi_ssid, DEFAULT_WIFI_SSID, sizeof(wifi_ssid));
        wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
        strncpy(wifi_password, DEFAULT_WIFI_PASSWORD, sizeof(wifi_password));
        wifi_password[sizeof(wifi_password) - 1] = '\0';
        strncpy(hostname, DEFAULT_HOSTNAME, sizeof(hostname));
        hostname[sizeof(hostname) - 1] = '\0';
        wifi_connect_timeout = 30;
        eepromUpdated = true;
    }

    size_t wifiPasswordLength = strnLength(wifi_password, sizeof(wifi_password));
    bool wifiPasswordEmpty = (wifiPasswordLength == 0);
    bool wifiPasswordWhitespaceOnly = isWhitespaceOnly(wifi_password, sizeof(wifi_password));
    bool wifiPasswordHasNonPrintable = containsNonPrintable(wifi_password, sizeof(wifi_password));
    bool wifiPasswordHasFF = contains0xFF(wifi_password, sizeof(wifi_password));

    if (wifiPasswordHasFF || wifiPasswordEmpty || wifiPasswordWhitespaceOnly || wifiPasswordHasNonPrintable) {
        Serial.println(F("🛑 Ungültiges WiFi-Passwort im EEPROM gefunden! Setze Standardwert..."));
        strncpy(wifi_password, DEFAULT_WIFI_PASSWORD, sizeof(wifi_password));
        wifi_password[sizeof(wifi_password) - 1] = '\0';
        eepromUpdated = true;
    }

    size_t hostnameLength = strnLength(hostname, sizeof(hostname));
    bool hostnameEmpty = (hostnameLength == 0);
    bool hostnameWhitespaceOnly = isWhitespaceOnly(hostname, sizeof(hostname));
    bool hostnameHasNonPrintable = containsNonPrintable(hostname, sizeof(hostname));
    bool hostnameHasFF = contains0xFF(hostname, sizeof(hostname));

    if (hostnameHasFF || hostnameEmpty || hostnameWhitespaceOnly || hostnameHasNonPrintable) {
        Serial.println(F("🛑 Ungültiger Hostname im EEPROM gefunden! Setze Standardwert..."));
        strncpy(hostname, DEFAULT_HOSTNAME, sizeof(hostname));
        hostname[sizeof(hostname) - 1] = '\0';
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

    unsigned long originalAutoInterval = letter_auto_display_interval;
    bool restoredDefaultAutoInterval = (originalAutoInterval == 0UL || originalAutoInterval == 0xFFFFFFFFUL);
    if (restoredDefaultAutoInterval) {
        letter_auto_display_interval = 300UL;
    } else {
        letter_auto_display_interval = std::min(std::max(letter_auto_display_interval, 30UL), 600UL);
    }

    if (originalAutoInterval != letter_auto_display_interval) {
        Serial.print(F("⚠️ Automodus-Intervall außerhalb zulässigen Bereichs ("));
        Serial.print(originalAutoInterval);
        if (restoredDefaultAutoInterval) {
            Serial.println(F(" s) – setze auf Standardwert 300 s."));
        } else {
            Serial.println(F(" s) – passe auf 30–600 s an."));
        }
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

void IRAM_ATTR display_updater() {
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

