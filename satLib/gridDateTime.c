/* 
 * File:   gridDateTime.c
 * Author: root
 *
 * Created on September 17, 2006, 12:33 PM
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <errno.h>
extern int errno;
char ErrStr[4096];

#include "gridDateTime.h"
#include "timeUtils.h"
#include "ioUtils.h"
#include "sunae.h"

/*
 * 
 */
int getGridDateTime(gridDataType *grid, dateTimeType *dt, float timeAdjustMinutes)
 {
    // don't want to change the adjusted grid time as we may need that later, as
    // in the case of shifted ratios in shiftGrids()

    // adjust the obs_time to be on the even hour
    // in the case of afg-pak, it is -7 minutes
    time_t adj_obs = grid->hed.obs_time + (time_t) ((timeAdjustMinutes * 60));  // timeAdjust is additive in the model  
    dt->obs_time = adj_obs;
    
    setDateTimeFromObs(dt);
    
    return True;
 }

int setDateTimeFromObs(dateTimeType *dt)
{
    struct tm *tm;
    // adjust the obs_time to be on the even hour
    // in the case of afg-pak, it is -7 minutes
    tm = gmtime(&dt->obs_time);        
    
    dt->year = tm->tm_year + 1900;
    dt->month = tm->tm_mon + 1;
    dt->day = tm->tm_mday;
    dt->hour = tm->tm_hour;
    dt->min = tm->tm_min; // add this back in -- it should have been taken out during the satModel run
    dt->doy = tm->tm_yday + 1;
    dt->julianDay = (int) ((float)dt->obs_time/86400.0);
    
    return True;
}

void resetDateTimeFromObsTime(dateTimeType *dt, time_t obs_time)
{
    dt->obs_time = obs_time;
    setDateTimeFromObs(dt);
}

// also sets doy
int setObsTime(dateTimeType *dt)
{
    if(!dateTimeSanityCheck(dt))
        return False;

    dt->obs_time = timeGm(dt->year, dt->month, dt->day, dt->hour, dt->min, 0);

    setDateTimeFromObs(dt);

    return True;
}


 // just clear all dt fields
void initDateTime(dateTimeType *dt) 
{
    dt->year = -1;
    dt->month = -1;
    dt->day = -1;
    dt->hour = -1;
    dt->min = -1;
    dt->obs_time = -1;
    dt->doy = -1;
    dt->julianDay = -1;
}
 
// if(isDateTimeSet(curr))
int isDateTimeSet(dateTimeType *dt) 
{
    return(dt->year != -1);
}

int newMonth(dateTimeType *prev, dateTimeType *curr) 
{
    return(prev->month != curr->month);
}
  
// flag curr grids as being off from what we expect from the previous grid
int checkTimeSequence(dateTimeType *prev, dateTimeType *curr)
{
    // if prev is unset, its the first sample
    // otherwise, if the day of month is different, don't compare
    if(prev->year == -1 || prev->day != curr->day) {
        return(curr->min);  // catch the situation where an 8:30 file is first
    }
    
    // examples => 7:00 8:30    7:00 9:00   6:00 8:30
    //               30             60         90
    if(curr->hour - prev->hour != 1 || curr->min != 0)
        return ((curr->hour - prev->hour - 1) * 60 + curr->min);
        
    return 0;
}

// just transfer the field values of curr to prev
void updateDateTime(dateTimeType *to, dateTimeType *from) 
{
    to->obs_time = from->obs_time;
    setDateTimeFromObs(to);
}

// dt2 - dt1
float diffMinutes(dateTimeType *dt1, dateTimeType *dt2) 
{
    float minutes;
    
    minutes = (dt2->obs_time - dt1->obs_time)/60.0;

    return(minutes);
}

float diffHours(dateTimeType *dt1, dateTimeType *dt2)
{
    float hours;
    
    hours = (dt2->obs_time - dt1->obs_time)/3600.0;
    
    return(hours);
}

float diffDays(dateTimeType *dt1, dateTimeType *dt2)
{
    float days;
    
    days = (dt2->obs_time - dt1->obs_time)/86400.0;
    
    return(days);
}

//  aug 8, 2008 -- this is the way I should have been doing these inc/decrement
//  functions all along -- just add or subtract seconds from obs_time and reset everything based
//  on that.  Simpler and less error prone.  Duh!
int incrementNumMinutes(dateTimeType *dt, float minutes)
{
    dt->obs_time += (time_t) (minutes * 60);
    setDateTimeFromObs(dt);
    
    return True;
}

int incrementMinute(dateTimeType *dt)
{
    dt->obs_time += 60;
    setDateTimeFromObs(dt);
    
    return True;
}

