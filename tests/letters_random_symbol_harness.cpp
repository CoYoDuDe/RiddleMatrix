#include "letters.h"

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
    loadLetterData();

    if (letterData.count('*') != 0) {
        std::cerr << "'*' darf keine feste Bitmap haben, sondern muss Zufallsauswahl bleiben" << std::endl;
        return 1;
    }

    for (char symbol : {'#', '&'}) {
        if (letterData.count(symbol) != 1) {
            std::cerr << "Erwartete Symbolzuordnung fehlt: " << symbol << std::endl;
            return 1;
        }
        if (!hasVisiblePixels(letterData[symbol])) {
            std::cerr << "Symbol hat keine gesetzten Pixel: " << symbol << std::endl;
            return 1;
        }
    }

    return 0;
}
