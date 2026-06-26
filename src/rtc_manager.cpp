#include "rtc_manager.h"

#include "config.h"

#include <sys/time.h>
#include <time.h>

namespace {

volatile int cachedWeekday = -1;
volatile unsigned long cachedWeekdayTimestamp = 0;
volatile bool cachedWeekdayValid = false;

constexpr unsigned long WEEKDAY_CACHE_MAX_AGE_MS = 5000UL;
constexpr unsigned long SERIAL_IDLE_CHECK_DELAY_MS = 2UL;
constexpr unsigned long SERIAL_IDLE_MAX_WAIT_MS = 20UL;
constexpr const char *NTP_TIMEZONE_EUROPE_BERLIN = "CET-1CEST,M3.5.0,M10.5.0/3";

void storeWeekdayInCache(int weekday) {
  if (weekday >= 0 && weekday < static_cast<int>(NUM_DAYS)) {
    cachedWeekday = weekday;
    cachedWeekdayTimestamp = millis();
    cachedWeekdayValid = true;
  } else {
    cachedWeekdayValid = false;
  }
}

bool getSystemLocalTime(struct tm &timeinfo, uint32_t timeoutMs = 0) {
  if (!getLocalTime(&timeinfo, timeoutMs)) {
    return false;
  }
  return (timeinfo.tm_year + 1900) >= 2020;
}

bool setSystemLocalTime(int year, int month, int day, int hour, int minute, int second) {
  struct tm localTime = {};
  localTime.tm_year = year - 1900;
  localTime.tm_mon = month - 1;
  localTime.tm_mday = day;
  localTime.tm_hour = hour;
  localTime.tm_min = minute;
  localTime.tm_sec = second;
  localTime.tm_isdst = -1;

  const time_t epoch = mktime(&localTime);
  if (epoch <= 0) {
    return false;
  }

  timeval now = {};
  now.tv_sec = epoch;
  now.tv_usec = 0;
  if (settimeofday(&now, nullptr) != 0) {
    return false;
  }

  storeWeekdayInCache(localTime.tm_wday);
  return true;
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
    struct tm timeinfo = {};
    if (getSystemLocalTime(timeinfo)) {
      storeWeekdayInCache(timeinfo.tm_wday);
      return true;
    }
    cachedWeekdayValid = false;
    return false;
  }

  const unsigned long now = millis();
  if (cachedWeekdayValid && (now - cachedWeekdayTimestamp) < WEEKDAY_CACHE_MAX_AGE_MS) {
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
      return cachedWeekdayValid;
    }
  }

  enableRTC();
  DateTime nowRtc = rtc.now();
  enableRS485();

  storeWeekdayInCache(nowRtc.dayOfTheWeek());
  return cachedWeekdayValid;
}

bool getRTCMinutesOfDay(uint16_t &minutesOfDay) {
  if (!rtc_ok) {
    struct tm timeinfo = {};
    if (!getSystemLocalTime(timeinfo)) {
      return false;
    }
    storeWeekdayInCache(timeinfo.tm_wday);
    minutesOfDay = static_cast<uint16_t>((timeinfo.tm_hour * 60) + timeinfo.tm_min);
    return true;
  }

  enableRTC();
  DateTime now = rtc.now();
  enableRS485();

  storeWeekdayInCache(now.dayOfTheWeek());
  minutesOfDay = static_cast<uint16_t>((now.hour() * 60) + now.minute());
  return true;
}

String getRTCTime() {
    if (!rtc_ok) {
        struct tm timeinfo = {};
        if (!getSystemLocalTime(timeinfo)) {
            return String(F("Keine gueltige Zeit verfuegbar"));
        }

        storeWeekdayInCache(timeinfo.tm_wday);
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%s, %04d-%02d-%02d %02d:%02d:%02d (System/NTP)",
                 daysOfTheWeek[timeinfo.tm_wday], timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return String(buffer);
    }

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
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    const int parsedDate = sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day);
    const int parsedTime = sscanf(time.c_str(), "%d:%d:%d", &hour, &minute, &second);

    if (parsedDate != 3 || (parsedTime != 2 && parsedTime != 3)) {
        Serial.println(F("Fehler: Ungueltiges Datums- oder Zeitformat uebermittelt."));
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
        Serial.println(F("Fehler: Jahr ausserhalb des gueltigen Bereichs (2000-2099)."));
        return false;
    }

    const int maxDay = daysInMonth(year, month);
    if (maxDay == 0 || day < 1 || day > maxDay) {
        Serial.println(F("Fehler: Ungueltiges Datum uebermittelt."));
        return false;
    }

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        Serial.println(F("Fehler: Ungueltige Uhrzeit uebermittelt."));
        return false;
    }

    const DateTime newDateTime(year, month, day, hour, minute, second);
    if (!setSystemLocalTime(year, month, day, hour, minute, second)) {
        Serial.println(F("Fehler: Systemzeit konnte nicht gesetzt werden."));
        return false;
    }

    if (rtc_ok) {
        enableRTC();
        rtc.adjust(newDateTime);
        storeWeekdayInCache(newDateTime.dayOfTheWeek());
        enableRS485();
        Serial.println(F("RTC und Systemzeit wurden aktualisiert."));
    } else {
        Serial.println(F("Systemzeit wurde aktualisiert; keine RTC zum Speichern verfuegbar."));
    }
    return true;
}
bool syncTimeWithNTP() {
    Serial.println(F("Synchronisiere Zeit mit NTP..."));
    configTzTime(NTP_TIMEZONE_EUROPE_BERLIN, "pool.ntp.org", "time.nist.gov");

    struct tm timeinfo;
    if (!getSystemLocalTime(timeinfo, 10000)) {
        Serial.println(F("NTP Zeit konnte nicht abgerufen werden!"));
        return false;
    }

    storeWeekdayInCache(timeinfo.tm_wday);

    if (!rtc_ok) {
        Serial.println(F("NTP Zeit als Systemzeit aktiv; keine RTC zum Speichern verfuegbar."));
        return true;
    }

    enableRTC();
    const DateTime ntpDateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                               timeinfo.tm_mday, timeinfo.tm_hour,
                               timeinfo.tm_min, timeinfo.tm_sec);
    rtc.adjust(ntpDateTime);
    storeWeekdayInCache(ntpDateTime.dayOfTheWeek());
    enableRS485();
    Serial.println(F("NTP Synchronisierung erfolgreich!"));
    return true;
}

