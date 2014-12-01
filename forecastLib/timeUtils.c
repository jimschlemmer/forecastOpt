
/*
 * File:   timeUtils.c
 * Author: root
 *
 * Created on September 15, 2006, 1:50 PM
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "timeUtils.h"
#ifndef True
#define True 1
#define False 0
#endif

                           // J   F   M   A  ....
static int TU_sumRegDays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

static int TU_sumLeapDays[] = {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335};

/*
 *
 */
int isLeap(int year)
{
  int isLeap = False;

  if (year % 4 == 0) {
    if(year % 100 == 0)
        isLeap = (year % 400 == 0);  // true for 2000
    else
        isLeap = True;  // true for 2004
  }

  return(isLeap);
}

time_t timeStr2gm(char *dateTime)
{
    // assume input looks like 20071010 12:15:00
    // delimeter between date and time and between
    // hour,min,sec can be anything
    // if sec is not given, it's taken to be zero

    // previously, I hadn't set these strings and I think it was a bug
    char yearStr[5]={0}, monthStr[3]={0}, mdayStr[3]={0}, hourStr[3]={0}, minStr[3]={0}, secStr[3]={0};

    if(strlen(dateTime) < 11) {
        fprintf(stderr, "timeStr2gm() bad input dateTime string: %s", dateTime);
        return 0;
    }

    strncpy(yearStr , dateTime,    4);
    strncpy(monthStr, dateTime+4,  2);
    strncpy(mdayStr , dateTime+6,  2);
    strncpy(hourStr , dateTime+9,  2);
    strncpy(minStr  , dateTime+12, 2);
    if(strlen(dateTime) > 16)
        strncpy(secStr  , dateTime+15, 2);
    else
        strcpy(secStr, "0");

    return timeGm(atoi(yearStr), atoi(monthStr), atoi(mdayStr), atoi(hourStr), atoi(minStr), atoi(secStr));
}
// timeGm -- emulate the Perl function of the same name, sorta
// this is similar to mktime() but that function forces local time
// year,month,day are 1-indexed, time is 00-indexed
//
// returns a time_t suitable for use in gmtime()
time_t timeGm(int year, int month, int mday, int hour, int min, int sec)
{
    int *days_per_mon;
    long jdays=0;
    time_t secs;

    if(isLeap(year))
        days_per_mon = TU_sumLeapDays;
    else
        days_per_mon = TU_sumRegDays;

    // first tally up the days so far this year
    jdays += days_per_mon[month-1] + mday - 1;

    // now we add in intervening days, going back to the epoch, 1/1/1970
    // we can calculate leap days incurred easily since 2000 was a leap year
    jdays += ((long) year - 1970) * 365;  /* days per year * years */
    jdays += ((long) year - 1969)/4;  /* leap days since 1970: 1999=7, 2000=7, 2001=8, etc. */

    secs = jdays * 86400;
    secs += hour*3600 + min*60 + sec;

    return(secs);
}

// much easier version of above function
time_t timeGmDoy(int year, int doy, double decHour)
{
    long jdays = doy-1;
    time_t secs;

    // now we add in intervening days, going back to the epoch, 1/1/1970
    // we can calculate leap days incurred easily since 2000 was a leap year
    jdays += ((long) year - 1970) * 365;  /* days per year * years */
    jdays += ((long) year - 1969)/4;  /* leap days since 1970: 1999=7, 2000=7, 2001=8, etc. */

    secs = jdays * 86400;
    secs += (decHour * 3600);

    return(secs);
}

char *timeOfDayStr(void)
{
    static char timeStr[256];
    struct tm *tm;
    time_t t = time(NULL);
    tm = gmtime(&t);
    // strftime(timeStr, 256, "%C", tm);

    // sprintf(str, "%d/%02d/%02d (%03d) %02d:%02d:00", dt->year, dt->month, dt->day, dt->doy, dt->hour, dt->min);

    strftime(timeStr, 256, "%Y-%m-%d %H:%M:%S", tm);
    return(timeStr);
}

