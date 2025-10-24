#include "config.h"

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

int main() {
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
                return 1;
            }
        }
    }

    return 0;
}
