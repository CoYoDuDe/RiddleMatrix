#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include "config.h"

// **RTC aktivieren**
void enableRTC();

// **RS485 aktivieren**
void enableRS485();

// **RTC Zeit abrufen**
String getRTCTime();

int getRTCWeekday();

void setRTCFromWeb(String date, String time);


#endif