int decrementMinute(dateTimeType *dt)
{
    dt->obs_time -= 60;
    setDateTimeFromObs(dt);
    
    return True;
}

int incrementNumHours(dateTimeType *dt, float hrs)
{
    dt->obs_time += (time_t) (3600 * hrs);
    setDateTimeFromObs(dt);
    
    return True;
}

int incrementHour(dateTimeType *dt)
{
    dt->obs_time += 3600;
    setDateTimeFromObs(dt);
    
    return True;
}

int decrementHour(dateTimeType *dt)
{
    dt->obs_time -= 3600;
    setDateTimeFromObs(dt);
    
    return True;
}

int incrementNumDays(dateTimeType *dt, float days)
{
    dt->obs_time += (time_t) (86400 * days);
    setDateTimeFromObs(dt);
    
    return True;
}

int incrementDay(dateTimeType *dt)
{
    dt->obs_time += 86400;
    setDateTimeFromObs(dt);
    
    return True;
}

int decrementDay(dateTimeType *dt)
{
    dt->obs_time -= 86400;
    setDateTimeFromObs(dt);
    
    return True;
}

// Warning: this can cause an illegal day of month
int incrementMonth(dateTimeType *dt)
{
    dt->month++;
    
    if(dt->month > 12) {
        dt->month = 1;
        dt->year++;
    }
    
    setObsTime(dt);   
    return(True);
}

// Warning: this can cause an illegal day of month
int decrementMonth(dateTimeType *dt)
{
    dt->month--;
    
    if(dt->month < 1) {
        dt->month = 12;
        dt->year--;
    }
    
    setObsTime(dt);   
    return(True);
}

// Warning: this can cause a shift in month/day
int incrementYear(dateTimeType *dt)
{
    dt->year++;
    setObsTime(dt);   
    return(True);
}

// Warning: this can cause a shift in month/day
int decrementYear(dateTimeType *dt)
{
    dt->year++;
    setObsTime(dt);   
    return(True);
}

// set dt to midnight, Jan 1, year
void setToJan1(dateTimeType *dt, int year)
{
    setDateTime(dt, year, 1, 1, 0, 0);
}


void setDateTime(dateTimeType *dt, int year, int month, int mday, int hour, int min)
{
    dt->year = year;
    dt->month = month;
    dt->day = mday;
    dt->hour = hour;
    dt->min = min;
    
    setObsTime(dt);   
}

// convert strings of the form 200301012315 to dt->year=2003, dt->month=01, dt->day=01 dt->hour=23, dt->min=15
int stringToDt(dateTimeType *dt, char *dateStr)
{
    sscanf(dateStr, "%04d%02d%02d%02d%02d", &dt->year, &dt->month, &dt->day, &dt->hour, &dt->min);   
    return setObsTime(dt);
}

char *dtToStringDateTime(dateTimeType *dt)
{
    static char str[256];
    
    sprintf(str, "%d/%02d/%02d %02d:%02d:00", dt->year, dt->month, dt->day, dt->hour, dt->min);
    return str;
}

char *dtToString(dateTimeType *dt)
{
    static char str[256];
    
    sprintf(str, "%d/%02d/%02d (%03d) %02d:%02d:00 (jday=%d)", dt->year, dt->month, dt->day, dt->doy, dt->hour, dt->min, dt->julianDay);
    return str;
}

char *dtToString2(dateTimeType *dt)
{
    static char str[256];
    
    sprintf(str, "%d/%02d/%02d %02d:%02d:00,%03d,%ld", dt->year, dt->month, dt->day, dt->hour, dt->min, dt->doy, dt->obs_time);
    return str;
}


char *dtToStringCsv(dateTimeType *dt)
{
    static char str[256];
    
    sprintf(str, "%d,%02d,%02d,%03d,%.2f", dt->year, dt->month, dt->day, dt->doy, (float) dt->hour + ((float) dt->min)/60.0);
    return str;
}

char *dtToStringCsv2(dateTimeType *dt)
{
    static char str[256];
    
    sprintf(str, "%d,%02d,%02d,%02d,%02d", dt->year, dt->month, dt->day, dt->hour, dt->min);
    return str;
}

char *dtToStringFilename(dateTimeType *dt)
{
    static char str[256];
    
    sprintf(str, "%d%02d%02d.%02d%02d00", dt->year, dt->month, dt->day, dt->hour, dt->min);
    return str;
}

