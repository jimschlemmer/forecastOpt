/*
 * File:   timeUtils.h
 * Author: root
 *
 * Created on September 15, 2006, 1:52 PM
 */

#ifndef _timeUtils_H
#define _timeUtils_H

#include <time.h>

#define DAYS_1900_1970  25568L  /* days from 1/1/00 to 1/1/1970  */

int isLeap(int year);
time_t timeGm(int year, int month, int mday, int hour, int min, int sec);
char *strfstr(time_t *time);
time_t jdays2secs(double jday);
time_t timeGmDoy(int year, int doy, double decHour);
double secs2jdays(time_t secs);
int getLocalDoy(int year, int doy, double hour, int timeZone);
int getLocalJday(int jday, double hour, int timeZone);
time_t timeStr2gm(char *dateTime);
char *utime2sqlDateTime(time_t *time);
time_t sqlDateTime2utime(char *dateTime);
char *timeOfDayStr(void);

#endif        /* _timeUtils_H */

