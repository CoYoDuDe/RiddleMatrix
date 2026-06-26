#ifndef CONFIG_H
#define CONFIG_H

#include <Wire.h>
#include <RTClib.h>
#include <PxMatrix.h>
#if defined(ESP32)
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif
#include <ESPAsyncWebServer.h>
#include <EEPROM.h>
#include <cstddef>
#include <cstring>
#include <map>
#include <Ticker.h>
#include "letters.h"

// **EEPROM Speichergröße**
#define EEPROM_SIZE 4096

// **RS485 & RTC Pins**
#define GPIO_RS485_ENABLE 10
#define I2C_SDA 3
#define I2C_SCL 1
#define RS485_RX 3
#define RS485_TX 1

// **LED-Matrix Pins**
#if defined(ESP32)
#ifndef P_A
#define P_A 19
#endif
#ifndef P_B
#define P_B 23
#endif
#ifndef P_C
#define P_C 18
#endif
#ifndef P_D
#define P_D 5
#endif
#ifndef P_E
#define P_E 15
#endif
#ifndef P_CLK
#define P_CLK 14
#endif
#ifndef P_LAT
#define P_LAT 4
#endif
#ifndef P_OE
#define P_OE 13
#endif
#ifndef P_R1
#define P_R1 25
#endif
#else
#define P_A D1
#define P_B D2
#define P_C D8
#define P_D D6
#define P_E D3
#define P_CLK D5
#define P_LAT D0
#define P_OE D4
#define P_R1 D7
#endif

// **Allgemeine Konstanten für Trigger und EEPROM**
static constexpr size_t NUM_TRIGGERS = 3;
static constexpr size_t NUM_DAYS = 7;
static constexpr size_t COLOR_STRING_LENGTH = 8; // "#RRGGBB" + Terminator
static constexpr size_t RANDOM_COLOR_PALETTE_SIZE = 8;
static constexpr size_t CUSTOM_SYMBOL_COUNT = 8;
static constexpr size_t EDITABLE_BUILTIN_SYMBOL_COUNT = 31;
static constexpr size_t SYMBOL_BITMAP_SIZE = 128;

#if defined(RIDDLEMATRIX_HOST_TEST)
static constexpr size_t EEPROM_SIZEOF_UNSIGNED_LONG = 4;
#else
static constexpr size_t EEPROM_SIZEOF_UNSIGNED_LONG = sizeof(unsigned long);
#endif

