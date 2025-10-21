#ifndef WEB_MANAGER_H
#define WEB_MANAGER_H

#include "config.h"
#include "trigger_handler.h"
#include "rtc_manager.h"
#include <ESPAsyncWebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <stdlib.h>

extern const char scriptJS[] PROGMEM;

void setupWebServer();

#endif
