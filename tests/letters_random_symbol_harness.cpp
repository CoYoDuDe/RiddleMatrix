#include "symbol_defaults.h"

#include <cstddef>
#include <cstdint>
#include <iostream>

SerialClass Serial;

bool hasVisiblePixels(const uint8_t *bitmap) {
    for (std::size_t index = 0; index < 128; ++index) {
        if (bitmap[index] != 0) {
            return true;
        }
    }
    return false;
}

int main() {
    if (factorySymbolExists('*')) {
        std::cerr << "'*' darf keine feste Bitmap haben, sondern muss Zufallsauswahl bleiben" << std::endl;
        return 1;
    }

    for (char symbol : {'#', '&'}) {
        const uint8_t *bitmap = getFactorySymbolBitmap(symbol);
        if (bitmap == nullptr) {
            std::cerr << "Erwartete Symbolzuordnung fehlt: " << symbol << std::endl;
            return 1;
        }
        if (!hasVisiblePixels(bitmap)) {
            std::cerr << "Symbol hat keine gesetzten Pixel: " << symbol << std::endl;
            return 1;
        }
    }

    return 0;
}
