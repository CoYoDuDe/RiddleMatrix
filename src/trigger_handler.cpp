#include "trigger_handler.h"
#include "rtc_manager.h"

DisplayLetterError lastDisplayLetterError = DisplayLetterError::None;
bool pendingTriggerActive = false;

namespace {

constexpr size_t MAX_PENDING_TRIGGERS = NUM_TRIGGERS * 3;

PendingTrigger pendingQueue[MAX_PENDING_TRIGGERS];
size_t pendingTriggerCount = 0;

void shiftPendingQueue(size_t fromIndex) {
    if (fromIndex >= pendingTriggerCount) {
        return;
    }

    for (size_t i = fromIndex + 1; i < pendingTriggerCount; ++i) {
        pendingQueue[i - 1] = pendingQueue[i];
    }
}

} // namespace

void clearDisplay() {
    if (alreadyCleared) {
        Serial.println(F("⚠️ `clearDisplay()` wurde bereits ausgeführt, Abbruch."));
        return;
    }

    Serial.println(F("🧹 Buchstabe wird jetzt gelöscht!"));

    display.fillScreen(display.color565(0, 0, 0));
    display.display();

    alreadyCleared = true;
    triggerActive = false;
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

    if (triggerActive) {
        Serial.println(F("⚠️ Anzeige läuft noch – neuer Trigger wird verworfen."));
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

    int today = getRTCWeekday();
    if (today < 0 || today >= static_cast<int>(NUM_DAYS)) {
        Serial.println(F("⚠️ Ungültiger Wochentag – Trigger kann nicht geplant werden."));
        return false;
    }

    unsigned long delaySeconds = letter_trigger_delays[triggerIndex][static_cast<size_t>(today)];
    unsigned long executeAt = millis() + (delaySeconds * 1000UL);

    pendingQueue[pendingTriggerCount++] = {triggerIndex, executeAt, fromWeb, static_cast<int8_t>(today)};
    pendingTriggerActive = true;

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

            handleTrigger(static_cast<char>('1' + current.triggerIndex), false, static_cast<int>(current.scheduledWeekday));

            if (triggerActive) {
                break;
            }

            now = millis();
            continue;
        }

        ++index;
    }

    if (pendingTriggerCount == 0) {
        pendingTriggerActive = false;
    }
}

bool displayLetter(uint8_t triggerIndex, char letter, int weekdayOverride) {
    lastDisplayLetterError = DisplayLetterError::None;

    if (triggerIndex >= NUM_TRIGGERS) {
        Serial.print(F("⚠️ Ungültiger Trigger-Index "));
        Serial.print(triggerIndex);
        Serial.println(F(" – fallback auf Trigger 1."));
        triggerIndex = 0;
    }

    Serial.print(F("🎨 Zeichne Buchstabe für Trigger "));
    Serial.print(triggerIndex + 1);
    Serial.print(F(": "));
    Serial.println(letter);

    if (triggerActive) {
        Serial.println(F("⚠️ Ein Buchstabe ist bereits aktiv. Abbruch."));
        lastDisplayLetterError = DisplayLetterError::TriggerAlreadyActive;
        return false;
    }

    triggerActive = true;

    if (letter == '*') {
        letter = (random(2) == 0) ? '#' : '&';
        Serial.print(F("🔀 `*` wurde ersetzt durch: "));
        Serial.println(letter);
    }

    bool useOverride = weekdayOverride >= 0 && weekdayOverride < static_cast<int>(NUM_DAYS);
    int effectiveDay = useOverride ? weekdayOverride : getRTCWeekday();

    Serial.print(useOverride ? F("📅 Verwende gespeicherten Wochentag: ") : F("📅 Heutiger Wochentag: "));
    Serial.println(effectiveDay);

    if (effectiveDay < 0 || effectiveDay >= static_cast<int>(NUM_DAYS)) {
        Serial.println(F("⚠️ Ungültiger Wochentag – breche Anzeige ab."));
        triggerActive = false;
        lastDisplayLetterError = DisplayLetterError::InvalidWeekday;
        return false;
    }

    String selectedColor(dailyLetterColors[triggerIndex][effectiveDay]);

    Serial.print(F("🎨 Geladene Farbe für heute: "));
    Serial.println(selectedColor);

    if (selectedColor.length() != 7 || selectedColor[0] != '#') {
        Serial.println(F("⚠️ Fehler: Ungültige Farbe! Setze Standardfarbe Weiß."));
        selectedColor = "#FFFFFF";
    }

    uint32_t colorHex = strtol(selectedColor.c_str() + 1, NULL, 16);
    uint8_t r = (colorHex >> 16) & 0xFF;
    uint8_t g = (colorHex >> 8) & 0xFF;
    uint8_t b = colorHex & 0xFF;

    uint16_t letterColor = display.color565(r, g, b);

    Serial.print(F("🎨 Konvertierte RGB-Werte: R="));
    Serial.print(r);
    Serial.print(F(", G="));
    Serial.print(g);
    Serial.print(F(", B="));
    Serial.println(b);

    display.fillScreen(display.color565(0, 0, 0));
    display.display();
    delay(10);

    if (letterData.find(letter) == letterData.end()) {
        Serial.println(F("⚠️ Fehler: Buchstabe nicht gefunden!"));
        triggerActive = false;
        lastDisplayLetterError = DisplayLetterError::LetterNotFound;
        return false;
    }

    const uint8_t* bitmap = letterData[letter];

    Serial.println(F("🖊️ Beginne Zeichnung..."));
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            uint8_t rowValue = pgm_read_byte(&bitmap[y * 4 + (x / 8)]);
            if (rowValue & (1 << (7 - (x % 8)))) {
                display.setBrightness(display_brightness);
                display.fillRect(x * 2, y * 2, 2, 2, letterColor);
            }
        }
    }

    Serial.println(F("✅ Buchstabe auf Display gezeichnet!"));
    display.display();

    letterStartTime = millis();
    Serial.print(F("⏳ Anzeigezeit startet jetzt für "));
    Serial.print(letter_display_time);
    Serial.println(F(" Sekunden!"));

    lastDisplayLetterError = DisplayLetterError::None;
    return true;
}

