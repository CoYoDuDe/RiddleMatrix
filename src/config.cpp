#include "config.h"

#include <algorithm>
#include <cctype>

// Definitionen globaler Variablen
char wifi_ssid[50] = "";
char wifi_password[50] = "";
char hostname[50] = "";
// Default AP credentials for freshly flashed boxes. These must match the
// RiddleMatrix manager/hotspot defaults so unconfigured boxes can join.
constexpr char DEFAULT_WIFI_SSID[] = "RiddleMatrix_AP";
constexpr char DEFAULT_WIFI_PASSWORD[] = "RiddleMatrix-Setup!";
const char DEFAULT_INFRA_WIFI_SSID[] = "RiddleMatrix_WLAN";
const char DEFAULT_INFRA_WIFI_PASSWORD[] = "ChangeMe-RiddleMatrix!";
constexpr char DEFAULT_HOSTNAME[] = "your-device-hostname";
constexpr char DEFAULT_WIFI_STATIC_IP[] = "192.168.137.50";
constexpr char DEFAULT_WIFI_GATEWAY[] = "192.168.137.1";
constexpr char DEFAULT_WIFI_SUBNET[] = "255.255.255.0";
constexpr char DEFAULT_WIFI_DNS[] = "192.168.137.1";
constexpr char DEFAULT_WIFI_LOCAL_AP_SSID[] = "RiddleMatrix-Box";
constexpr char DEFAULT_WIFI_LOCAL_AP_PASSWORD[] = "RiddleMatrix-Setup!";
int wifi_connect_timeout = 30;
uint8_t wifi_operation_mode = static_cast<uint8_t>(WiFiOperationMode::TimedManager);
bool wifi_status_symbol_enabled = true;
bool wifi_static_ip_enabled = false;
char wifi_static_ip[16] = "";
char wifi_gateway[16] = "";
char wifi_subnet[16] = "";
char wifi_dns[16] = "";
char wifi_local_ap_ssid[50] = "";
char wifi_local_ap_password[50] = "";

static const char DEFAULT_DAILY_LETTERS[NUM_TRIGGERS][NUM_DAYS] = {
    {'A', 'B', 'C', 'D', 'E', 'F', 'G'},
    {'H', 'I', 'J', 'K', 'L', 'M', 'N'},
    {'O', 'P', 'Q', 'R', 'S', 'T', 'U'}};

static const char DEFAULT_DAILY_COLORS[NUM_TRIGGERS][NUM_DAYS][COLOR_STRING_LENGTH] = {
    {"#FF0000", "#00FF00", "#0000FF", "#FFFF00", "#FF00FF", "#00FFFF", "#FFA500"},
    {"#FFFFFF", "#FFD700", "#ADFF2F", "#00CED1", "#9400D3", "#FF69B4", "#1E90FF"},
    {"#FFA07A", "#20B2AA", "#87CEFA", "#FFE4B5", "#DA70D6", "#90EE90", "#FFDAB9"}};

const char *const randomColorPalette[RANDOM_COLOR_PALETTE_SIZE] = {
    "#FF0000", "#00FF00", "#0000FF", "#FFFF00",
    "#FF00FF", "#00FFFF", "#FFA500", "#FFFFFF"};

const char *const randomColorPaletteLabels[RANDOM_COLOR_PALETTE_SIZE] = {
    "Rot", "Gruen", "Blau", "Gelb",
    "Magenta", "Cyan", "Orange", "Weiss"};

static const unsigned long DEFAULT_TRIGGER_DELAYS[NUM_TRIGGERS][NUM_DAYS] = {
    {0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0}};

char dailyLetters[NUM_TRIGGERS][NUM_DAYS] = {};
char dailyLetterColors[NUM_TRIGGERS][NUM_DAYS][COLOR_STRING_LENGTH] = {};
uint8_t dailyLetterColorModes[NUM_TRIGGERS][NUM_DAYS] = {};
uint16_t dailyLetterRandomPaletteMasks[NUM_TRIGGERS][NUM_DAYS] = {};
unsigned long letter_trigger_delays[NUM_TRIGGERS][NUM_DAYS] = {};
uint8_t customSymbolBitmaps[CUSTOM_SYMBOL_COUNT][SYMBOL_BITMAP_SIZE] = {};
uint8_t customSymbolEnabled[CUSTOM_SYMBOL_COUNT] = {};
char random_symbol_pool[RANDOM_SYMBOL_POOL_LENGTH] = {};

