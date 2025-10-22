#include "trigger_handler.h"
#include "rtc_manager.h"

DisplayLetterError lastDisplayLetterError = DisplayLetterError::None;

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

bool displayLetter(uint8_t triggerIndex, char letter) {
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

    int today = getRTCWeekday();
    Serial.print(F("📅 Heutiger Wochentag: "));
    Serial.println(today);

    if (today < 0 || today >= static_cast<int>(NUM_DAYS)) {
        Serial.println(F("⚠️ Ungültiger Wochentag – breche Anzeige ab."));
        triggerActive = false;
        lastDisplayLetterError = DisplayLetterError::InvalidWeekday;
        return false;
    }

    String selectedColor(dailyLetterColors[triggerIndex][today]);

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

void handleTrigger(char triggerType, bool isAutoMode) {
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

    int today = getRTCWeekday();
    bool validDay = today >= 0 && today < static_cast<int>(NUM_DAYS);
    size_t delayDayIndex = validDay ? static_cast<size_t>(today) : 0;

    if (!validDay) {
        Serial.println(F("⚠️ Ungültiger Wochentag! Nutze Fallback-Index 0 für Verzögerungen."));
    }

    if (validDay) {
        char letter = dailyLetters[triggerIndex][today];
        Serial.print(F("📅 Heute ist "));
        Serial.print(daysOfTheWeek[today]);
        Serial.print(F(" → Trigger "));
        Serial.print(triggerIndex + 1);
        Serial.print(F(" zeigt Buchstabe: "));
        Serial.println(letter);

        unsigned long delayTime = 0;
        if (!isAutoMode) {
            delayTime = letter_trigger_delays[triggerIndex][delayDayIndex];
            Serial.print(F("⏳ Warte auf Trigger-Verzögerung: "));
            Serial.print(delayTime);
            Serial.println(F(" Sekunden..."));
            delay(delayTime * 1000UL);
        }

        bool displayed = displayLetter(triggerIndex, letter);

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

    } else {
        Serial.println(F("⚠️ Ungültiger Wochentag! Anzeige wird übersprungen."));
    }
}

void checkTrigger() {
    if (Serial.available() > 0) {
        char receivedChar = Serial.read();
        if (receivedChar == '1' || receivedChar == '2' || receivedChar == '3') {
            Serial.println(F("🔔 Trigger erhalten: Zeige heutigen Buchstaben!"));
            handleTrigger(receivedChar, false);
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

