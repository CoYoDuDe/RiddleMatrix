#ifndef EEPROM_H
#define EEPROM_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

class FakeEEPROMClass {
  public:
    void begin(size_t size) {
        if (buffer.size() != size) {
            buffer.assign(size, 0xFF);
        }
    }

    template <typename T>
    void get(int address, T &value) const {
        ensureCapacity(address, sizeof(T));
        std::memcpy(&value, buffer.data() + address, sizeof(T));
    }

    template <typename T>
    void put(int address, const T &value) {
        ensureCapacity(address, sizeof(T));
        std::memcpy(buffer.data() + address, &value, sizeof(T));
    }

    void commit() {}

    void fill(uint8_t value) {
        std::fill(buffer.begin(), buffer.end(), value);
    }

    uint8_t *raw() { return buffer.data(); }
    const std::vector<uint8_t> &data() const { return buffer; }

  private:
    void ensureCapacity(int address, size_t length) const {
        size_t required = static_cast<size_t>(address) + length;
        if (required > buffer.size()) {
            const_cast<std::vector<uint8_t> &>(buffer).resize(required, 0xFF);
        }
    }

    mutable std::vector<uint8_t> buffer;
};

extern FakeEEPROMClass EEPROM;

#endif