int display_brightness;
unsigned long letter_display_time;
unsigned long letter_auto_display_interval;
bool autoDisplayMode;
uint16_t standalone_active_start_minutes;
uint16_t standalone_active_end_minutes;

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
constexpr uint16_t EEPROM_CONFIG_VERSION_WITH_AUTH = 4;
constexpr uint16_t EEPROM_CONFIG_VERSION_WITHOUT_ACTIVITY_WINDOW = 5;
constexpr uint16_t EEPROM_CONFIG_VERSION_WITH_ACTIVITY_WINDOW = 6;
constexpr uint16_t EEPROM_CONFIG_VERSION_WITH_COLOR_MODES = 7;
constexpr uint16_t EEPROM_CONFIG_VERSION_WITH_WIFI_MODES = 8;
constexpr uint16_t EEPROM_CONFIG_VERSION_WITH_EDITABLE_SYMBOLS = 9;
constexpr uint16_t DEFAULT_ACTIVE_START_MINUTES = 0;
constexpr uint16_t DEFAULT_ACTIVE_END_MINUTES = 1439;
constexpr char DEFAULT_RANDOM_SYMBOL_POOL[] = "#&";

constexpr size_t LEGACY_AUTH_TOKEN_MAX_LENGTH = 64;

uint16_t getFullRandomPaletteMask() {
    uint16_t mask = 0;
    for (size_t index = 0; index < RANDOM_COLOR_PALETTE_SIZE; ++index) {
        mask |= static_cast<uint16_t>(1U << index);
    }
    return mask;
}

void copyDefaultString(char *destination, size_t destinationSize, const char *value) {
    strncpy(destination, value, destinationSize);
    destination[destinationSize - 1] = '\0';
}

void resetNetworkExtensionDefaults() {
    wifi_operation_mode = static_cast<uint8_t>(WiFiOperationMode::TimedManager);
    wifi_status_symbol_enabled = true;
    wifi_static_ip_enabled = false;
    copyDefaultString(wifi_static_ip, sizeof(wifi_static_ip), DEFAULT_WIFI_STATIC_IP);
    copyDefaultString(wifi_gateway, sizeof(wifi_gateway), DEFAULT_WIFI_GATEWAY);
    copyDefaultString(wifi_subnet, sizeof(wifi_subnet), DEFAULT_WIFI_SUBNET);
    copyDefaultString(wifi_dns, sizeof(wifi_dns), DEFAULT_WIFI_DNS);
    copyDefaultString(wifi_local_ap_ssid, sizeof(wifi_local_ap_ssid), DEFAULT_WIFI_LOCAL_AP_SSID);
    copyDefaultString(wifi_local_ap_password, sizeof(wifi_local_ap_password), DEFAULT_WIFI_LOCAL_AP_PASSWORD);
}

bool isSupportedRandomPoolSymbol(char letter) {
    if (letter == '\0' || letter == '*') {
        return false;
    }
    for (size_t index = 0; index < sizeof(availableLetters); ++index) {
        if (availableLetters[index] == letter) {
            return true;
        }
    }
    return false;
}

void resetRandomSymbolPoolToDefault() {
    copyDefaultString(random_symbol_pool, sizeof(random_symbol_pool), DEFAULT_RANDOM_SYMBOL_POOL);
}

bool sanitizeRandomSymbolPool() {
    char sanitized[RANDOM_SYMBOL_POOL_LENGTH] = {};
    size_t writeIndex = 0;

    for (size_t index = 0; index < RANDOM_SYMBOL_POOL_LENGTH - 1; ++index) {
        char current = random_symbol_pool[index];
        if (current == '\0') {
            break;
        }
        current = static_cast<char>(std::toupper(static_cast<unsigned char>(current)));
        if (!isSupportedRandomPoolSymbol(current)) {
            continue;
        }
        bool duplicate = false;
        for (size_t existing = 0; existing < writeIndex; ++existing) {
            if (sanitized[existing] == current) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && writeIndex < RANDOM_SYMBOL_POOL_LENGTH - 1) {
            sanitized[writeIndex++] = current;
        }
    }

    if (writeIndex == 0) {
        copyDefaultString(sanitized, sizeof(sanitized), DEFAULT_RANDOM_SYMBOL_POOL);
    }

    const bool changed = strncmp(random_symbol_pool, sanitized, RANDOM_SYMBOL_POOL_LENGTH) != 0;
    memcpy(random_symbol_pool, sanitized, RANDOM_SYMBOL_POOL_LENGTH);
    random_symbol_pool[RANDOM_SYMBOL_POOL_LENGTH - 1] = '\0';
    return changed;
}

