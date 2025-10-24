#ifndef ARDUINO_H
#define ARDUINO_H

#include <cstdint>
#include <string>
#include <cstring>

using String = std::string;

#define PROGMEM
#define F(x) x

#define D0 0
#define D1 1
#define D2 2
#define D3 3
#define D4 4
#define D5 5
#define D6 6
#define D7 7
#define D8 8

struct SerialClass {
    template <typename T>
    SerialClass &print(const T &) {
        return *this;
    }

    template <typename T>
    SerialClass &println(const T &) {
        return *this;
    }

    SerialClass &println() {
        return *this;
    }
};

extern SerialClass Serial;

struct ESPClass {
    int getFreeHeap() const { return 1024; }
};

extern ESPClass ESP;

inline void delay(unsigned long) {}

#endif
