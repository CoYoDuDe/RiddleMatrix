#include "config.h"

#include <cstdint>
#include <cstring>
#include <iostream>

SerialClass Serial;
ESPClass ESP;
FakeEEPROMClass EEPROM;
Ticker display_ticker;
bool triggerActive = false;
unsigned long letterStartTime = 0;
unsigned long wifiStartTime = 0;
AsyncWebServer server(80);

namespace {

constexpr uint16_t LEGACY_VERSION_OFFSET = 400; // Siehe migrateLegacyLayout()

bool verify_default_color_sanitizing() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.fill(0xFF);

    loadConfig();

    static const char EXPECTED[NUM_TRIGGERS][NUM_DAYS][COLOR_STRING_LENGTH] = {
        {"#FF0000", "#00FF00", "#0000FF", "#FFFF00", "#FF00FF", "#00FFFF", "#FFA500"},
        {"#FFFFFF", "#FFD700", "#ADFF2F", "#00CED1", "#9400D3", "#FF69B4", "#1E90FF"},
        {"#FFA07A", "#20B2AA", "#87CEFA", "#FFE4B5", "#DA70D6", "#90EE90", "#FFDAB9"}
    };

    for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
        for (size_t day = 0; day < NUM_DAYS; ++day) {
            const char *actual = dailyLetterColors[trigger][day];
            const char *expected = EXPECTED[trigger][day];
            if (std::strncmp(actual, expected, COLOR_STRING_LENGTH) != 0) {
                std::cerr << "Mismatch at trigger " << trigger << ", day " << day << " -> "
                          << actual << " expected " << expected << std::endl;
                return false;
            }
        }
    }

    return true;
}

bool verify_legacy_trigger_delay_migration() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.fill(0xFF);

    const unsigned long legacyDelays[NUM_TRIGGERS] = {111UL, 222UL, 333UL};
    for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
        int offset = static_cast<int>(EEPROM_OFFSET_TRIGGER_DELAY_MATRIX +
                                      trigger * EEPROM_SIZEOF_UNSIGNED_LONG);
        EEPROM.put(offset, legacyDelays[trigger]);
    }

    // Hinterlegte Werte nach dem alten Bereich enthalten absichtlich gültige
    // Verzögerungszahlen, um sicherzustellen, dass migrateLegacyLayout diese
    // Fremddaten nicht übernimmt.
    const unsigned long foreignMatrixValue = 42UL;
    EEPROM.put(EEPROM_OFFSET_AUTO_INTERVAL, foreignMatrixValue);
    uint8_t foreignAutoMode = 1;
    EEPROM.put(EEPROM_OFFSET_AUTO_MODE, foreignAutoMode);
    int foreignWifiTimeout = 77;
    EEPROM.put(EEPROM_OFFSET_WIFI_CONNECT_TIMEOUT, foreignWifiTimeout);

    uint16_t legacyVersion = 2;
    EEPROM.put(LEGACY_VERSION_OFFSET, legacyVersion);
    loadConfig();

    for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
        for (size_t day = 0; day < NUM_DAYS; ++day) {
            unsigned long expected = legacyDelays[trigger];
            unsigned long actual = letter_trigger_delays[trigger][day];
            if (actual != expected) {
                std::cerr << "Legacy delay mismatch at trigger " << trigger << ", day " << day
                          << ": got " << actual << " expected " << expected << std::endl;
                return false;
            }
        }
    }

    return true;
}

bool verify_wifi_password_hostname_recovery() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.fill(0xFF);

    char validSsid[sizeof(wifi_ssid)] = {};
    std::strncpy(validSsid, "ValidSSID", sizeof(validSsid));
    EEPROM.put(EEPROM_OFFSET_WIFI_SSID, validSsid);

    uint16_t currentVersion = EEPROM_CONFIG_VERSION;
    EEPROM.put(EEPROM_OFFSET_CONFIG_VERSION, currentVersion);

    loadConfig();

    if (std::strncmp(wifi_ssid, validSsid, sizeof(wifi_ssid)) != 0) {
        std::cerr << "SSID mismatch – expected ValidSSID got " << wifi_ssid << std::endl;
        return false;
    }

    static const char EXPECTED_PASSWORD[] = "YOUR_WIFI_PASSWORD";
    if (std::strncmp(wifi_password, EXPECTED_PASSWORD, sizeof(wifi_password)) != 0) {
        std::cerr << "WiFi password fallback failed – got " << wifi_password << std::endl;
        return false;
    }

    static const char EXPECTED_HOSTNAME[] = "your-device-hostname";
    if (std::strncmp(hostname, EXPECTED_HOSTNAME, sizeof(hostname)) != 0) {
        std::cerr << "Hostname fallback failed – got " << hostname << std::endl;
        return false;
    }

    const auto &buffer = EEPROM.data();
    if (buffer.size() <= EEPROM_OFFSET_WIFI_PASSWORD ||
        buffer[EEPROM_OFFSET_WIFI_PASSWORD] == 0xFF) {
        std::cerr << "EEPROM password bytes were not sanitized" << std::endl;
        return false;
    }

    if (buffer.size() <= EEPROM_OFFSET_HOSTNAME ||
        buffer[EEPROM_OFFSET_HOSTNAME] == 0xFF) {
        std::cerr << "EEPROM hostname bytes were not sanitized" << std::endl;
        return false;
    }

    return true;
}

