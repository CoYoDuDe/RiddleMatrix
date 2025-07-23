#ifndef MATRIX_DISPLAY_H
#define MATRIX_DISPLAY_H

#include "config.h"
#include "letters.h"

// **LED-Test mit Röhren-TV-Effekt**
void testLEDMatrix() {
    Serial.println("🔴 LED-Test gestartet...");

    display.fillScreen(display.color565(255, 0, 0)); 
    display.display();
    delay(800);

    display.fillScreen(display.color565(0, 255, 0)); 
    display.display();
    delay(800);

    display.fillScreen(display.color565(0, 0, 255)); 
    display.display();
    delay(800);

    // **Röhren-TV-Ausschalt-Effekt**
    for (int r = 32; r > 0; r--) {
        display.fillScreen(display.color565(0, 0, 0));
        display.fillCircle(32, 32, r, display.color565(255, 255, 255));
        display.display();
        delay(8);
    }

    display.fillScreen(display.color565(0, 0, 0));
    display.display();
    delay(300);

    display.clearDisplay();
    display.display();

    Serial.println("✅ LED-Test beendet!");
}

#endif