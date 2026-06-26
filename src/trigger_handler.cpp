#include "trigger_handler.h"
#include "rtc_manager.h"
#include "wifi_manager.h"

DisplayLetterError lastDisplayLetterError = DisplayLetterError::None;
bool pendingTriggerActive = false;
bool activeDisplayManagedBySchedule = false;

namespace {

constexpr size_t MAX_PENDING_TRIGGERS = NUM_TRIGGERS * 3;
constexpr unsigned long WEEKDAY_CACHE_RETRY_DELAY_MS = 5UL;

PendingTrigger pendingQueue[MAX_PENDING_TRIGGERS];
size_t pendingTriggerCount = 0;

bool customSymbolIsAvailable(char letter) {
    if (letter < '0' || letter > '7') {
        return false;
    }
    const int index = letter - '0';
    return index >= 0 &&
        index < static_cast<int>(CUSTOM_SYMBOL_COUNT) &&
        customSymbolEnabled[index] == 1;
}

bool displaySymbolIsAvailable(char letter) {
    return customSymbolIsAvailable(letter) || letterData.find(letter) != letterData.end();
}

char resolveRandomSymbolSelection() {
    char candidates[RANDOM_SYMBOL_POOL_LENGTH] = {};
    size_t candidateCount = 0;

    for (size_t index = 0; index < RANDOM_SYMBOL_POOL_LENGTH - 1; ++index) {
        const char candidate = random_symbol_pool[index];
        if (candidate == '\0') {
            break;
        }
        if (candidate != '*' && displaySymbolIsAvailable(candidate) && candidateCount < RANDOM_SYMBOL_POOL_LENGTH - 1) {
            candidates[candidateCount++] = candidate;
        }
    }

    if (candidateCount == 0) {
        if (displaySymbolIsAvailable('#')) {
            candidates[candidateCount++] = '#';
        }
        if (displaySymbolIsAvailable('&')) {
            candidates[candidateCount++] = '&';
        }
    }

    if (candidateCount == 0) {
        return '\0';
    }
    return candidates[random(static_cast<long>(candidateCount))];
}

void prioritizePendingTriggersWithExecuteAt(unsigned long executeAt) {
    if (pendingTriggerCount < 2) {
        return;
    }

    size_t insertPosition = 0;
    for (size_t i = 0; i < pendingTriggerCount; ++i) {
        if (pendingQueue[i].executeAt == executeAt) {
            PendingTrigger matching = pendingQueue[i];

            for (size_t j = i; j > insertPosition; --j) {
                pendingQueue[j] = pendingQueue[j - 1];
            }

            pendingQueue[insertPosition] = matching;
            ++insertPosition;
        }
    }
}

void shiftPendingQueue(size_t fromIndex) {
    if (fromIndex >= pendingTriggerCount) {
        return;
    }

    for (size_t i = fromIndex + 1; i < pendingTriggerCount; ++i) {
        pendingQueue[i - 1] = pendingQueue[i];
    }
}

void ensureWiFiSymbolAfterError() {
    if (wifiConnected && !wifiDisabled) {
        drawWiFiSymbol();
    }
}

bool ensureWeekdayCacheForTriggers() {
    if (isWeekdayCacheValid()) {
        return true;
    }

    Serial.println(F("⌛ Warte auf gültigen Wochentag aus dem RTC-Cache..."));
    updateCachedWeekday(true);

    if (!isWeekdayCacheValid()) {
        delay(WEEKDAY_CACHE_RETRY_DELAY_MS);
        updateCachedWeekday(true);
    }

    if (!isWeekdayCacheValid()) {
        Serial.println(F("⚠️ Wochentag-Cache konnte nicht aktualisiert werden."));
        return false;
    }

    return true;
}

int resolveWeekdayForTriggerHandling() {
    if (!ensureWeekdayCacheForTriggers()) {
        return -1;
    }

    return getCachedWeekday();
}

bool isWithinStandaloneWindow(uint16_t minutesOfDay) {
    if (standalone_active_start_minutes == standalone_active_end_minutes) {
        return true;
    }

    if (standalone_active_start_minutes < standalone_active_end_minutes) {
        return minutesOfDay >= standalone_active_start_minutes &&
               minutesOfDay <= standalone_active_end_minutes;
    }

    return minutesOfDay >= standalone_active_start_minutes ||
           minutesOfDay <= standalone_active_end_minutes;
}

uint16_t color565FromHex(const String &hexColor) {
    uint32_t colorHex = strtol(hexColor.c_str() + 1, NULL, 16);
    uint8_t r = (colorHex >> 16) & 0xFF;
    uint8_t g = (colorHex >> 8) & 0xFF;
    uint8_t b = colorHex & 0xFF;
    return display.color565(r, g, b);
}

String buildRainbowRandomColor() {
    const uint16_t hue = static_cast<uint16_t>(random(1536));
    const uint8_t segment = static_cast<uint8_t>(hue / 256U);
    const uint8_t offset = static_cast<uint8_t>(hue % 256U);

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    switch (segment) {
        case 0:
            r = 255;
            g = offset;
            break;
        case 1:
            r = static_cast<uint8_t>(255 - offset);
            g = 255;
            break;
        case 2:
            g = 255;
            b = offset;
            break;
        case 3:
            g = static_cast<uint8_t>(255 - offset);
            b = 255;
            break;
        case 4:
            r = offset;
            b = 255;
            break;
        default:
            r = 255;
            b = static_cast<uint8_t>(255 - offset);
            break;
    }

    char buffer[8];
    snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", r, g, b);
    return String(buffer);
}

String resolveDisplayColor(uint8_t triggerIndex, size_t dayIndex) {
    String fixedColor(dailyLetterColors[triggerIndex][dayIndex]);
    if (fixedColor.length() != 7 || fixedColor[0] != '#') {
        fixedColor = "#FFFFFF";
    }

    const uint8_t colorModeValue = dailyLetterColorModes[triggerIndex][dayIndex];
    const LetterColorMode colorMode = static_cast<LetterColorMode>(colorModeValue);

    if (colorMode == LetterColorMode::Fixed) {
        return fixedColor;
    }

    if (colorMode == LetterColorMode::RandomAll) {
        return buildRainbowRandomColor();
    }

    uint16_t paletteMask = dailyLetterRandomPaletteMasks[triggerIndex][dayIndex];
    size_t selectedCount = 0;
    for (size_t index = 0; index < RANDOM_COLOR_PALETTE_SIZE; ++index) {
        if ((paletteMask & static_cast<uint16_t>(1U << index)) != 0U) {
            ++selectedCount;
        }
    }

    if (selectedCount == 0) {
        return fixedColor;
    }

    size_t selectedOffset = static_cast<size_t>(random(static_cast<long>(selectedCount)));
    for (size_t index = 0; index < RANDOM_COLOR_PALETTE_SIZE; ++index) {
        if ((paletteMask & static_cast<uint16_t>(1U << index)) == 0U) {
            continue;
        }

        if (selectedOffset == 0) {
            return String(randomColorPalette[index]);
        }
        --selectedOffset;
    }

    return fixedColor;
}

} // namespace