static constexpr uint16_t EEPROM_OFFSET_WIFI_SSID = 0;
static constexpr uint16_t EEPROM_OFFSET_WIFI_PASSWORD = EEPROM_OFFSET_WIFI_SSID + 50;
static constexpr uint16_t EEPROM_OFFSET_HOSTNAME = EEPROM_OFFSET_WIFI_PASSWORD + 50;
static constexpr uint16_t EEPROM_OFFSET_DAILY_LETTERS = EEPROM_OFFSET_HOSTNAME + 50;
static constexpr uint16_t EEPROM_OFFSET_DAILY_LETTER_COLORS = EEPROM_OFFSET_DAILY_LETTERS + (NUM_TRIGGERS * NUM_DAYS);
static constexpr uint16_t EEPROM_OFFSET_DISPLAY_BRIGHTNESS = EEPROM_OFFSET_DAILY_LETTER_COLORS + (NUM_TRIGGERS * NUM_DAYS * COLOR_STRING_LENGTH);
static constexpr uint16_t EEPROM_OFFSET_LETTER_DISPLAY_TIME = EEPROM_OFFSET_DISPLAY_BRIGHTNESS + sizeof(int);
static constexpr uint16_t EEPROM_OFFSET_TRIGGER_DELAY_MATRIX = EEPROM_OFFSET_LETTER_DISPLAY_TIME + EEPROM_SIZEOF_UNSIGNED_LONG;
static constexpr size_t EEPROM_TRIGGER_DELAY_MATRIX_SIZE = NUM_TRIGGERS * NUM_DAYS * EEPROM_SIZEOF_UNSIGNED_LONG;
static constexpr uint16_t EEPROM_OFFSET_AUTO_INTERVAL = EEPROM_OFFSET_TRIGGER_DELAY_MATRIX + EEPROM_TRIGGER_DELAY_MATRIX_SIZE;
static constexpr uint16_t EEPROM_OFFSET_AUTO_MODE = EEPROM_OFFSET_AUTO_INTERVAL + EEPROM_SIZEOF_UNSIGNED_LONG;
static constexpr uint16_t EEPROM_OFFSET_WIFI_CONNECT_TIMEOUT = EEPROM_OFFSET_AUTO_MODE + sizeof(uint8_t);
static constexpr uint16_t EEPROM_OFFSET_CONFIG_VERSION = EEPROM_OFFSET_WIFI_CONNECT_TIMEOUT + sizeof(int);
static constexpr uint16_t EEPROM_OFFSET_ACTIVE_START_MINUTES = EEPROM_OFFSET_CONFIG_VERSION + sizeof(uint16_t);
static constexpr uint16_t EEPROM_OFFSET_ACTIVE_END_MINUTES = EEPROM_OFFSET_ACTIVE_START_MINUTES + sizeof(uint16_t);
static constexpr uint16_t EEPROM_OFFSET_COLOR_MODE_MATRIX = EEPROM_OFFSET_ACTIVE_END_MINUTES + sizeof(uint16_t);
static constexpr size_t EEPROM_COLOR_MODE_MATRIX_SIZE = NUM_TRIGGERS * NUM_DAYS * sizeof(uint8_t);
static constexpr uint16_t EEPROM_OFFSET_COLOR_PALETTE_MASK_MATRIX = EEPROM_OFFSET_COLOR_MODE_MATRIX + EEPROM_COLOR_MODE_MATRIX_SIZE;
static constexpr size_t EEPROM_COLOR_PALETTE_MASK_MATRIX_SIZE = NUM_TRIGGERS * NUM_DAYS * sizeof(uint16_t);
static constexpr uint16_t EEPROM_OFFSET_WIFI_OPERATION_MODE = EEPROM_OFFSET_COLOR_PALETTE_MASK_MATRIX + EEPROM_COLOR_PALETTE_MASK_MATRIX_SIZE;
static constexpr uint16_t EEPROM_OFFSET_WIFI_STATUS_SYMBOL_ENABLED = EEPROM_OFFSET_WIFI_OPERATION_MODE + sizeof(uint8_t);
static constexpr uint16_t EEPROM_OFFSET_WIFI_STATIC_IP_ENABLED = EEPROM_OFFSET_WIFI_STATUS_SYMBOL_ENABLED + sizeof(uint8_t);
static constexpr uint16_t EEPROM_OFFSET_WIFI_STATIC_IP = EEPROM_OFFSET_WIFI_STATIC_IP_ENABLED + sizeof(uint8_t);
static constexpr uint16_t EEPROM_OFFSET_WIFI_GATEWAY = EEPROM_OFFSET_WIFI_STATIC_IP + 16;
static constexpr uint16_t EEPROM_OFFSET_WIFI_SUBNET = EEPROM_OFFSET_WIFI_GATEWAY + 16;
static constexpr uint16_t EEPROM_OFFSET_WIFI_DNS = EEPROM_OFFSET_WIFI_SUBNET + 16;
static constexpr uint16_t EEPROM_OFFSET_WIFI_LOCAL_AP_SSID = EEPROM_OFFSET_WIFI_DNS + 16;
static constexpr uint16_t EEPROM_OFFSET_WIFI_LOCAL_AP_PASSWORD = EEPROM_OFFSET_WIFI_LOCAL_AP_SSID + 50;
static constexpr uint16_t EEPROM_OFFSET_CUSTOM_SYMBOL_BITMAPS = EEPROM_OFFSET_WIFI_LOCAL_AP_PASSWORD + 50;
static constexpr size_t EEPROM_CUSTOM_SYMBOL_BITMAPS_SIZE = CUSTOM_SYMBOL_COUNT * SYMBOL_BITMAP_SIZE;
static constexpr uint16_t EEPROM_OFFSET_CUSTOM_SYMBOL_ENABLED = EEPROM_OFFSET_CUSTOM_SYMBOL_BITMAPS + EEPROM_CUSTOM_SYMBOL_BITMAPS_SIZE;
static constexpr uint16_t EEPROM_CONFIG_VERSION = 9;

static_assert(EEPROM_OFFSET_DAILY_LETTERS + (NUM_TRIGGERS * NUM_DAYS) <= EEPROM_OFFSET_DAILY_LETTER_COLORS,
              "Letter-Block überschneidet sich mit Farb-Block");
static_assert(EEPROM_OFFSET_DAILY_LETTER_COLORS + (NUM_TRIGGERS * NUM_DAYS * COLOR_STRING_LENGTH) <= EEPROM_OFFSET_DISPLAY_BRIGHTNESS,
              "Farb-Block überschneidet sich mit Anzeigeparametern");
static_assert(EEPROM_OFFSET_CONFIG_VERSION >= EEPROM_OFFSET_TRIGGER_DELAY_MATRIX + EEPROM_TRIGGER_DELAY_MATRIX_SIZE,
              "Config version offset overlaps trigger delay matrix");
static_assert(EEPROM_OFFSET_COLOR_PALETTE_MASK_MATRIX + EEPROM_COLOR_PALETTE_MASK_MATRIX_SIZE <= EEPROM_SIZE,
              "Activity window exceeds allocated EEPROM size");
static_assert(EEPROM_OFFSET_WIFI_LOCAL_AP_PASSWORD + 50 <= EEPROM_SIZE,
              "WiFi network extension exceeds allocated EEPROM size");
