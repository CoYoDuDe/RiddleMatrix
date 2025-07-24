#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include "config.h"

// **RTC aktivieren**
void enableRTC() {
  digitalWrite(GPIO_RS485_ENABLE, HIGH);
  Serial.flush();
  Serial.end();
  delay(40);
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(40);
}

// **RS485 aktivieren**
void enableRS485() {
  delay(40);
  Serial.begin(19200);
  digitalWrite(GPIO_RS485_ENABLE, LOW);
}

// **RTC Zeit abrufen**
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

void setRTCFromWeb(String date, String time) {
    enableRTC();
    int year, month, day, hour, minute, second;
    sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day);
    sscanf(time.c_str(), "%d:%d:%d", &hour, &minute, &second);
    rtc.adjust(DateTime(year, month, day, hour, minute, second));
    enableRS485();
    Serial.println(F("🕒 RTC wurde aktualisiert!"));
}

void resetRTC() {
    enableRTC();
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    enableRS485();
    Serial.println(F("🔄 RTC wurde auf das aktuelle Datum/Zeit gesetzt!"));
}

#endif