void clearDisplay() {
    if (alreadyCleared) {
        Serial.println(F("⚠️ `clearDisplay()` wurde bereits ausgeführt, Abbruch."));
        return;
    }

    Serial.println(F("🧹 Zeichen/Symbol wird jetzt gelöscht!"));

    display.fillScreen(display.color565(0, 0, 0));
    display.display();

    alreadyCleared = true;
    triggerActive = false;
    wifiSymbolVisible = false;
    activeDisplayManagedBySchedule = false;

    if (wifiConnected && !wifiDisabled) {
        drawWiFiSymbol();
    }
}

bool isTriggerPending(uint8_t triggerIndex) {
    if (triggerIndex >= NUM_TRIGGERS) {
        return false;
    }

    for (size_t i = 0; i < pendingTriggerCount; ++i) {
        if (pendingQueue[i].triggerIndex == triggerIndex) {
            return true;
        }
    }

    return false;
}

bool enqueuePendingTrigger(uint8_t triggerIndex, bool fromWeb) {
    if (triggerIndex >= NUM_TRIGGERS) {
        Serial.println(F("⚠️ Ungültiger Trigger-Index beim Planen – Vorgang abgebrochen."));
        return false;
    }

    if (pendingTriggerCount >= MAX_PENDING_TRIGGERS) {
        Serial.println(F("⚠️ Zu viele geplante Trigger – bitte warten, bis ein Trigger abgearbeitet wurde."));
        return false;
    }

    if (isTriggerPending(triggerIndex)) {
        Serial.println(F("⚠️ Trigger wurde bereits geplant – doppelter Eintrag wird ignoriert."));
        return false;
    }

    int today = resolveWeekdayForTriggerHandling();
    if (today < 0 || today >= static_cast<int>(NUM_DAYS)) {
        Serial.println(F("⚠️ Ungültiger Wochentag aus Cache – Trigger kann nicht geplant werden."));
        return false;
    }

    unsigned long delaySeconds = letter_trigger_delays[triggerIndex][static_cast<size_t>(today)];
    unsigned long executeAt = millis() + (delaySeconds * 1000UL);

    pendingQueue[pendingTriggerCount++] = {triggerIndex, executeAt, fromWeb};
    pendingTriggerActive = true;

    if (triggerActive) {
        Serial.println(F("ℹ️ Anzeige läuft noch – Trigger wurde zur späteren Ausführung eingeplant."));
    }

    Serial.print(F("📥 Geplanter Trigger "));
    Serial.print(triggerIndex + 1);
    Serial.print(F(" aus Quelle "));
    Serial.println(fromWeb ? F("Web") : F("Seriell"));

    if (delaySeconds == 0) {
        Serial.println(F("⚡ Keine Verzögerung konfiguriert – Ausführung erfolgt beim nächsten Durchlauf."));
    } else {
        Serial.print(F("⏱️ Ausführung in "));
        Serial.print(delaySeconds);
        Serial.println(F(" Sekunden geplant."));
    }

    return true;
}