// now we just have a couple of functions to write ascii dates to and read from 
// files.
int saveDateToFile(dateTimeType *dt, char *fileName)
{
    // just write date as 200610251700
    FILE *fp;

    // check sanity
    if(!dateTimeSanityCheck(dt)) {
        fprintf(stderr, "saveDateToFile(): aborting save to %s.\n", fileName);
        return(False);
    }
    
    // open file
    if((fp = fopen(fileName, "w")) == NULL) {            
        sprintf(ErrStr, "saveDateToFile(): Couldn't open file %s to write.\n", fileName);
        perror(ErrStr);
        return(False);
    }
    
    // write and close
    fprintf(fp, "%4d%02d%02d%02d%02d", dt->year, dt->month, dt->day, dt->hour, dt->min);
    fclose(fp);
    
    return(True);
}

int loadDateFromFile(dateTimeType *dt, char *fileName)
{
    // just write date as 200610251700
    FILE *fp;
    
    if((fp = fopen(fileName, "r")) == NULL) {            
        sprintf(ErrStr, "loadDateFromFile(): Couldn't open file %s to read.\n", fileName);
        perror(ErrStr);
        
        if(errno == ENOENT) { // if file doesn't exist, check that it's toucheable
                if((fp = fopen(fileName, "w")) == NULL) {
                    sprintf(ErrStr, "loadDateFromFile(): File %s not writeable either.\n", fileName);
                    perror(ErrStr);
                    return(False);
                }
                fclose(fp);
                // set dt to the epoch, just for laughs
                setDateTime(dt, 1970, 1, 1, 0, 0);
                return(True);
        }
        
        return(False);  // otherwise it was something else
    }
    
    if(fileSize(fp) == 0) {
        setDateTime(dt, 1970, 1, 1, 0, 0);
        fprintf(stderr, "Note: loadDateFromFile(): date file %s is empty.  Using earliest possible date.\n", fileName);
        return(True);
    }
    
    fscanf(fp, "%4d%02d%02d%02d%02d", &dt->year, &dt->month, &dt->day, &dt->hour, &dt->min);    
    setObsTime(dt);
    fclose(fp);
    
    if(!dateTimeSanityCheck(dt)) {
        fprintf(stderr, "loadDateFromFile(): aborting load from %s.\n", fileName);
        return(False);
    }

    return(True);
}

int dateTimeSanityCheck(dateTimeType *dt)
{
        // sanity checks
    if(dt->year < 1970 || dt->year > 2050) {  // I'll be 88 by then
        fprintf(stderr, "dateTimeSanityCheck(): bad year in date : %d\n", dt->year);
        return(False);
    }
    if(dt->month < 1 || dt->month > 12) {  
        fprintf(stderr, "dateTimeSanityCheck(): bad month in date : %d\n", dt->month);
        return(False);
    }
    if(dt->day < 1 || dt->day > 31) {  
        fprintf(stderr, "dateTimeSanityCheck(): bad day in date : %d\n", dt->day);
        return(False);
    }
    if(dt->hour < 0 || dt->hour > 23) {  
        fprintf(stderr, "dateTimeSanityCheck(): bad hour in date : %d\n", dt->hour);
        return(False);
    }
    if(dt->min < 0 || dt->min > 59) {  
        fprintf(stderr, "dateTimeSanityCheck(): bad minute in date : %d\n", dt->min);
        return(False);
    }
    
    return(True);
}

// archiver args look like -a [startdate],[enddate] 
// -a 200610271700,200610301700 
// -a 200610271700 
// -a ,200610301700
int parseArchDates(dateTimeType *dts, dateTimeType *dte, char *optarg)
{
    char *end, *start;
    char backup[256];
//    dateTimeType *dts, *dte;
    
    start = end = optarg;
    strncpy(backup, optarg, 256);

//    dts = &sat->archiverStartDate;
//    dte = &sat->archiverEndDate;
    
    dte->hour = dte->min = dts->hour = dts->min = 0;  // in case we get a short date

    while(*end && *end != ',')
        end++;

    if(*end) {
        *end = '\0';
        end++;
    }
                  // yyyymmddhhmm
    if(*start) {
        sscanf(start, "%04d%02d%02d%02d%02d", &dts->year, &dts->month, &dts->day, &dts->hour, &dts->min);   
        setObsTime(dts);  // calculate time_t and doy numbers
        
        if(!dateTimeSanityCheck(dts)) {
            fprintf(stderr, "parseArchDates(): error in archiver start date %s in argument string %s\n", start, backup);
            return(False);
        }
    }
    if(*end) {
        sscanf(end  , "%4d%02d%02d%02d%02d", &dte->year, &dte->month, &dte->day, &dte->hour, &dte->min);    
        setObsTime(dte);
            
        if(!dateTimeSanityCheck(dte)) {
            fprintf(stderr, "parseArchDates(): error in archiver end date %s in argument string %s\n", end, backup);
            return(False);
        }
    }
    
    return True;
}