bool isValidOperationModeValue(uint8_t mode) {
    return mode <= static_cast<uint8_t>(WiFiOperationMode::StaWithLocalAp);
}

bool isValidIPv4Literal(const char *value) {
    IPAddress parsed;
    return value != nullptr && parsed.fromString(value);
}

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

    for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
        for (size_t day = 0; day < NUM_DAYS; ++day) {
            dailyLetterColorModes[trigger][day] = static_cast<uint8_t>(LetterColorMode::Fixed);
            dailyLetterRandomPaletteMasks[trigger][day] = getFullRandomPaletteMask();
        }
    }
}

void resetTriggerDelaysToDefaults() {
    memcpy(letter_trigger_delays, DEFAULT_TRIGGER_DELAYS, sizeof(letter_trigger_delays));
}

void resetCustomSymbolsToDefaults() {
    memset(customSymbolBitmaps, 0, sizeof(customSymbolBitmaps));
    memset(customSymbolEnabled, 0, sizeof(customSymbolEnabled));
}

bool isValidDelayValue(unsigned long value) {
    return value <= 999UL;
}

bool isValidActiveMinuteValue(uint16_t value) {
    return value <= 1439U;
}

bool isValidColorModeValue(uint8_t value) {
    return value <= static_cast<uint8_t>(LetterColorMode::RandomAll);
}

bool hasSelectedRandomPaletteColor(uint16_t mask) {
    return (mask & getFullRandomPaletteMask()) != 0U;
}

