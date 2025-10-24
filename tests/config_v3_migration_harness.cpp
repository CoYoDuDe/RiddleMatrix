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
constexpr uint16_t OFFSET_CONFIG_VERSION_V3 = 469;
constexpr uint16_t OFFSET_DAILY_LETTER_COLORS_V3 = 200;
constexpr uint16_t OFFSET_DISPLAY_BRIGHTNESS_V3 =
    OFFSET_DAILY_LETTER_COLORS_V3 + (NUM_TRIGGERS * NUM_DAYS * COLOR_STRING_LENGTH);
constexpr uint16_t OFFSET_LETTER_DISPLAY_TIME_V3 = OFFSET_DISPLAY_BRIGHTNESS_V3 + sizeof(int32_t);
constexpr uint16_t OFFSET_TRIGGER_DELAY_MATRIX_V3 = OFFSET_LETTER_DISPLAY_TIME_V3 + sizeof(uint32_t);
constexpr uint16_t OFFSET_AUTO_INTERVAL_V3 =
    OFFSET_TRIGGER_DELAY_MATRIX_V3 + (NUM_TRIGGERS * NUM_DAYS * sizeof(uint32_t));
constexpr uint16_t OFFSET_AUTO_MODE_V3 = OFFSET_AUTO_INTERVAL_V3 + sizeof(uint32_t);
constexpr uint16_t OFFSET_WIFI_CONNECT_TIMEOUT_V3 = OFFSET_AUTO_MODE_V3 + sizeof(uint8_t);

constexpr char TEST_LETTERS[NUM_TRIGGERS][NUM_DAYS] = {
    {'A', 'B', 'C', 'D', 'E', 'F', 'G'},
    {'H', 'I', 'J', 'K', 'L', 'M', 'N'},
    {'O', 'P', 'Q', 'R', 'S', 'T', 'U'},
};

constexpr char TEST_COLORS[NUM_TRIGGERS][NUM_DAYS][COLOR_STRING_LENGTH] = {
    {"#000001", "#000002", "#000003", "#000004", "#000005", "#000006", "#000007"},
    {"#000101", "#000102", "#000103", "#000104", "#000105", "#000106", "#000107"},
    {"#000201", "#000202", "#000203", "#000204", "#000205", "#000206", "#000207"},
};

constexpr uint32_t TEST_TRIGGER_DELAYS[NUM_TRIGGERS][NUM_DAYS] = {
    {1, 1, 1, 1, 1, 1, 1},
    {2, 2, 2, 2, 2, 2, 2},
    {3, 3, 3, 3, 3, 3, 3},
};

void write_u16(uint16_t offset, uint16_t value) {
    std::memcpy(EEPROM.raw() + offset, &value, sizeof(value));
}

void write_u32(uint16_t offset, uint32_t value) {
    std::memcpy(EEPROM.raw() + offset, &value, sizeof(value));
}

void write_i32(uint16_t offset, int32_t value) {
    std::memcpy(EEPROM.raw() + offset, &value, sizeof(value));
}
}

int main() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.fill(0xFF);

    uint8_t *raw = EEPROM.raw();

    std::memcpy(raw + EEPROM_OFFSET_DAILY_LETTERS, TEST_LETTERS, sizeof(TEST_LETTERS));
    std::memcpy(raw + OFFSET_DAILY_LETTER_COLORS_V3, TEST_COLORS, sizeof(TEST_COLORS));

    write_i32(OFFSET_DISPLAY_BRIGHTNESS_V3, 123);
    write_u32(OFFSET_LETTER_DISPLAY_TIME_V3, 17);

    for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
        for (size_t day = 0; day < NUM_DAYS; ++day) {
            size_t index = (trigger * NUM_DAYS) + day;
            write_u32(static_cast<uint16_t>(OFFSET_TRIGGER_DELAY_MATRIX_V3 + index * sizeof(uint32_t)),
                      TEST_TRIGGER_DELAYS[trigger][day]);
        }
    }

    write_u32(OFFSET_AUTO_INTERVAL_V3, 555);
    raw[OFFSET_AUTO_MODE_V3] = 1;
    write_i32(OFFSET_WIFI_CONNECT_TIMEOUT_V3, 42);
    write_u16(OFFSET_CONFIG_VERSION_V3, 3);

    loadConfig();

    bool success = true;

    for (size_t trigger = 0; trigger < NUM_TRIGGERS; ++trigger) {
        for (size_t day = 0; day < NUM_DAYS; ++day) {
            if (std::strncmp(dailyLetterColors[trigger][day], TEST_COLORS[trigger][day], COLOR_STRING_LENGTH) != 0) {
                std::cerr << "Mismatch in colors at trigger " << trigger << ", day " << day << " -> actual "
                          << dailyLetterColors[trigger][day] << " expected " << TEST_COLORS[trigger][day]
                          << std::endl;
                success = false;
            }
        }
    }

    if (!autoDisplayMode) {
        std::cerr << "Auto mode not migrated correctly" << std::endl;
        success = false;
    }

    uint16_t versionAfter = 0;
    EEPROM.get(EEPROM_OFFSET_CONFIG_VERSION, versionAfter);
    if (versionAfter != EEPROM_CONFIG_VERSION) {
        std::cerr << "Unexpected config version after migration: " << versionAfter << std::endl;
        success = false;
    }

    return success ? 0 : 1;
}
