#include "rtc_manager.h"

#include "config.h"

#include <time.h>

namespace {

volatile int cachedWeekday = -1;
volatile unsigned long cachedWeekdayTimestamp = 0;
volatile bool cachedWeekdayValid = false;

constexpr unsigned long WEEKDAY_CACHE_MAX_AGE_MS = 5000UL;
constexpr unsigned long SERIAL_IDLE_CHECK_DELAY_MS = 2UL;
constexpr unsigned long SERIAL_IDLE_MAX_WAIT_MS = 20UL;

void storeWeekdayInCache(int weekday) {
  if (weekday >= 0 && weekday < static_cast<int>(NUM_DAYS)) {
    cachedWeekday = weekday;
    cachedWeekdayTimestamp = millis();
    cachedWeekdayValid = true;
  } else {
    cachedWeekdayValid = false;
  }
}

} // namespace

void enableRTC() {
  digitalWrite(GPIO_RS485_ENABLE, HIGH);
  Serial.flush();
  Serial.end();
  delay(40);
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(40);
}

void enableRS485() {
  delay(40);
  Serial.begin(19200);
  digitalWrite(GPIO_RS485_ENABLE, LOW);
}

bool isWeekdayCacheValid() {
  return cachedWeekdayValid;
}

int getCachedWeekday() {
  return cachedWeekdayValid ? cachedWeekday : -1;
}

void invalidateWeekdayCache() {
  cachedWeekdayValid = false;
}

bool updateCachedWeekday(bool waitForIdle) {
  if (!rtc_ok) {
    cachedWeekdayValid = false;
    return false;
  }

  const unsigned long now = millis();
  const bool cacheFresh =
      cachedWeekdayValid && (now - cachedWeekdayTimestamp) < WEEKDAY_CACHE_MAX_AGE_MS;
  if (cacheFresh) {
    return true;
  }

  if (Serial.available() > 0) {
    delay(SERIAL_IDLE_CHECK_DELAY_MS);

    if (waitForIdle) {
      const unsigned long waitStart = millis();
      while (Serial.available() > 0 && (millis() - waitStart) < SERIAL_IDLE_MAX_WAIT_MS) {
        delay(SERIAL_IDLE_CHECK_DELAY_MS);
      }
    }

    if (Serial.available() > 0) {
      if (!cacheFresh) {
        cachedWeekdayValid = false;
      }
      return cachedWeekdayValid;
    }
  }

  enableRTC();
  DateTime nowRtc = rtc.now();
  enableRS485();

  storeWeekdayInCache(nowRtc.dayOfTheWeek());
  return cachedWeekdayValid;
}

String getRTCTime() {
    enableRTC();
    DateTime now = rtc.now();
    enableRS485();

    storeWeekdayInCache(now.dayOfTheWeek());

    char buffer[40];
    snprintf(buffer, sizeof(buffer), "%s, %04d-%02d-%02d %02d:%02d:%02d",
             daysOfTheWeek[now.dayOfTheWeek()], now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return String(buffer);
}

int getRTCWeekday() {
    if (!updateCachedWeekday(true) && !isWeekdayCacheValid()) {
        return -1;
    }

    return getCachedWeekday();
}

bool setRTCFromWeb(const String &date, const String &time) {
    enableRTC();

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    const int parsedDate = sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day);
    const int parsedTime = sscanf(time.c_str(), "%d:%d:%d", &hour, &minute, &second);

    if (parsedDate != 3 || parsedTime != 3) {
        Serial.println(F("❌ Fehler: Ungültiges Datums- oder Zeitformat übermittelt."));
        enableRS485();
        return false;
    }

    auto isLeapYear = [](int y) -> bool {
        return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
    };

    auto daysInMonth = [&](int y, int m) -> int {
        if (m < 1 || m > 12) {
            return 0;
        }

        const int daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        int days = daysPerMonth[m - 1];
        if (m == 2 && isLeapYear(y)) {
            days = 29;
        }
        return days;
    };

    if (year < 2000 || year > 2099) {
        Serial.println(F("❌ Fehler: Jahr außerhalb des gültigen Bereichs (2000-2099)."));
        enableRS485();
        return false;
    }

    const int maxDay = daysInMonth(year, month);
    if (maxDay == 0 || day < 1 || day > maxDay) {
        Serial.println(F("❌ Fehler: Ungültiges Datum übermittelt."));
        enableRS485();
        return false;
    }

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        Serial.println(F("❌ Fehler: Ungültige Uhrzeit übermittelt."));
        enableRS485();
        return false;
    }

    const DateTime newDateTime(year, month, day, hour, minute, second);
    rtc.adjust(newDateTime);
    storeWeekdayInCache(newDateTime.dayOfTheWeek());
    enableRS485();
    Serial.println(F("🕒 RTC wurde aktualisiert!"));
    return true;
}

bool syncTimeWithNTP() {
    Serial.println(F("🔄 Synchronisiere Zeit mit NTP..."));
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10000)) {
        Serial.println(F("❌ NTP Zeit konnte nicht abgerufen werden!"));
        return false;
    }

    enableRTC();
    const DateTime ntpDateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                               timeinfo.tm_mday, timeinfo.tm_hour,
                               timeinfo.tm_min, timeinfo.tm_sec);
    rtc.adjust(ntpDateTime);
    storeWeekdayInCache(ntpDateTime.dayOfTheWeek());
    enableRS485();
    Serial.println(F("✅ NTP Synchronisierung erfolgreich!"));
    return true;
}
