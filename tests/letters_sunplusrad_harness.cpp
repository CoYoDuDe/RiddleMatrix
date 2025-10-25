#include "letters.h"

#include <cstddef>
#include <cstdint>
#include <iostream>

SerialClass Serial;

int main() {
    loadLetterData();

    if (letterData.count('*') != 1) {
        std::cerr << "Erwartete Glyphenzuordnung für '*' fehlt oder ist mehrfach vorhanden" << std::endl;
        return 1;
    }

    const uint8_t *bitmap = letterData['*'];
    bool hasPixel = false;
    for (std::size_t index = 0; index < 128; ++index) {
        if (bitmap[index] != 0) {
            hasPixel = true;
            break;
        }
    }

    if (!hasPixel) {
        std::cerr << "Sun+Rad-Glyph enthält keine gesetzten Pixel" << std::endl;
        return 1;
    }

    return 0;
}