void processPendingTriggers() {
    if (pendingTriggerCount == 0) {
        pendingTriggerActive = false;
        return;
    }

    if (triggerActive) {
        return;
    }

    unsigned long now = millis();
    size_t index = 0;

    while (index < pendingTriggerCount) {
        const PendingTrigger current = pendingQueue[index];

        if (static_cast<long>(now - current.executeAt) >= 0) {
            shiftPendingQueue(index);
            --pendingTriggerCount;
            pendingTriggerActive = pendingTriggerCount > 0;

            Serial.print(F("🚀 Ausführung des geplanten Triggers "));
            Serial.print(current.triggerIndex + 1);
            Serial.print(F(" (Quelle: "));
            Serial.print(current.fromWeb ? F("Web") : F("Seriell"));
            Serial.println(F(")"));

            handleTrigger(static_cast<char>('1' + current.triggerIndex), false, current.fromWeb);

            unsigned long afterExecution = millis();

            bool hasSameExecuteAtPending = false;
            for (size_t i = 0; i < pendingTriggerCount; ++i) {
                const PendingTrigger& candidate = pendingQueue[i];
                if (candidate.executeAt == current.executeAt &&
                    static_cast<long>(afterExecution - candidate.executeAt) >= 0) {
                    hasSameExecuteAtPending = true;
                    break;
                }
            }

            if (hasSameExecuteAtPending) {
                Serial.println(F("🔁 Weitere Trigger mit identischem Ausführungszeitpunkt warten auf Abarbeitung."));
                prioritizePendingTriggersWithExecuteAt(current.executeAt);
            }

            if (triggerActive) {
                break;
            }

            now = afterExecution;
            continue;
        }

        ++index;
    }

    if (pendingTriggerCount == 0) {
        pendingTriggerActive = false;
    }
}