size_t strnLength(const char *value, size_t maxLength) {
    size_t length = 0;
    while (length < maxLength && value[length] != '\0') {
        ++length;
    }
    return length;
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

void loadConfigFromVersion4Layout() {
    constexpr uint16_t LEGACY_OFFSET_AUTH_TOKEN = EEPROM_OFFSET_HOSTNAME + 50;
    constexpr uint16_t LEGACY_OFFSET_AUTH_FLAG = LEGACY_OFFSET_AUTH_TOKEN + LEGACY_AUTH_TOKEN_MAX_LENGTH;
    constexpr uint16_t LEGACY_OFFSET_DAILY_LETTERS = LEGACY_OFFSET_AUTH_FLAG + sizeof(uint8_t);
    constexpr uint16_t LEGACY_OFFSET_DAILY_LETTER_COLORS =
        LEGACY_OFFSET_DAILY_LETTERS + (NUM_TRIGGERS * NUM_DAYS);
    constexpr uint16_t LEGACY_OFFSET_DISPLAY_BRIGHTNESS = LEGACY_OFFSET_DAILY_LETTER_COLORS +
                                                          (NUM_TRIGGERS * NUM_DAYS * COLOR_STRING_LENGTH);
    constexpr uint16_t LEGACY_OFFSET_LETTER_DISPLAY_TIME = LEGACY_OFFSET_DISPLAY_BRIGHTNESS + sizeof(int);
    constexpr uint16_t LEGACY_OFFSET_TRIGGER_DELAY_MATRIX =
        LEGACY_OFFSET_LETTER_DISPLAY_TIME + EEPROM_SIZEOF_UNSIGNED_LONG;
    constexpr uint16_t LEGACY_OFFSET_AUTO_INTERVAL =
        LEGACY_OFFSET_TRIGGER_DELAY_MATRIX + EEPROM_TRIGGER_DELAY_MATRIX_SIZE;
    constexpr uint16_t LEGACY_OFFSET_AUTO_MODE = LEGACY_OFFSET_AUTO_INTERVAL + EEPROM_SIZEOF_UNSIGNED_LONG;
    constexpr uint16_t LEGACY_OFFSET_WIFI_CONNECT_TIMEOUT = LEGACY_OFFSET_AUTO_MODE + sizeof(uint8_t);

    (void)LEGACY_OFFSET_AUTH_TOKEN;
    (void)LEGACY_OFFSET_AUTH_FLAG;

    EEPROM.get(LEGACY_OFFSET_DAILY_LETTERS, dailyLetters);
    EEPROM.get(LEGACY_OFFSET_DAILY_LETTER_COLORS, dailyLetterColors);
    sanitizeColorMatrix(dailyLetterColors);
    EEPROM.get(LEGACY_OFFSET_TRIGGER_DELAY_MATRIX, letter_trigger_delays);
    EEPROM.get(LEGACY_OFFSET_AUTO_INTERVAL, letter_auto_display_interval);
    uint8_t autoModeByte = 0;
    EEPROM.get(LEGACY_OFFSET_AUTO_MODE, autoModeByte);
    autoDisplayMode = (autoModeByte == 1);
    EEPROM.get(LEGACY_OFFSET_DISPLAY_BRIGHTNESS, display_brightness);
    EEPROM.get(LEGACY_OFFSET_LETTER_DISPLAY_TIME, letter_display_time);
    EEPROM.get(LEGACY_OFFSET_WIFI_CONNECT_TIMEOUT, wifi_connect_timeout);
    standalone_active_start_minutes = DEFAULT_ACTIVE_START_MINUTES;
    standalone_active_end_minutes = DEFAULT_ACTIVE_END_MINUTES;
}

void loadConfigFromVersion5Layout() {
    EEPROM.get(EEPROM_OFFSET_DAILY_LETTERS, dailyLetters);
    EEPROM.get(EEPROM_OFFSET_DAILY_LETTER_COLORS, dailyLetterColors);
    sanitizeColorMatrix(dailyLetterColors);
    EEPROM.get(EEPROM_OFFSET_TRIGGER_DELAY_MATRIX, letter_trigger_delays);
    EEPROM.get(EEPROM_OFFSET_AUTO_INTERVAL, letter_auto_display_interval);
    uint8_t autoModeByte = 0;
    EEPROM.get(EEPROM_OFFSET_AUTO_MODE, autoModeByte);
    autoDisplayMode = (autoModeByte == 1);
    EEPROM.get(EEPROM_OFFSET_DISPLAY_BRIGHTNESS, display_brightness);
    EEPROM.get(EEPROM_OFFSET_LETTER_DISPLAY_TIME, letter_display_time);
    EEPROM.get(EEPROM_OFFSET_WIFI_CONNECT_TIMEOUT, wifi_connect_timeout);
    standalone_active_start_minutes = DEFAULT_ACTIVE_START_MINUTES;
    standalone_active_end_minutes = DEFAULT_ACTIVE_END_MINUTES;
}

void loadConfigFromVersion6Layout() {
    EEPROM.get(EEPROM_OFFSET_DAILY_LETTERS, dailyLetters);
    EEPROM.get(EEPROM_OFFSET_DAILY_LETTER_COLORS, dailyLetterColors);
    sanitizeColorMatrix(dailyLetterColors);
    EEPROM.get(EEPROM_OFFSET_TRIGGER_DELAY_MATRIX, letter_trigger_delays);
    EEPROM.get(EEPROM_OFFSET_AUTO_INTERVAL, letter_auto_display_interval);
    uint8_t autoModeByte = 0;
    EEPROM.get(EEPROM_OFFSET_AUTO_MODE, autoModeByte);
    autoDisplayMode = (autoModeByte == 1);
    EEPROM.get(EEPROM_OFFSET_DISPLAY_BRIGHTNESS, display_brightness);
    EEPROM.get(EEPROM_OFFSET_LETTER_DISPLAY_TIME, letter_display_time);
    EEPROM.get(EEPROM_OFFSET_WIFI_CONNECT_TIMEOUT, wifi_connect_timeout);
    EEPROM.get(EEPROM_OFFSET_ACTIVE_START_MINUTES, standalone_active_start_minutes);
    EEPROM.get(EEPROM_OFFSET_ACTIVE_END_MINUTES, standalone_active_end_minutes);
}

void loadConfigFromVersion7Layout() {
    EEPROM.get(EEPROM_OFFSET_DAILY_LETTERS, dailyLetters);
    EEPROM.get(EEPROM_OFFSET_DAILY_LETTER_COLORS, dailyLetterColors);
    sanitizeColorMatrix(dailyLetterColors);
    EEPROM.get(EEPROM_OFFSET_TRIGGER_DELAY_MATRIX, letter_trigger_delays);
    EEPROM.get(EEPROM_OFFSET_AUTO_INTERVAL, letter_auto_display_interval);
    uint8_t autoModeByte = 0;
    EEPROM.get(EEPROM_OFFSET_AUTO_MODE, autoModeByte);
    autoDisplayMode = (autoModeByte == 1);
    EEPROM.get(EEPROM_OFFSET_DISPLAY_BRIGHTNESS, display_brightness);
    EEPROM.get(EEPROM_OFFSET_LETTER_DISPLAY_TIME, letter_display_time);
    EEPROM.get(EEPROM_OFFSET_WIFI_CONNECT_TIMEOUT, wifi_connect_timeout);
    EEPROM.get(EEPROM_OFFSET_ACTIVE_START_MINUTES, standalone_active_start_minutes);
    EEPROM.get(EEPROM_OFFSET_ACTIVE_END_MINUTES, standalone_active_end_minutes);
    EEPROM.get(EEPROM_OFFSET_COLOR_MODE_MATRIX, dailyLetterColorModes);
    EEPROM.get(EEPROM_OFFSET_COLOR_PALETTE_MASK_MATRIX, dailyLetterRandomPaletteMasks);
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
    EEPROM.put(EEPROM_OFFSET_ACTIVE_START_MINUTES, standalone_active_start_minutes);
    EEPROM.put(EEPROM_OFFSET_ACTIVE_END_MINUTES, standalone_active_end_minutes);
    EEPROM.put(EEPROM_OFFSET_COLOR_MODE_MATRIX, dailyLetterColorModes);
    EEPROM.put(EEPROM_OFFSET_COLOR_PALETTE_MASK_MATRIX, dailyLetterRandomPaletteMasks);
    EEPROM.put(EEPROM_OFFSET_WIFI_OPERATION_MODE, wifi_operation_mode);
    uint8_t statusSymbolByte = wifi_status_symbol_enabled ? 1 : 0;
    uint8_t staticIpByte = wifi_static_ip_enabled ? 1 : 0;
    EEPROM.put(EEPROM_OFFSET_WIFI_STATUS_SYMBOL_ENABLED, statusSymbolByte);
    EEPROM.put(EEPROM_OFFSET_WIFI_STATIC_IP_ENABLED, staticIpByte);
    EEPROM.put(EEPROM_OFFSET_WIFI_STATIC_IP, wifi_static_ip);
    EEPROM.put(EEPROM_OFFSET_WIFI_GATEWAY, wifi_gateway);
    EEPROM.put(EEPROM_OFFSET_WIFI_SUBNET, wifi_subnet);
    EEPROM.put(EEPROM_OFFSET_WIFI_DNS, wifi_dns);
    EEPROM.put(EEPROM_OFFSET_WIFI_LOCAL_AP_SSID, wifi_local_ap_ssid);
    EEPROM.put(EEPROM_OFFSET_WIFI_LOCAL_AP_PASSWORD, wifi_local_ap_password);
    EEPROM.put(EEPROM_OFFSET_CUSTOM_SYMBOL_BITMAPS, customSymbolBitmaps);
    EEPROM.put(EEPROM_OFFSET_CUSTOM_SYMBOL_ENABLED, customSymbolEnabled);
    sanitizeRandomSymbolPool();
    EEPROM.put(EEPROM_OFFSET_RANDOM_SYMBOL_POOL, random_symbol_pool);
    EEPROM.commit();

    Serial.println(F("✅ Einstellungen erfolgreich gespeichert!"));
}

void loadConfig() {
    Serial.println(F("📂 Lade Einstellungen aus EEPROM..."));

    resetLettersToDefaults();
    resetTriggerDelaysToDefaults();
    resetCustomSymbolsToDefaults();
    resetRandomSymbolPoolToDefault();
    standalone_active_start_minutes = DEFAULT_ACTIVE_START_MINUTES;
    standalone_active_end_minutes = DEFAULT_ACTIVE_END_MINUTES;
    resetNetworkExtensionDefaults();

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

    bool migratedLegacyLayout = false;
    uint16_t versionOffset = EEPROM_OFFSET_CONFIG_VERSION;
    uint16_t storedVersion = readStoredConfigVersion(versionOffset);
    bool usingCurrentLayout = (storedVersion == EEPROM_CONFIG_VERSION);
    uint8_t autoModeRaw = 0;
    bool autoModeValueRead = false;

    if (storedVersion == EEPROM_VERSION_INVALID) {
        Serial.println(F("ℹ️ Keine gültige Konfigurationsversion gefunden – gehe von Legacy-Layout aus."));
    } else if (versionOffset == EEPROM_OFFSET_CONFIG_VERSION_LEGACY) {
        Serial.println(F("ℹ️ Legacy-Versionskennung am historischen Offset 0x190 entdeckt."));
    }

    if (usingCurrentLayout || storedVersion == EEPROM_CONFIG_VERSION_WITH_EDITABLE_SYMBOLS || storedVersion == EEPROM_CONFIG_VERSION_WITH_WIFI_MODES) {
        EEPROM.get(EEPROM_OFFSET_DAILY_LETTERS, dailyLetters);
        EEPROM.get(EEPROM_OFFSET_DAILY_LETTER_COLORS, dailyLetterColors);
        sanitizeColorMatrix(dailyLetterColors);
        EEPROM.get(EEPROM_OFFSET_ACTIVE_START_MINUTES, standalone_active_start_minutes);
        EEPROM.get(EEPROM_OFFSET_ACTIVE_END_MINUTES, standalone_active_end_minutes);
        EEPROM.get(EEPROM_OFFSET_COLOR_MODE_MATRIX, dailyLetterColorModes);
        EEPROM.get(EEPROM_OFFSET_COLOR_PALETTE_MASK_MATRIX, dailyLetterRandomPaletteMasks);
        EEPROM.get(EEPROM_OFFSET_WIFI_OPERATION_MODE, wifi_operation_mode);
        uint8_t statusSymbolByte = 1;
        uint8_t staticIpByte = 0;
        EEPROM.get(EEPROM_OFFSET_WIFI_STATUS_SYMBOL_ENABLED, statusSymbolByte);
        EEPROM.get(EEPROM_OFFSET_WIFI_STATIC_IP_ENABLED, staticIpByte);
        wifi_status_symbol_enabled = (statusSymbolByte == 1);
        wifi_static_ip_enabled = (staticIpByte == 1);
        EEPROM.get(EEPROM_OFFSET_WIFI_STATIC_IP, wifi_static_ip);
        wifi_static_ip[sizeof(wifi_static_ip) - 1] = '\0';
        EEPROM.get(EEPROM_OFFSET_WIFI_GATEWAY, wifi_gateway);
        wifi_gateway[sizeof(wifi_gateway) - 1] = '\0';
        EEPROM.get(EEPROM_OFFSET_WIFI_SUBNET, wifi_subnet);
        wifi_subnet[sizeof(wifi_subnet) - 1] = '\0';
        EEPROM.get(EEPROM_OFFSET_WIFI_DNS, wifi_dns);
        wifi_dns[sizeof(wifi_dns) - 1] = '\0';
        EEPROM.get(EEPROM_OFFSET_WIFI_LOCAL_AP_SSID, wifi_local_ap_ssid);
        wifi_local_ap_ssid[sizeof(wifi_local_ap_ssid) - 1] = '\0';
        EEPROM.get(EEPROM_OFFSET_WIFI_LOCAL_AP_PASSWORD, wifi_local_ap_password);
        wifi_local_ap_password[sizeof(wifi_local_ap_password) - 1] = '\0';
        if (usingCurrentLayout || storedVersion == EEPROM_CONFIG_VERSION_WITH_EDITABLE_SYMBOLS) {
            EEPROM.get(EEPROM_OFFSET_CUSTOM_SYMBOL_BITMAPS, customSymbolBitmaps);
            EEPROM.get(EEPROM_OFFSET_CUSTOM_SYMBOL_ENABLED, customSymbolEnabled);
        } else {
            migratedLegacyLayout = true;
        }
        if (usingCurrentLayout) {
            EEPROM.get(EEPROM_OFFSET_RANDOM_SYMBOL_POOL, random_symbol_pool);
            random_symbol_pool[sizeof(random_symbol_pool) - 1] = '\0';
        } else {
            resetRandomSymbolPoolToDefault();
            migratedLegacyLayout = true;
        }
    } else if (storedVersion == EEPROM_CONFIG_VERSION_WITH_COLOR_MODES) {
        Serial.println(F("ℹ️ Konfiguration ohne erweiterte WLAN-Optionen erkannt – setze WLAN-Defaults."));
        loadConfigFromVersion7Layout();
        migratedLegacyLayout = true;
    } else if (storedVersion == EEPROM_CONFIG_VERSION_WITH_ACTIVITY_WINDOW) {
        Serial.println(F("ℹ️ Konfiguration ohne Farbmodi erkannt – feste Farben werden übernommen."));
        loadConfigFromVersion6Layout();
        migratedLegacyLayout = true;
    } else if (storedVersion == EEPROM_CONFIG_VERSION_WITHOUT_ACTIVITY_WINDOW) {
        Serial.println(F("ℹ️ Konfiguration ohne Aktivzeitfenster erkannt – setze ganztägige Aktivität."));
        loadConfigFromVersion5Layout();
        migratedLegacyLayout = true;
    } else if (storedVersion == EEPROM_CONFIG_VERSION_WITH_AUTH) {
        Serial.println(F("ℹ️ Konfiguration der Vorversion erkannt – übernehme Werte unverändert."));
        loadConfigFromVersion4Layout();
        migratedLegacyLayout = true;
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

    if (usingCurrentLayout || storedVersion == EEPROM_CONFIG_VERSION_WITH_EDITABLE_SYMBOLS || storedVersion == EEPROM_CONFIG_VERSION_WITH_WIFI_MODES || storedVersion != EEPROM_CONFIG_VERSION_WITH_AUTH) {
        EEPROM.get(EEPROM_OFFSET_DISPLAY_BRIGHTNESS, display_brightness);
        EEPROM.get(EEPROM_OFFSET_LETTER_DISPLAY_TIME, letter_display_time);
        if (usingCurrentLayout || storedVersion == EEPROM_CONFIG_VERSION_WITH_EDITABLE_SYMBOLS || storedVersion == EEPROM_CONFIG_VERSION_WITH_WIFI_MODES) {
            EEPROM.get(EEPROM_OFFSET_TRIGGER_DELAY_MATRIX, letter_trigger_delays);
        }
        EEPROM.get(EEPROM_OFFSET_AUTO_INTERVAL, letter_auto_display_interval);
        EEPROM.get(EEPROM_OFFSET_AUTO_MODE, autoModeRaw);
        autoModeValueRead = true;
        EEPROM.get(EEPROM_OFFSET_WIFI_CONNECT_TIMEOUT, wifi_connect_timeout);
        autoDisplayMode = (autoModeRaw == 1);
    }

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

    if (wifiPasswordHasFF || wifiPasswordWhitespaceOnly || wifiPasswordHasNonPrintable) {
        Serial.println(F("🛑 Ungültiges WiFi-Passwort im EEPROM gefunden! Setze Standardwert..."));
        strncpy(wifi_password, DEFAULT_WIFI_PASSWORD, sizeof(wifi_password));
        wifi_password[sizeof(wifi_password) - 1] = '\0';
        eepromUpdated = true;
    } else if (wifiPasswordEmpty) {
        Serial.println(F("ℹ️ Leeres WiFi-Passwort erkannt – offene Netzwerke werden unterstützt."));
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
                Serial.println(F("🛑 Ungültiges Zeichen/Symbol entdeckt! Setze Standardwert."));
                dailyLetters[trigger][day] = DEFAULT_DAILY_LETTERS[trigger][day];
                eepromUpdated = true;
            }

            if (!isValidHexColor(dailyLetterColors[trigger][day])) {
                Serial.println(F("🛑 Ungültige Farbe! Setze Standardwert..."));
                strncpy(dailyLetterColors[trigger][day], DEFAULT_DAILY_COLORS[trigger][day], COLOR_STRING_LENGTH);
                dailyLetterColors[trigger][day][COLOR_STRING_LENGTH - 1] = '\0';
                eepromUpdated = true;
            }

            if (!isValidColorModeValue(dailyLetterColorModes[trigger][day])) {
                Serial.println(F("⚠️ Ungültiger Farbmodus entdeckt! Setze feste Farbe."));
                dailyLetterColorModes[trigger][day] = static_cast<uint8_t>(LetterColorMode::Fixed);
                eepromUpdated = true;
            }

            dailyLetterRandomPaletteMasks[trigger][day] &= getFullRandomPaletteMask();
            if (dailyLetterColorModes[trigger][day] == static_cast<uint8_t>(LetterColorMode::RandomSelected) &&
                !hasSelectedRandomPaletteColor(dailyLetterRandomPaletteMasks[trigger][day])) {
                Serial.println(F("⚠️ Zufallspalette ohne Auswahl entdeckt! Setze feste Farbe."));
                dailyLetterColorModes[trigger][day] = static_cast<uint8_t>(LetterColorMode::Fixed);
                dailyLetterRandomPaletteMasks[trigger][day] = getFullRandomPaletteMask();
                eepromUpdated = true;
            }
        }
    }

    for (size_t symbolIndex = 0; symbolIndex < CUSTOM_SYMBOL_COUNT; ++symbolIndex) {
        if (customSymbolEnabled[symbolIndex] != 0 && customSymbolEnabled[symbolIndex] != 1) {
            customSymbolEnabled[symbolIndex] = 0;
            memset(customSymbolBitmaps[symbolIndex], 0, SYMBOL_BITMAP_SIZE);
            eepromUpdated = true;
        }
    }

    if (sanitizeRandomSymbolPool()) {
        Serial.println(F("Zufalls-Zeichenliste bereinigt."));
        eepromUpdated = true;
    }

    if (display_brightness < 1 || display_brightness > 255) {
        Serial.println(F("🛑 Ungültige Helligkeit! Setze Standardwert..."));
        display_brightness = 100;
        eepromUpdated = true;
    }

    if (letter_display_time < 1 || letter_display_time > 60) {
        Serial.println(F("🛑 Ungültige Zeichen-/Symbol-Anzeigezeit! Setze Standardwert..."));
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

    if (!isValidOperationModeValue(wifi_operation_mode)) {
        Serial.println(F("⚠️ Ungültiger WLAN-Betriebsmodus! Setze Timeout-Manager-Modus."));
        wifi_operation_mode = static_cast<uint8_t>(WiFiOperationMode::TimedManager);
        eepromUpdated = true;
    }

    if (wifi_status_symbol_enabled && wifi_operation_mode != static_cast<uint8_t>(WiFiOperationMode::TimedManager)) {
        Serial.println(F("ℹ️ WLAN-Symbol wird in permanenten Netzwerkmodi deaktiviert."));
        wifi_status_symbol_enabled = false;
        eepromUpdated = true;
    }

    if (wifi_static_ip_enabled) {
        if (!isValidIPv4Literal(wifi_static_ip)) {
            Serial.println(F("⚠️ Ungültige statische IP! Setze Standard-IP."));
            copyDefaultString(wifi_static_ip, sizeof(wifi_static_ip), DEFAULT_WIFI_STATIC_IP);
            eepromUpdated = true;
        }
        if (!isValidIPv4Literal(wifi_gateway)) {
            Serial.println(F("⚠️ Ungültiges Gateway! Setze Standard-Gateway."));
            copyDefaultString(wifi_gateway, sizeof(wifi_gateway), DEFAULT_WIFI_GATEWAY);
            eepromUpdated = true;
        }
        if (!isValidIPv4Literal(wifi_subnet)) {
            Serial.println(F("⚠️ Ungültige Subnetzmaske! Setze Standard-Subnetz."));
            copyDefaultString(wifi_subnet, sizeof(wifi_subnet), DEFAULT_WIFI_SUBNET);
            eepromUpdated = true;
        }
        if (!isValidIPv4Literal(wifi_dns)) {
            Serial.println(F("⚠️ Ungültiger DNS-Server! Setze Standard-DNS."));
            copyDefaultString(wifi_dns, sizeof(wifi_dns), DEFAULT_WIFI_DNS);
            eepromUpdated = true;
        }
    }

    const bool localApSsidInvalid =
        strnLength(wifi_local_ap_ssid, sizeof(wifi_local_ap_ssid)) < 2 ||
        isWhitespaceOnly(wifi_local_ap_ssid, sizeof(wifi_local_ap_ssid)) ||
        containsNonPrintable(wifi_local_ap_ssid, sizeof(wifi_local_ap_ssid)) ||
        contains0xFF(wifi_local_ap_ssid, sizeof(wifi_local_ap_ssid));
    if (localApSsidInvalid) {
        Serial.println(F("⚠️ Ungültige lokale AP-SSID! Setze Standardwert."));
        copyDefaultString(wifi_local_ap_ssid, sizeof(wifi_local_ap_ssid), DEFAULT_WIFI_LOCAL_AP_SSID);
        eepromUpdated = true;
    }

    const bool localApPasswordInvalid =
        isWhitespaceOnly(wifi_local_ap_password, sizeof(wifi_local_ap_password)) ||
        containsNonPrintable(wifi_local_ap_password, sizeof(wifi_local_ap_password)) ||
        contains0xFF(wifi_local_ap_password, sizeof(wifi_local_ap_password));
    if (localApPasswordInvalid) {
        Serial.println(F("⚠️ Ungültiges lokales AP-Passwort! Setze Standardwert."));
        copyDefaultString(wifi_local_ap_password, sizeof(wifi_local_ap_password), DEFAULT_WIFI_LOCAL_AP_PASSWORD);
        eepromUpdated = true;
    }

    if (!isValidActiveMinuteValue(standalone_active_start_minutes)) {
        Serial.println(F("⚠️ Ungültige Startzeit für Standalone-Aktivität! Setze 00:00."));
        standalone_active_start_minutes = DEFAULT_ACTIVE_START_MINUTES;
        eepromUpdated = true;
    }

    if (!isValidActiveMinuteValue(standalone_active_end_minutes)) {
        Serial.println(F("⚠️ Ungültige Endzeit für Standalone-Aktivität! Setze 23:59."));
        standalone_active_end_minutes = DEFAULT_ACTIVE_END_MINUTES;
        eepromUpdated = true;
    }

    if (autoModeValueRead && autoModeRaw > 1) {
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
