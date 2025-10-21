#ifndef TRIGGER_HANDLER_H
#define TRIGGER_HANDLER_H

#include "config.h"
#include "letters.h"

extern bool alreadyCleared;

void clearDisplay();

// **Funktion: Buchstaben oder Sonderzeichen anzeigen**
void displayLetter(char letter);

void handleTrigger(char triggerType, bool isAutoMode = false, bool keepWiFi = false);

void checkTrigger();

void checkAutoDisplay();

#endif
