#ifndef CONFIG_H
#define CONFIG_H

#include <Wire.h>
#include <RTClib.h>
#include <PxMatrix.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <EEPROM.h>
#include <cstddef>
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

// **Allgemeine Konstanten für Trigger und EEPROM**
static constexpr size_t NUM_TRIGGERS = 3;
static constexpr size_t NUM_DAYS = 7;
static constexpr size_t COLOR_STRING_LENGTH = 8; // "#RRGGBB" + Terminator

static constexpr uint16_t EEPROM_OFFSET_WIFI_SSID = 0;
static constexpr uint16_t EEPROM_OFFSET_WIFI_PASSWORD = EEPROM_OFFSET_WIFI_SSID + 50;
static constexpr uint16_t EEPROM_OFFSET_HOSTNAME = EEPROM_OFFSET_WIFI_PASSWORD + 50;
static constexpr uint16_t EEPROM_OFFSET_DAILY_LETTERS = EEPROM_OFFSET_HOSTNAME + 50;
static constexpr uint16_t EEPROM_OFFSET_DAILY_LETTER_COLORS = 200; // Historischer Versatz für Rückwärtskompatibilität
static constexpr uint16_t EEPROM_OFFSET_DISPLAY_BRIGHTNESS = EEPROM_OFFSET_DAILY_LETTER_COLORS + (NUM_TRIGGERS * NUM_DAYS * COLOR_STRING_LENGTH);
static constexpr uint16_t EEPROM_OFFSET_LETTER_DISPLAY_TIME = EEPROM_OFFSET_DISPLAY_BRIGHTNESS + sizeof(int);
static constexpr uint16_t EEPROM_OFFSET_TRIGGER_DELAY_MATRIX = EEPROM_OFFSET_LETTER_DISPLAY_TIME + sizeof(unsigned long);
static constexpr size_t EEPROM_TRIGGER_DELAY_MATRIX_SIZE = NUM_TRIGGERS * NUM_DAYS * sizeof(unsigned long);
static constexpr uint16_t EEPROM_OFFSET_AUTO_INTERVAL = EEPROM_OFFSET_TRIGGER_DELAY_MATRIX + EEPROM_TRIGGER_DELAY_MATRIX_SIZE;
static constexpr uint16_t EEPROM_OFFSET_AUTO_MODE = EEPROM_OFFSET_AUTO_INTERVAL + sizeof(unsigned long);
static constexpr uint16_t EEPROM_OFFSET_WIFI_CONNECT_TIMEOUT = EEPROM_OFFSET_AUTO_MODE + sizeof(uint8_t);
static constexpr uint16_t EEPROM_OFFSET_CONFIG_VERSION = EEPROM_OFFSET_WIFI_CONNECT_TIMEOUT + sizeof(int);
static constexpr uint16_t EEPROM_CONFIG_VERSION = 3;

static_assert(EEPROM_OFFSET_CONFIG_VERSION >= EEPROM_OFFSET_TRIGGER_DELAY_MATRIX + EEPROM_TRIGGER_DELAY_MATRIX_SIZE,
              "Config version offset overlaps trigger delay matrix");
static_assert(EEPROM_OFFSET_CONFIG_VERSION + sizeof(uint16_t) <= EEPROM_SIZE,
              "Config version exceeds allocated EEPROM size");

// **Standard-WiFi-Daten (werden bei Erststart gesetzt)**
extern char wifi_ssid[50];
extern char wifi_password[50];
extern char hostname[50];
extern int wifi_connect_timeout; // Timeout für die WLAN-Verbindung in Sekunden

// **Globale Variablen für die Anzeige**

extern Ticker display_ticker;
extern bool triggerActive;
extern unsigned long letterStartTime;
extern unsigned long wifiStartTime;

// **Buchstaben für Wochentage (Standardwerte)**
extern char dailyLetters[NUM_TRIGGERS][NUM_DAYS];

// **Buchstabenfarben für die Wochentage (Standard: Weiß)**
extern char dailyLetterColors[NUM_TRIGGERS][NUM_DAYS][COLOR_STRING_LENGTH];

// **Alle auswählbaren Buchstaben (A-Z & `*`)**
const char availableLetters[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '*', '#', '~', '&', '?'};

// **Konfiguration für Buchstabenanzeige**
extern int display_brightness;           // Standard: 100
extern unsigned long letter_display_time;           // Standard: 10 Sekunden
extern unsigned long letter_trigger_delays[NUM_TRIGGERS][NUM_DAYS];
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
