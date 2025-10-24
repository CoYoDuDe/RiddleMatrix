#ifndef RTCLIB_H
#define RTCLIB_H

class DateTime {};

class RTC_DS1307 {
  public:
    bool begin() { return true; }
    bool isrunning() { return true; }
    DateTime now() { return DateTime{}; }
};

#endif
