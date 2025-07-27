#include "letters.h"

std::map<char, const uint8_t*> letterData;

void loadLetterData() {
    Serial.println(F("📦 Lade Buchstaben-Daten..."));
    letterData['A'] = letter_A;
    letterData['B'] = letter_B;
    letterData['C'] = letter_C;
    letterData['D'] = letter_D;
    letterData['E'] = letter_E;
    letterData['F'] = letter_F;
    letterData['G'] = letter_G;
    letterData['H'] = letter_H;
    letterData['I'] = letter_I;
    letterData['J'] = letter_J;
    letterData['K'] = letter_K;
    letterData['L'] = letter_L;
    letterData['M'] = letter_M;
    letterData['N'] = letter_N;
    letterData['O'] = letter_O;
    letterData['P'] = letter_P;
    letterData['Q'] = letter_Q;
    letterData['R'] = letter_R;
    letterData['S'] = letter_S;
    letterData['T'] = letter_T;
    letterData['U'] = letter_U;
    letterData['V'] = letter_V;
    letterData['W'] = letter_W;
    letterData['X'] = letter_X;
    letterData['Y'] = letter_Y;
    letterData['Z'] = letter_Z;
    letterData['#'] = letter_SUN;
    letterData['~'] = letter_WIFI;
    letterData['&'] = letter_RIESENRAD;
    letterData['?'] = letter_RIDDLER;
}