bool displayLetter(uint8_t triggerIndex, char letter) {
    lastDisplayLetterError = DisplayLetterError::None;

    if (triggerIndex >= NUM_TRIGGERS) {
        Serial.print(F("⚠️ Ungültiger Trigger-Index "));
        Serial.print(triggerIndex);
        Serial.println(F(" – fallback auf Trigger 1."));
        triggerIndex = 0;
    }

    Serial.print(F("🎨 Zeichne Zeichen/Symbol für Trigger "));
    Serial.print(triggerIndex + 1);
    Serial.print(F(": "));
    Serial.println(letter);

    if (triggerActive) {
        Serial.println(F("⚠️ Ein Zeichen/Symbol ist bereits aktiv. Abbruch."));
        lastDisplayLetterError = DisplayLetterError::TriggerAlreadyActive;
        return false;
    }

    triggerActive = true;

    if (letter == '*') {
        letter = resolveRandomSymbolSelection();
        if (letter == '\0') {
            Serial.println(F("Keine gueltige Zufalls-Zeichenliste vorhanden."));
            triggerActive = false;
            lastDisplayLetterError = DisplayLetterError::LetterNotFound;
            ensureWiFiSymbolAfterError();
            return false;
        }
        Serial.print(F("Zufallsauswahl `*` wurde ersetzt durch: "));
        Serial.println(letter);
    }

    int today = getRTCWeekday();
    Serial.print(F("📅 Heutiger Wochentag: "));
    Serial.println(today);

    if (today < 0 || today >= static_cast<int>(NUM_DAYS)) {
        Serial.println(F("⚠️ Ungültiger Wochentag – breche Anzeige ab."));
        triggerActive = false;
        lastDisplayLetterError = DisplayLetterError::InvalidWeekday;
        ensureWiFiSymbolAfterError();
        return false;
    }

    String selectedColor = resolveDisplayColor(triggerIndex, static_cast<size_t>(today));

    Serial.print(F("🎨 Geladene Farbe für heute: "));
    Serial.println(selectedColor);

    if (selectedColor.length() != 7 || selectedColor[0] != '#') {
        Serial.println(F("⚠️ Fehler: Ungültige Farbe! Setze Standardfarbe Weiß."));
        selectedColor = "#FFFFFF";
    }

    uint16_t letterColor = color565FromHex(selectedColor);

    uint8_t builtinOverrideBitmap[SYMBOL_BITMAP_SIZE] = {};
    const bool useBuiltinOverride = getEditableBuiltinSymbolBitmap(letter, builtinOverrideBitmap);

    int customSymbolIndex = -1;
    if (letter >= '0' && letter <= '7') {
        customSymbolIndex = letter - '0';
    }
    const bool useCustomSymbol =
        customSymbolIndex >= 0 &&
        customSymbolIndex < static_cast<int>(CUSTOM_SYMBOL_COUNT) &&
        customSymbolEnabled[customSymbolIndex] == 1;

    if (!useBuiltinOverride && !useCustomSymbol && letterData.find(letter) == letterData.end()) {
        Serial.println(F("⚠️ Fehler: Zeichen/Symbol nicht gefunden!"));
        triggerActive = false;
        lastDisplayLetterError = DisplayLetterError::LetterNotFound;
        ensureWiFiSymbolAfterError();
        return false;
    }

    const uint8_t* bitmap = useBuiltinOverride
        ? builtinOverrideBitmap
        : (useCustomSymbol ? customSymbolBitmaps[customSymbolIndex] : letterData[letter]);

    wifiSymbolVisible = false;
    display.fillScreen(display.color565(0, 0, 0));
    display.display();
    delay(10);

    Serial.println(F("🖊️ Beginne Zeichnung..."));
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            uint8_t rowValue = (useBuiltinOverride || useCustomSymbol)
                ? bitmap[y * 4 + (x / 8)]
                : pgm_read_byte(&bitmap[y * 4 + (x / 8)]);
            if (rowValue & (1 << (7 - (x % 8)))) {
                display.setBrightness(display_brightness);
                display.fillRect(x * 2, y * 2, 2, 2, letterColor);
            }
        }
    }

    Serial.println(F("✅ Zeichen/Symbol auf Display gezeichnet!"));
    display.display();

    letterStartTime = millis();
    Serial.print(F("⏳ Anzeigezeit startet jetzt für "));
    Serial.print(letter_display_time);
    Serial.println(F(" Sekunden!"));

    lastDisplayLetterError = DisplayLetterError::None;
    return true;
}