bool verify_open_network_password_preserved() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.fill(0xFF);

    loadConfig();

    char baselineLetters[NUM_TRIGGERS][NUM_DAYS];
    std::memcpy(baselineLetters, dailyLetters, sizeof(baselineLetters));
    char baselineColors[NUM_TRIGGERS][NUM_DAYS][COLOR_STRING_LENGTH];
    std::memcpy(baselineColors, dailyLetterColors, sizeof(baselineColors));
    unsigned long baselineDelays[NUM_TRIGGERS][NUM_DAYS];
    std::memcpy(baselineDelays, letter_trigger_delays, sizeof(baselineDelays));
    int baselineBrightness = display_brightness;
    unsigned long baselineDisplayTime = letter_display_time;
    unsigned long baselineInterval = letter_auto_display_interval;
    bool baselineAutoMode = autoDisplayMode;
    int baselineTimeout = wifi_connect_timeout;

    EEPROM.begin(EEPROM_SIZE);
    std::uint8_t *raw = EEPROM.raw();
    std::memset(raw, 0, EEPROM_SIZE);

    char openSsid[sizeof(wifi_ssid)] = {};
    std::strncpy(openSsid, "OpenNetwork", sizeof(openSsid));
    std::memcpy(raw + EEPROM_OFFSET_WIFI_SSID, openSsid, sizeof(openSsid));

    char openHostname[sizeof(hostname)] = {};
    std::strncpy(openHostname, "open-host", sizeof(openHostname));
    std::memcpy(raw + EEPROM_OFFSET_HOSTNAME, openHostname, sizeof(openHostname));

    std::memcpy(raw + EEPROM_OFFSET_DAILY_LETTERS, baselineLetters, sizeof(baselineLetters));
    std::memcpy(raw + EEPROM_OFFSET_DAILY_LETTER_COLORS, baselineColors, sizeof(baselineColors));

    std::memcpy(raw + EEPROM_OFFSET_DISPLAY_BRIGHTNESS,
                &baselineBrightness,
                sizeof(baselineBrightness));

    std::uint32_t displayTime = static_cast<std::uint32_t>(baselineDisplayTime);
    std::memcpy(raw + EEPROM_OFFSET_LETTER_DISPLAY_TIME, &displayTime, sizeof(displayTime));

    for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
        for (size_t day = 0; day < NUM_DAYS; ++day) {
            std::uint32_t value = static_cast<std::uint32_t>(baselineDelays[trigger][day]);
            size_t index = trigger * NUM_DAYS + day;
            size_t offset = EEPROM_OFFSET_TRIGGER_DELAY_MATRIX + index * sizeof(std::uint32_t);
            std::memcpy(raw + offset, &value, sizeof(value));
        }
    }

    std::uint32_t interval = static_cast<std::uint32_t>(baselineInterval);
    std::memcpy(raw + EEPROM_OFFSET_AUTO_INTERVAL, &interval, sizeof(interval));

    std::uint8_t autoModeByte = baselineAutoMode ? 1U : 0U;
    std::memcpy(raw + EEPROM_OFFSET_AUTO_MODE, &autoModeByte, sizeof(autoModeByte));

    std::memcpy(raw + EEPROM_OFFSET_WIFI_CONNECT_TIMEOUT,
                &baselineTimeout,
                sizeof(baselineTimeout));

    std::uint16_t version = EEPROM_CONFIG_VERSION;
    std::memcpy(raw + EEPROM_OFFSET_CONFIG_VERSION, &version, sizeof(version));

    loadConfig();

    if (wifi_password[0] != '\0') {
        std::cerr << "Offenes Netz wurde nicht beibehalten – Passwort ist: " << wifi_password
                  << std::endl;
        return false;
    }

    const auto &buffer = EEPROM.data();
    if (buffer.size() > EEPROM_OFFSET_WIFI_PASSWORD &&
        buffer[EEPROM_OFFSET_WIFI_PASSWORD] != 0x00) {
        std::cerr << "EEPROM-Passwort wurde unerwartet überschrieben" << std::endl;
        return false;
    }

    return true;
}

} // namespace

int main() {
    if (!verify_default_color_sanitizing()) {
        return 1;
    }

    if (!verify_legacy_trigger_delay_migration()) {
        return 1;
    }

    if (!verify_wifi_password_hostname_recovery()) {
        return 1;
    }

    if (!verify_open_network_password_preserved()) {
        return 1;
    }

    return 0;
}
