#ifndef EEPROM_H
#define EEPROM_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
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
        size_t storedSize = sizeForType<T>();
        ensureCapacity(address, storedSize);
        std::memset(&value, 0, sizeof(T));
        std::memcpy(&value, buffer.data() + address, storedSize);
    }

    template <typename T>
    void put(int address, const T &value) {
        size_t storedSize = sizeForType<T>();
        ensureCapacity(address, storedSize);
        std::memcpy(buffer.data() + address, &value, storedSize);
    }

    void commit() {}

    void fill(uint8_t value) {
        std::fill(buffer.begin(), buffer.end(), value);
    }

    uint8_t *raw() { return buffer.data(); }
    const std::vector<uint8_t> &data() const { return buffer; }

  private:
    template <typename T>
    static constexpr size_t sizeForType() {
#if defined(RIDDLEMATRIX_HOST_TEST)
        if constexpr (std::is_same_v<T, unsigned long>) {
            return 4;
        }
#endif
        return sizeof(T);
    }

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
