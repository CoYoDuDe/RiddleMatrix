#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include <Arduino.h>

void enableRTC();
void enableRS485();
String getRTCTime();
int getRTCWeekday();
bool setRTCFromWeb(const String &date, const String &time);
void syncTimeWithNTP();

#endif
