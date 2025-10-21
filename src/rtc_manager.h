#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include <Arduino.h>

void enableRTC();
void enableRS485();
String getRTCTime();
int getRTCWeekday();
void setRTCFromWeb(String date, String time);
void syncTimeWithNTP();

#endif
