#ifndef TRIGGER_HANDLER_H
#define TRIGGER_HANDLER_H

#include "config.h"
#include "matrix_display.h"
#include "letters.h"

  static bool alreadyCleared;

void clearDisplay() {
    
    if (alreadyCleared) {
        Serial.println("⚠️ `clearDisplay()` wurde bereits ausgeführt, Abbruch.");
        return;
    }

    Serial.println("🧹 Buchstabe wird jetzt gelöscht!");
    
    display.fillScreen(display.color565(0, 0, 0));
    display.display();

    alreadyCleared = true;
    triggerActive = false;
}

// **Funktion: Buchstaben oder Sonderzeichen anzeigen**
void displayLetter(char letter) {
    Serial.println("🎨 Zeichne Buchstabe: " + String(letter));

    if (triggerActive) {
        Serial.println("⚠️ Ein Buchstabe ist bereits aktiv. Abbruch.");
        return;
    }

    triggerActive = true;  

    if (letter == '*') {
        letter = (random(2) == 0) ? '#' : '&';
        Serial.print("🔀 `*` wurde ersetzt durch: ");
        Serial.println(letter);
    }

    // **Heutigen Wochentag abrufen**
    int today = getRTCWeekday();  // 0 = Sonntag, 6 = Samstag
    Serial.print("📅 Heutiger Wochentag: ");
    Serial.println(today);

    // **Farbe aus `dailyLetterColors[today]` abrufen**
    String selectedColor = dailyLetterColors[today];  // Farbe für den heutigen Tag

    Serial.print("🎨 Geladene Farbe für heute: ");
    Serial.println(selectedColor);  // Sollte z.B. "#0000FF" sein

    // **Falls ungültige Farbe, Standard auf Weiß setzen**
    if (selectedColor.length() != 7 || selectedColor[0] != '#') {
        Serial.println("⚠️ Fehler: Ungültige Farbe! Setze Standardfarbe Weiß.");
        selectedColor = "#FFFFFF";
    }

    // **HEX-Farbe nach RGB umwandeln**
    uint32_t colorHex = strtol(selectedColor.c_str() + 1, NULL, 16);
    uint8_t r = (colorHex >> 16) & 0xFF;
    uint8_t g = (colorHex >> 8) & 0xFF;
    uint8_t b = colorHex & 0xFF;

    // **RGB nach RGB565 umwandeln**
    uint16_t letterColor = display.color565(r, g, b);

    // **Debugging der Farbwerte**
    Serial.print("🎨 Konvertierte RGB-Werte: R=");
    Serial.print(r);
    Serial.print(", G=");
    Serial.print(g);
    Serial.print(", B=");
    Serial.println(b);

    // **Display vor Zeichnung komplett leeren**
    display.fillScreen(display.color565(0, 0, 0));
    display.display();
    delay(10);

    if (letterData.find(letter) == letterData.end()) {
        Serial.println("⚠️ Fehler: Buchstabe nicht gefunden!");
        triggerActive = false;
        return;
    }

    const uint8_t* bitmap = letterData[letter];

    Serial.println("🖊️ Beginne Zeichnung...");
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            uint8_t rowValue = pgm_read_byte(&bitmap[y * 4 + (x / 8)]);
            if (rowValue & (1 << (7 - (x % 8)))) {
                display.setBrightness(display_brightness);
                display.fillRect(x * 2, y * 2, 2, 2, letterColor);
            }
        }
    }

    Serial.println("✅ Buchstabe auf Display gezeichnet!");
    display.display();

    // **Anzeigezeit speichern**
    letterStartTime = millis();
    Serial.print("⏳ Anzeigezeit startet jetzt für ");
    Serial.print(letter_display_time);
    Serial.println(" Sekunden!");
}


void handleTrigger(char triggerType, bool isAutoMode = false) {
    if (wifiConnected && !isAutoMode) {  
        Serial.println("⛔ WiFi wird abgeschaltet wegen Trigger!");
        WiFi.disconnect();
        wifiConnected = false;
        server.end();
    }

    int today = getRTCWeekday();
    
    if (today >= 0 && today < 7) {
        char letter = dailyLetters[today];
        Serial.print("📅 Heute ist ");
        Serial.print(daysOfTheWeek[today]);
        Serial.print(" → Zeige Buchstabe: ");
        Serial.println(letter);

        // **Verzögerung NUR für manuelle Trigger**
    int delayTime = 0;
    if (!isAutoMode) {
        if (triggerType == '1') delayTime = letter_trigger_delay_1;
        else if (triggerType == '2') delayTime = letter_trigger_delay_2;
        else if (triggerType == '3') delayTime = letter_trigger_delay_3;

        Serial.print("⏳ Warte auf Trigger-Verzögerung: ");
        Serial.print(delayTime);
        Serial.println(" Sekunden...");
        delay(delayTime * 1000);
    }

    displayLetter(letter);
        
    alreadyCleared = false;

    } else {
        Serial.println("⚠️ Ungültiger Wochentag!");
    }
}

void checkTrigger() {
    if (Serial.available() > 0) {
        char receivedChar = Serial.read();
        if (receivedChar == '1' || receivedChar == '2' || receivedChar == '3') {
            Serial.println("🔔 Trigger erhalten: Zeige heutigen Buchstaben!");
            handleTrigger(receivedChar, false);
        } else {
            Serial.print("❌ Unbekannter Trigger: ");
            Serial.println(receivedChar);
        }
    }
}

void checkAutoDisplay() {
    static unsigned long lastDisplayTime = 0;

    if (autoDisplayMode && millis() - lastDisplayTime > (letter_auto_display_interval * 1000)) {
        lastDisplayTime = millis();
        Serial.println("🕒 Automodus aktiv: Zeige heutigen Buchstaben automatisch!");

        handleTrigger('1', true);
    }
}

#endif