void handleTrigger(char triggerType, bool isAutoMode, int weekdayOverride) {
    uint8_t triggerIndex = 0;
    if (triggerType >= '1' && triggerType <= ('0' + NUM_TRIGGERS)) {
        triggerIndex = static_cast<uint8_t>(triggerType - '1');
    } else {
        Serial.print(F("⚠️ Unbekannter Trigger-Typ "));
        Serial.print(triggerType);
        Serial.println(F(" – verwende Trigger 1."));
        triggerIndex = 0;
    }

    if (wifiConnected && !isAutoMode) {
        Serial.println(F("⛔ WiFi wird abgeschaltet wegen Trigger!"));
        WiFi.disconnect();
        wifiConnected = false;
        server.end();
    }

    bool overrideValid = weekdayOverride >= 0 && weekdayOverride < static_cast<int>(NUM_DAYS);
    int effectiveDay = overrideValid ? weekdayOverride : getRTCWeekday();
    bool validDay = effectiveDay >= 0 && effectiveDay < static_cast<int>(NUM_DAYS);

    if (!validDay) {
        Serial.println(F("⚠️ Ungültiger Wochentag! Anzeige wird übersprungen."));
        return;
    }

    size_t delayDayIndex = static_cast<size_t>(effectiveDay);
    char letter = dailyLetters[triggerIndex][effectiveDay];

    Serial.print(F("📅 "));
    Serial.print(overrideValid ? F("Geplanter Tag ") : F("Heute ist "));
    Serial.print(daysOfTheWeek[effectiveDay]);
    Serial.print(F(" → Trigger "));
    Serial.print(triggerIndex + 1);
    Serial.print(F(" zeigt Buchstabe: "));
    Serial.println(letter);

    unsigned long delayTime = letter_trigger_delays[triggerIndex][delayDayIndex];
    if (!isAutoMode) {
        if (delayTime == 0) {
            Serial.println(F("⚡ Keine Verzögerung für diesen Trigger hinterlegt."));
        } else {
            Serial.print(F("⏳ Konfigurierte Verzögerung: "));
            Serial.print(delayTime);
            Serial.println(F(" Sekunden (bereits eingehalten)."));
        }
    }

    bool displayed = displayLetter(triggerIndex, letter, effectiveDay);

    if (displayed) {
        alreadyCleared = false;
    } else {
        Serial.print(F("❌ Anzeige fehlgeschlagen: "));
        switch (lastDisplayLetterError) {
            case DisplayLetterError::TriggerAlreadyActive:
                Serial.println(F("Ein anderer Buchstabe wird bereits angezeigt."));
                break;
            case DisplayLetterError::InvalidWeekday:
                Serial.println(F("Ungültiger Wochentag vom RTC-Modul."));
                break;
            case DisplayLetterError::LetterNotFound:
                Serial.println(F("Kein Muster für den angeforderten Buchstaben."));
                break;
            case DisplayLetterError::None:
            default:
                Serial.println(F("Unbekannter Fehler."));
                break;
        }
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

    if (autoDisplayMode && (millis() - lastDisplayTime > ((unsigned long)letter_auto_display_interval * 1000UL))) {
        lastDisplayTime = millis();
        Serial.println(F("🕒 Automodus aktiv: Zeige heutigen Buchstaben automatisch!"));

        handleTrigger('1', true);
    }
}

