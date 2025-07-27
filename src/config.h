#ifndef CONFIG_H
#define CONFIG_H

#include <Wire.h>
#include <RTClib.h>
#include <PxMatrix.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <EEPROM.h>
#include <cstring>
#include <map>
#include <Ticker.h>
#include "letters.h"

// **EEPROM Speichergröße**
#define EEPROM_SIZE 512

// **RS485 & RTC Pins**
#define GPIO_RS485_ENABLE 10
#define I2C_SDA 3
#define I2C_SCL 1
#define RS485_RX 3
#define RS485_TX 1

// **LED-Matrix Pins**
#define P_A D1
#define P_B D2
#define P_C D8
#define P_D D6
#define P_E D3
#define P_CLK D5
#define P_LAT D0
#define P_OE D4
#define P_R1 D7

// **Standard-WiFi-Daten (werden bei Erststart gesetzt)**
extern char wifi_ssid[50];
extern char wifi_password[50];
extern char hostname[50];
extern int wifi_connect_timeout; // Timeout für die WLAN-Verbindung in Sekunden

// **Globale Variablen für die Anzeige**

extern Ticker display_ticker;
extern bool triggerActive;
extern unsigned long letterStartTime;

// **Buchstaben für Wochentage (Standardwerte)**
extern char dailyLetters[7];

// **Buchstabenfarben für die Wochentage (Standard: Weiß)**
extern char dailyLetterColors[7][8];

// **Alle auswählbaren Buchstaben (A-Z & `*`)**
const char availableLetters[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '*', '#', '~', '&', '?'};

// **Konfiguration für Buchstabenanzeige**
extern int display_brightness;           // Standard: 100
extern unsigned long letter_display_time;           // Standard: 10 Sekunden
extern unsigned long letter_trigger_delay_1; // Verzögerung für Trigger 1
extern unsigned long letter_trigger_delay_2; // Verzögerung für Trigger 2
extern unsigned long letter_trigger_delay_3; // Verzögerung für Trigger 3
extern unsigned long letter_auto_display_interval; // Standard: 5 Minuten

// **Modus für Buchstabenanzeige (Auto/Trigger)**
extern bool autoDisplayMode;

// **RTC-Instanz**
extern RTC_DS1307 rtc;
extern bool rtc_ok;
extern String startTime;

// **LED-Matrix Instanz**
extern PxMATRIX display;

// **Webserver**
extern AsyncWebServer server;

// **WiFi Status**
extern bool wifiConnected;

// **Wochentags-Array**
extern const char* daysOfTheWeek[7];


// **💾 Einstellungen speichern in EEPROM**
void saveConfig();

// **📂 Einstellungen aus EEPROM laden**
void loadConfig();


void display_updater();

// **LED-Matrix Setup-Funktion**
void setupMatrix();

void checkMemoryUsage();

#endif