int sunIsUpInEntireRegion(double ullat, double ullon, double lrlat, double lrlon, dateTimeType *dt)
{
    int isUp;
    ae_pack ul, ur, lr, ll;

    ul.lat = ur.lat = ullat;
    ul.lon = ll.lon = ullon;
    lr.lat = ll.lat = lrlat;
    ur.lon = lr.lon = lrlon;

    isUp = sunIsUpPoint(&ul, dt) &&
           sunIsUpPoint(&ur, dt) &&
           sunIsUpPoint(&lr, dt) &&
           sunIsUpPoint(&ll, dt);

    return(isUp);
}

int sunIsUpInRegion(double ullat, double ullon, double lrlat, double lrlon, dateTimeType *dt)
{
    int isUp;
    ae_pack ul, ur, lr, ll;

    ul.lat = ur.lat = ullat;
    ul.lon = ll.lon = ullon;
    lr.lat = ll.lat = lrlat;
    ur.lon = lr.lon = lrlon;

    isUp = sunIsUpPoint(&ul, dt) ||
           sunIsUpPoint(&ur, dt) ||
           sunIsUpPoint(&lr, dt) ||
           sunIsUpPoint(&ll, dt);

    return(isUp);
}

//#define DEBUG_SUNUP
int sunIsUpPoint(ae_pack *aep, dateTimeType *dt)
{
    //struct tm       *tm;

    //tm = gmtime(&dt->obs_time);
    aep->year = dt->year; //tm->tm_year+1900;
    aep->doy = dt->doy;// tm->tm_yday+1;
    aep->hour = dt->hour + (dt->min/60.0);
    sunae(aep);
#ifdef DEBUG_SUNUP
    fprintf(stderr, "[%s] lat=%.3f lon=%.3f el=%.3f\n", dtToStringDateTime(dt), aep->lat, aep->lon, aep->el);
#endif
    return(aep->el > 0);
}

// Iterative approach to calculating sunrise time.
// Looked into codes on-line but they were either too complex
// or, in the case of the Naval Observatory's pseudo code, too
// ambiguous (in terms of radian to degree conversions).  With the below
// code, I use a tried and true ephemeris calculator (sunae) along with 
// an iterative method to home in on the correct hour/minute of sunrise.
dateTimeType calculateSunrise(dateTimeType *dt, double lat, double lon)
{
    dateTimeType sunrise = *dt;
    ae_pack aep;
    double sunUpHour, sunUpMinute;
    int hour, minute;
    char sunIsUp;
    
    aep.lat = lat;
    aep.lon = lon;
    aep.year = dt->year; //tm->tm_year+1900;
    aep.doy = dt->doy;// tm->tm_yday+1;
    aep.hour = -1;  // go back to previous day's hour 23 in case sunrise is at GMT midnight which is likely to be somewhere in Asia.
    sunae(&aep);
    sunIsUp = (aep.el > 0);
   
    for(hour=0; hour<24; hour++) {   
       aep.hour = hour;
       sunae(&aep);
       //fprintf(stderr, "[hour %d] lat=%.3f lon=%.3f el=%.3f zen=%.3f\n", hour, aep.lat, aep.lon, aep.el, aep.zen);
       if(!sunIsUp && aep.el > 0) { // sun just rose
           sunUpHour = hour - 1;
           break;
       }
       sunIsUp = (aep.el > 0);
    }
    
    aep.hour = sunUpHour - 1/60;  // go back a minute in case the sunrise is at the top of the hour
    sunae(&aep);
    sunIsUp = (aep.el > 0);
    for(minute=0; minute<60; minute++) {   
       aep.hour = sunUpHour + minute/60.0;
       sunae(&aep);
       //fprintf(stderr, "[hour %.0f minute %d] lat=%.3f lon=%.3f el=%.3f zen=%.3f\n", aep.hour, minute, aep.lat, aep.lon, aep.el, aep.zen);
       if(!sunIsUp && aep.el > 0) {   // sun just rose
           sunUpMinute = minute - 1;  // not doing seconds so it really doesn't matter
           break;
       }
       sunIsUp = (aep.el > 0);
    }
    
    sunrise.hour = sunUpHour;
    sunrise.min = sunUpMinute;
    setObsTime(&sunrise);  // make sure obs_time (utime) is set)
    
    return sunrise;
}
