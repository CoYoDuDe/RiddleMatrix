#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include <Arduino.h>

void enableRTC();
void enableRS485();
void initializeTimezone();
bool updateCachedWeekday(bool waitForIdle = false);
bool isWeekdayCacheValid();
int getCachedWeekday();
void invalidateWeekdayCache();
bool getRTCMinutesOfDay(uint16_t &minutesOfDay);
String getRTCTime();
int getRTCWeekday();
bool setRTCFromWeb(const String &date, const String &time);
bool syncTimeWithNTP();

#endif
