#ifndef ESP8266WIFI_H
#define ESP8266WIFI_H

#include <cstdlib>
#include <cstring>

class IPAddress {
public:
    bool fromString(const char *value) {
        if (value == nullptr || *value == '\0') {
            return false;
        }

        const char *cursor = value;
        for (int part = 0; part < 4; ++part) {
            if (*cursor == '\0') {
                return false;
            }

            char *end = nullptr;
            long octet = std::strtol(cursor, &end, 10);
            if (end == cursor || octet < 0 || octet > 255) {
                return false;
            }
            if (part < 3) {
                if (*end != '.') {
                    return false;
                }
                cursor = end + 1;
            } else if (*end != '\0') {
                return false;
            }
        }
        return true;
    }
};

#endif
