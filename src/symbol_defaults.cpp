#include "symbol_defaults.h"

const uint8_t *getFactorySymbolBitmap(char symbol) {
    switch (symbol) {
        case 'A': return letter_A;
        case 'B': return letter_B;
        case 'C': return letter_C;
        case 'D': return letter_D;
        case 'E': return letter_E;
        case 'F': return letter_F;
        case 'G': return letter_G;
        case 'H': return letter_H;
        case 'I': return letter_I;
        case 'J': return letter_J;
        case 'K': return letter_K;
        case 'L': return letter_L;
        case 'M': return letter_M;
        case 'N': return letter_N;
        case 'O': return letter_O;
        case 'P': return letter_P;
        case 'Q': return letter_Q;
        case 'R': return letter_R;
        case 'S': return letter_S;
        case 'T': return letter_T;
        case 'U': return letter_U;
        case 'V': return letter_V;
        case 'W': return letter_W;
        case 'X': return letter_X;
        case 'Y': return letter_Y;
        case 'Z': return letter_Z;
        case '#': return letter_SUN;
        case '~': return letter_WIFI;
        case '&': return letter_RIESENRAD;
        case '?': return letter_RIDDLER;
        default: return nullptr;
    }
}

bool factorySymbolExists(char symbol) {
    return getFactorySymbolBitmap(symbol) != nullptr;
}
