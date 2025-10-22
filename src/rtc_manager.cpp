#include "rtc_manager.h"

#include "config.h"

#include <time.h>

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

String getRTCTime() {
    enableRTC();
    DateTime now = rtc.now();
    enableRS485();

    char buffer[40];
    snprintf(buffer, sizeof(buffer), "%s, %04d-%02d-%02d %02d:%02d:%02d",
             daysOfTheWeek[now.dayOfTheWeek()], now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return String(buffer);
}

int getRTCWeekday() {
    enableRTC();
    DateTime now = rtc.now();
    enableRS485();
    return now.dayOfTheWeek();
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

    rtc.adjust(DateTime(year, month, day, hour, minute, second));
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
    rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                        timeinfo.tm_mday, timeinfo.tm_hour,
                        timeinfo.tm_min, timeinfo.tm_sec));
    enableRS485();
    Serial.println(F("✅ NTP Synchronisierung erfolgreich!"));
    return true;
}
