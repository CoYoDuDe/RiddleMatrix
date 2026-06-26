#include "config.h"
#include "letters.h"

#include <Arduino.h>
#include <cstring>

#if defined(ESP32)
#include <SPIFFS.h>
#define RIDDLEMATRIX_SYMBOL_FS SPIFFS
#elif defined(ESP8266)
#include <LittleFS.h>
#define RIDDLEMATRIX_SYMBOL_FS LittleFS
#endif

uint8_t editableBuiltinSymbolBitmaps[EDITABLE_BUILTIN_SYMBOL_COUNT][SYMBOL_BITMAP_SIZE] = {};
uint8_t editableBuiltinSymbolEnabled[EDITABLE_BUILTIN_SYMBOL_COUNT] = {};

namespace {

bool symbolFsReady = false;

String symbolFilePath(char symbol) {
    char buffer[16] = {};
    snprintf(buffer, sizeof(buffer), "/sym_%02X.bin", static_cast<unsigned char>(symbol));
    return String(buffer);
}

bool beginSymbolFs() {
#if defined(ESP32)
    if (RIDDLEMATRIX_SYMBOL_FS.begin(true)) {
        return true;
    }
#elif defined(ESP8266)
    if (RIDDLEMATRIX_SYMBOL_FS.begin()) {
        return true;
    }
    RIDDLEMATRIX_SYMBOL_FS.format();
    if (RIDDLEMATRIX_SYMBOL_FS.begin()) {
        return true;
    }
#else
    return false;
#endif
    return false;
}

void resetEditableBuiltinSymbols() {
    memset(editableBuiltinSymbolBitmaps, 0, sizeof(editableBuiltinSymbolBitmaps));
    memset(editableBuiltinSymbolEnabled, 0, sizeof(editableBuiltinSymbolEnabled));
}

} // namespace

int editableBuiltinSymbolIndexFromChar(char symbol) {
    for (size_t index = 0; index < EDITABLE_BUILTIN_SYMBOL_COUNT; ++index) {
        if (editableBuiltinSymbols[index] == symbol) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

bool isEditableBuiltinSymbol(char symbol) {
    return editableBuiltinSymbolIndexFromChar(symbol) >= 0;
}

bool initEditableSymbolStore() {
    resetEditableBuiltinSymbols();
    symbolFsReady = beginSymbolFs();
    if (!symbolFsReady) {
        Serial.println(F("Symbol-Dateisystem konnte nicht gestartet werden. Eingebaute Defaults bleiben aktiv."));
        return false;
    }

#if defined(ESP32) || defined(ESP8266)
    for (size_t index = 0; index < EDITABLE_BUILTIN_SYMBOL_COUNT; ++index) {
        const String path = symbolFilePath(editableBuiltinSymbols[index]);
        if (!RIDDLEMATRIX_SYMBOL_FS.exists(path)) {
            continue;
        }
        File file = RIDDLEMATRIX_SYMBOL_FS.open(path, "r");
        if (!file) {
            continue;
        }
        if (file.size() != static_cast<size_t>(SYMBOL_BITMAP_SIZE + 1)) {
            file.close();
            RIDDLEMATRIX_SYMBOL_FS.remove(path);
            continue;
        }
        editableBuiltinSymbolEnabled[index] = file.read() == 1 ? 1 : 0;
        const size_t bytesRead = file.read(editableBuiltinSymbolBitmaps[index], SYMBOL_BITMAP_SIZE);
        file.close();
        if (bytesRead != SYMBOL_BITMAP_SIZE) {
            editableBuiltinSymbolEnabled[index] = 0;
            memset(editableBuiltinSymbolBitmaps[index], 0, SYMBOL_BITMAP_SIZE);
            RIDDLEMATRIX_SYMBOL_FS.remove(path);
        }
    }
#endif

    return true;
}

bool getDefaultBuiltinSymbolBitmap(char symbol, uint8_t *target) {
    if (target == nullptr || letterData.find(symbol) == letterData.end()) {
        return false;
    }
    const uint8_t *source = letterData[symbol];
    for (size_t index = 0; index < SYMBOL_BITMAP_SIZE; ++index) {
        target[index] = pgm_read_byte(&source[index]);
    }
    return true;
}

bool getEditableBuiltinSymbolBitmap(char symbol, uint8_t *target) {
    if (target == nullptr) {
        return false;
    }
    const int index = editableBuiltinSymbolIndexFromChar(symbol);
    if (index < 0 || editableBuiltinSymbolEnabled[index] != 1) {
        return false;
    }
    memcpy(target, editableBuiltinSymbolBitmaps[index], SYMBOL_BITMAP_SIZE);
    return true;
}

bool saveEditableBuiltinSymbol(char symbol, const uint8_t *bitmap, bool enabled) {
    if (bitmap == nullptr) {
        return false;
    }
    const int index = editableBuiltinSymbolIndexFromChar(symbol);
    if (index < 0) {
        return false;
    }
    if (!symbolFsReady && !initEditableSymbolStore()) {
        return false;
    }

    memcpy(editableBuiltinSymbolBitmaps[index], bitmap, SYMBOL_BITMAP_SIZE);
    editableBuiltinSymbolEnabled[index] = enabled ? 1 : 0;

#if defined(ESP32) || defined(ESP8266)
    File file = RIDDLEMATRIX_SYMBOL_FS.open(symbolFilePath(symbol), "w");
    if (!file) {
        return false;
    }
    file.write(static_cast<uint8_t>(enabled ? 1 : 0));
    const size_t bytesWritten = file.write(bitmap, SYMBOL_BITMAP_SIZE);
    file.close();
    return bytesWritten == SYMBOL_BITMAP_SIZE;
#else
    return false;
#endif
}

bool clearEditableBuiltinSymbol(char symbol) {
    const int index = editableBuiltinSymbolIndexFromChar(symbol);
    if (index < 0) {
        return false;
    }
    editableBuiltinSymbolEnabled[index] = 0;
    memset(editableBuiltinSymbolBitmaps[index], 0, SYMBOL_BITMAP_SIZE);
    if (!symbolFsReady && !initEditableSymbolStore()) {
        return true;
    }
#if defined(ESP32) || defined(ESP8266)
    const String path = symbolFilePath(symbol);
    if (RIDDLEMATRIX_SYMBOL_FS.exists(path)) {
        return RIDDLEMATRIX_SYMBOL_FS.remove(path);
    }
#endif
    return true;
}