// make this conform with dtToString
char *strfstr(time_t *time)
{
    static char timeStr[256];
    struct tm *tm;
    tm = gmtime(time);
    // strftime(timeStr, 256, "%C", tm);

    // sprintf(str, "%d/%02d/%02d (%03d) %02d:%02d:00", dt->year, dt->month, dt->day, dt->doy, dt->hour, dt->min);

    strftime(timeStr, 256, "%Y/%m/%d (%j) %H:%M:00", tm);
    return(timeStr);
}

char *utime2Csv(time_t *time)
{
    static char timeStr[256];
    struct tm *tm;
    tm = gmtime(time);
    // strftime(timeStr, 256, "%C", tm);

    // sprintf(str, "%d/%02d/%02d (%03d) %02d:%02d:00", dt->year, dt->month, dt->day, dt->doy, dt->hour, dt->min);

    // YYYY-MM-DD HH:MM:SS
    strftime(timeStr, 256, "%Y,%m,%d,%H,%M", tm);
    return(timeStr);
}

time_t sqlDateTime2utime(char *dateTime)
{
    // assume input looks like 2007-10-10 12:15:00
    // delimeter between date and time and between
    // hour,min,sec can be anything
    // if sec is not given, it's taken to be zero

    // previously, I hadn't set these strings and I think it was a bug
    char yearStr[5]={0}, monthStr[3]={0}, mdayStr[3]={0}, hourStr[3]={0}, minStr[3]={0}, secStr[3]={0};

    if(strlen(dateTime) < 11) {
        fprintf(stderr, "timeStr2gm() bad input dateTime string: %s", dateTime);
        return 0;
    }

    strncpy(yearStr , dateTime,    4);
    strncpy(monthStr, dateTime+5,  2);
    strncpy(mdayStr , dateTime+8,  2);
    strncpy(hourStr , dateTime+11,  2);
    strncpy(minStr  , dateTime+14, 2);
    if(strlen(dateTime) > 16)
        strncpy(secStr  , dateTime+15, 2);
    else
        strcpy(secStr, "0");

    return timeGm(atoi(yearStr), atoi(monthStr), atoi(mdayStr), atoi(hourStr), atoi(minStr), atoi(secStr));
}
char *utime2sqlDateTime(time_t *time)
{
    static char timeStr[256];
    struct tm *tm;
    tm = gmtime(time);
    // strftime(timeStr, 256, "%C", tm);

    // sprintf(str, "%d/%02d/%02d (%03d) %02d:%02d:00", dt->year, dt->month, dt->day, dt->doy, dt->hour, dt->min);

    // YYYY-MM-DD HH:MM:SS
    strftime(timeStr, 256, "%F %H:%M:00", tm);
    return(timeStr);
}

// jdays2secs(33244) == 680572800
time_t jdays2secs(double jday)
{
    if(jday > 30000)
        jday -= DAYS_1900_1970;
    return(jday * 86400);
}

// jdays2secs(33244) == 680572800
double secs2jdays(time_t secs)
{
    //return((double)secs/86400.0 + DAYS_1900_1970);
    return((double)secs/86400.0);
}


int getLocalDoy(int year, int doy, double hour, int timeZone)
{
    // if we're in a zone that passes 0 GMT in the course of daytime hours then we need to keep the local
    // doy for some purposes
    if(hour < -timeZone) {
        if(doy == 1) {
            if(isLeap(year))
                return(366);
            else
                return(365);
        }
        else
            return(doy-1);
    }

    return(doy);
}

int getLocalJday(int jday, double hour, int timeZone)
{
    // if we're in a zone that passes 0 GMT in the course of daytime hours then we need to keep the local
    // doy for some purposes
    if(hour < -timeZone) {
            return(jday-1);
    }

    return(jday);
}

//#define DO_MAIN
#ifdef DO_MAIN
int main (int argc, char *argv[])
{
    int i;
    for(i=1970; i< 2010; i++)
        printf("%d %s\n", i, isLeap(i) ? "leap" : " ");

    time_t secs = timeGm(2002, 3, 15, 15, 23, 45);

    // check against perl -e 'print scalar gmtime(1016205825), "\n";'

    printf("done\n");

}
#endif