void handleTrigger(char triggerType, bool isAutoMode, bool fromWeb) {
    uint8_t triggerIndex = 0;
    if (triggerType >= '1' && triggerType <= ('0' + NUM_TRIGGERS)) {
        triggerIndex = static_cast<uint8_t>(triggerType - '1');
    } else {
        Serial.print(F("⚠️ Unbekannter Trigger-Typ "));
        Serial.print(triggerType);
        Serial.println(F(" – verwende Trigger 1."));
        triggerIndex = 0;
    }

    if (wifiConnected) {
        if (!isAutoMode && !fromWeb) {
            Serial.println(F("⛔ WiFi wird abgeschaltet wegen Trigger (Quelle: Seriell)!"));
            WiFi.disconnect();
            wifiConnected = false;
            server.end();
            webServerRunning = false;
        } else {
            Serial.print(F("ℹ️ WiFi bleibt aktiv (Quelle: "));
            if (isAutoMode) {
                Serial.println(F("Automodus)."));
            } else {
                Serial.println(F("Web)."));
            }
        }
    }

    int today = resolveWeekdayForTriggerHandling();
    bool validDay = today >= 0 && today < static_cast<int>(NUM_DAYS);
    size_t delayDayIndex = validDay ? static_cast<size_t>(today) : 0;

    if (!validDay) {
        Serial.println(F("⚠️ Ungültiger Wochentag! Nutze Fallback-Index 0 für Verzögerungen."));
    }

    if (validDay) {
        if (!fromWeb && !isWithinStandaloneActiveWindow()) {
            Serial.println(F("🌙 Standby aktiv – Zeichen/Symbol wird außerhalb des Aktivfensters nicht angezeigt."));
            activeDisplayManagedBySchedule = false;
            return;
        }

        char letter = dailyLetters[triggerIndex][today];
        Serial.print(F("📅 Heute ist "));
        Serial.print(daysOfTheWeek[today]);
        Serial.print(F(" → Trigger "));
        Serial.print(triggerIndex + 1);
        Serial.print(F(" zeigt Zeichen/Symbol: "));
        Serial.println(letter);

        unsigned long delayTime = letter_trigger_delays[triggerIndex][delayDayIndex];
        if (!isAutoMode && !fromWeb) {
            if (delayTime == 0) {
                Serial.println(F("⚡ Keine Verzögerung für diesen Trigger hinterlegt."));
            } else {
                Serial.print(F("⏳ Konfigurierte Verzögerung: "));
                Serial.print(delayTime);
                Serial.println(F(" Sekunden (bereits eingehalten)."));
            }
        }

        bool displayed = displayLetter(triggerIndex, letter);

        if (displayed) {
            alreadyCleared = false;
            activeDisplayManagedBySchedule = isAutoMode;
        } else {
            activeDisplayManagedBySchedule = false;
            Serial.print(F("❌ Anzeige fehlgeschlagen: "));
            switch (lastDisplayLetterError) {
                case DisplayLetterError::TriggerAlreadyActive:
                    Serial.println(F("Ein anderes Zeichen/Symbol wird bereits angezeigt."));
                    break;
                case DisplayLetterError::InvalidWeekday:
                    Serial.println(F("Ungültiger Wochentag vom RTC-Modul."));
                    break;
                case DisplayLetterError::LetterNotFound:
                    Serial.println(F("Kein Muster für das angeforderte Zeichen/Symbol."));
                    break;
                case DisplayLetterError::None:
                default:
                    Serial.println(F("Unbekannter Fehler."));
                    break;
            }
        }

    } else {
        Serial.println(F("⚠️ Ungültiger Wochentag! Anzeige wird übersprungen."));
    }
}

void checkTrigger() {
    if (Serial.available() > 0) {
        char receivedChar = Serial.read();
        if (receivedChar == '1' || receivedChar == '2' || receivedChar == '3') {
            uint8_t triggerIndex = static_cast<uint8_t>(receivedChar - '1');

            Serial.print(F("🔔 Serieller Trigger für Eingang "));
            Serial.println(triggerIndex + 1);

            if (enqueuePendingTrigger(triggerIndex, false)) {
                Serial.println(F("🗓️ Trigger wurde zur Ausführung eingeplant."));
            }
        } else {
            Serial.print(F("❌ Unbekannter Trigger: "));
            Serial.println(receivedChar);
        }
    }
}

void checkAutoDisplay() {
    static unsigned long lastDisplayTime = 0;

    if (!autoDisplayMode) {
        return;
    }

    if (!isWithinStandaloneActiveWindow()) {
        if (triggerActive && activeDisplayManagedBySchedule) {
            Serial.println(F("🌙 Standalone-Aktivzeit beendet – automatische Anzeige wird gelöscht."));
            clearDisplay();
        }
        return;
    }

    if (millis() - lastDisplayTime > ((unsigned long)letter_auto_display_interval * 1000UL)) {
        lastDisplayTime = millis();
        Serial.println(F("🕒 Automodus aktiv: Zeige heutiges Zeichen/Symbol automatisch!"));

        handleTrigger('1', true, false);
    }
}

bool isWithinStandaloneActiveWindow() {
    uint16_t minutesOfDay = 0;
    if (!getRTCMinutesOfDay(minutesOfDay)) {
        Serial.println(F("⚠️ RTC-Zeit für Aktivfenster nicht verfügbar – Standalone-Anzeige bleibt aktiv."));
        return true;
    }

    return isWithinStandaloneWindow(minutesOfDay);
}
