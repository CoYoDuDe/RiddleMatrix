#ifndef TRIGGER_HANDLER_H
#define TRIGGER_HANDLER_H

#include "config.h"
#include "letters.h"

extern bool alreadyCleared;

struct PendingTrigger {
    uint8_t triggerIndex;
    unsigned long executeAt;
    bool fromWeb;
};

extern bool pendingTriggerActive;

void clearDisplay();

enum class DisplayLetterError : uint8_t {
    None = 0,
    TriggerAlreadyActive,
    InvalidWeekday,
    LetterNotFound
};

extern DisplayLetterError lastDisplayLetterError;

// **Funktion: Buchstaben oder Sonderzeichen anzeigen**
bool displayLetter(uint8_t triggerIndex, char letter);

void handleTrigger(char triggerType, bool isAutoMode = false, bool fromWeb = false);

bool enqueuePendingTrigger(uint8_t triggerIndex, bool fromWeb);

bool isTriggerPending(uint8_t triggerIndex);

void processPendingTriggers();

void checkTrigger();

void checkAutoDisplay();

#endif
