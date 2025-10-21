#include "trigger_handler.h"
#include "rtc_manager.h"

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

void displayLetter(char letter) {
    Serial.print(F("🎨 Zeichne Buchstabe: "));
    Serial.println(letter);

    if (triggerActive) {
        Serial.println(F("⚠️ Ein Buchstabe ist bereits aktiv. Abbruch."));
        return;
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

    String selectedColor(dailyLetterColors[today]);

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
        return;
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
}

void handleTrigger(char triggerType, bool isAutoMode, bool keepWiFi) {
    if (wifiConnected && !isAutoMode && !keepWiFi) {
        Serial.println(F("⛔ WiFi wird abgeschaltet wegen Trigger!"));
        WiFi.disconnect();
        wifiConnected = false;
        server.end();
    }

    int today = getRTCWeekday();

    if (today >= 0 && today < 7) {
        char letter = dailyLetters[today];
        Serial.print(F("📅 Heute ist "));
        Serial.print(daysOfTheWeek[today]);
        Serial.print(F(" → Zeige Buchstabe: "));
        Serial.println(letter);

        int delayTime = 0;
        if (!isAutoMode) {
            if (triggerType == '1') delayTime = letter_trigger_delay_1;
            else if (triggerType == '2') delayTime = letter_trigger_delay_2;
            else if (triggerType == '3') delayTime = letter_trigger_delay_3;

            Serial.print(F("⏳ Warte auf Trigger-Verzögerung: "));
            Serial.print(delayTime);
            Serial.println(F(" Sekunden..."));
            delay(delayTime * 1000);
        }

        displayLetter(letter);

        alreadyCleared = false;

    } else {
        Serial.println(F("⚠️ Ungültiger Wochentag!"));
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