static_assert(EEPROM_OFFSET_CUSTOM_SYMBOL_ENABLED + CUSTOM_SYMBOL_COUNT <= EEPROM_SIZE,
              "Custom symbol block exceeds allocated EEPROM size");

enum class WiFiOperationMode : uint8_t {
    TimedManager = 0,
    AlwaysConnected = 1,
    StaWithLocalAp = 2,
};

// **Standard-WiFi-Daten (werden bei Erststart gesetzt)**
extern char wifi_ssid[50];
extern char wifi_password[50];
extern char hostname[50];
extern int wifi_connect_timeout; // Timeout für die WLAN-Verbindung in Sekunden
extern uint8_t wifi_operation_mode;
extern bool wifi_status_symbol_enabled;
extern bool wifi_static_ip_enabled;
extern char wifi_static_ip[16];
extern char wifi_gateway[16];
extern char wifi_subnet[16];
extern char wifi_dns[16];
extern char wifi_local_ap_ssid[50];
extern char wifi_local_ap_password[50];
extern const char DEFAULT_INFRA_WIFI_SSID[];
extern const char DEFAULT_INFRA_WIFI_PASSWORD[];

// **Globale Variablen für die Anzeige**

extern Ticker display_ticker;
extern bool triggerActive;
extern unsigned long letterStartTime;
extern unsigned long wifiStartTime;

// **Zeichen/Symbole für Wochentage (Standardwerte)**
extern char dailyLetters[NUM_TRIGGERS][NUM_DAYS];

// **Zeichen-/Symbolfarben für die Wochentage (Standard: Weiß)**
extern char dailyLetterColors[NUM_TRIGGERS][NUM_DAYS][COLOR_STRING_LENGTH];
extern uint8_t dailyLetterColorModes[NUM_TRIGGERS][NUM_DAYS];
extern uint16_t dailyLetterRandomPaletteMasks[NUM_TRIGGERS][NUM_DAYS];
extern uint8_t customSymbolBitmaps[CUSTOM_SYMBOL_COUNT][SYMBOL_BITMAP_SIZE];
extern uint8_t customSymbolEnabled[CUSTOM_SYMBOL_COUNT];
extern uint8_t editableBuiltinSymbolBitmaps[EDITABLE_BUILTIN_SYMBOL_COUNT][SYMBOL_BITMAP_SIZE];
extern uint8_t editableBuiltinSymbolEnabled[EDITABLE_BUILTIN_SYMBOL_COUNT];

enum class LetterColorMode : uint8_t {
    Fixed = 0,
    RandomSelected = 1,
    RandomAll = 2,
};

extern const char *const randomColorPalette[RANDOM_COLOR_PALETTE_SIZE];
extern const char *const randomColorPaletteLabels[RANDOM_COLOR_PALETTE_SIZE];

// **Alle auswählbaren Zeichen/Symbole**
const char availableLetters[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '*', '#', '~', '&', '?', '0', '1', '2', '3', '4', '5', '6', '7'};

const char editableBuiltinSymbols[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '*', '#', '~', '&', '?'};

// **Konfiguration für Zeichen-/Symbolanzeige**
extern int display_brightness;           // Standard: 100
extern unsigned long letter_display_time;           // Standard: 10 Sekunden
extern unsigned long letter_trigger_delays[NUM_TRIGGERS][NUM_DAYS];
extern unsigned long letter_auto_display_interval; // Standard: 5 Minuten
extern uint16_t standalone_active_start_minutes;   // Minuten seit Mitternacht
extern uint16_t standalone_active_end_minutes;     // Minuten seit Mitternacht

// **Modus für Zeichen-/Symbolanzeige (Auto/Trigger)**
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


// **Attribut für Interrupt-Routinen im IRAM sicherstellen**
#if !defined(IRAM_ATTR)
#  if defined(ICACHE_RAM_ATTR)
#    define IRAM_ATTR ICACHE_RAM_ATTR
#  else
#    define IRAM_ATTR
#  endif
#endif

// **💾 Einstellungen speichern in EEPROM**
void saveConfig();

// **📂 Einstellungen aus EEPROM laden**
void loadConfig();

bool initEditableSymbolStore();
int editableBuiltinSymbolIndexFromChar(char symbol);
bool isEditableBuiltinSymbol(char symbol);
bool getDefaultBuiltinSymbolBitmap(char symbol, uint8_t *target);
bool getEditableBuiltinSymbolBitmap(char symbol, uint8_t *target);
bool saveEditableBuiltinSymbol(char symbol, const uint8_t *bitmap, bool enabled);
bool clearEditableBuiltinSymbol(char symbol);


void IRAM_ATTR display_updater();

// **LED-Matrix Setup-Funktion**
void setupMatrix();

void checkMemoryUsage();

#endif
