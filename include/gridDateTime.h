/* 
 * File:   gredDateTime.h
 * Author: root
 *
 * Created on September 17, 2006, 12:34 PM
 */

/* data structure to track time/date changes in grid headers */

#ifndef _gridDateTime_H
#define        _gridDateTime_H

#include "sunae.h"
#include "gridded.h"  // need def of satelliteType

// typedef
typedef struct {
    int year, month, day, hour, min, doy, julianDay;
    time_t obs_time;
} dateTimeType;

// protos  
void initDateTime(dateTimeType *dt);
int newMonth(dateTimeType *prev, dateTimeType *curr);
int checkTimeSequence(dateTimeType *prev, dateTimeType *curr);
void updateDateTime(dateTimeType *prev, dateTimeType *curr);
int isDateTimeSet(dateTimeType *dt);
char *dtToString(dateTimeType *dt);
char *dtToStringCsv(dateTimeType *dt);
char *dtToStringCsv2(dateTimeType *dt);
char *dtToStringDateTime(dateTimeType *dt);
char *dtToString2(dateTimeType *dt);
char *dtToStringFilename(dateTimeType *dt);
float diffMinutes(dateTimeType *dt1, dateTimeType *dt2);
float diffHours(dateTimeType *dt1, dateTimeType *dt2);
float diffDays(dateTimeType *dt1, dateTimeType *dt2);
int incrementMinute(dateTimeType *dt);
int incrementHour(dateTimeType *dt);
int incrementDay(dateTimeType *dt);
int incrementMonth(dateTimeType *dt);
int incrementYear(dateTimeType *dt);
int decrementMinute(dateTimeType *dt);
int decrementHour(dateTimeType *dt);
int decrementDay(dateTimeType *dt);
int decrementMonth(dateTimeType *dt);
int decrementYear(dateTimeType *dt);
int incrementNumMinutes(dateTimeType *dt, float minutes);
int incrementNumHours(dateTimeType *dt, float hours);
int incrementNumDays(dateTimeType *dt, float days);
int getGridDateTime(gridDataType *grid, dateTimeType *dt, float time_adj);
int setObsTime(dateTimeType *dt);
void setToJan1(dateTimeType *dt, int year);
void setDateTime(dateTimeType *dt, int year, int month, int mday, int hour, int min);
int dateTimeSanityCheck(dateTimeType *dt);
int loadDateFromFile(dateTimeType *dt, char *fileName);
int saveDateToFile(dateTimeType *dt, char *fileName);
int setDateTimeFromObs(dateTimeType *dt);
int parseArchDates(dateTimeType *dts, dateTimeType *dte, char *optarg);
int sunIsUpInEntireRegion(double ullat, double ullon, double lrlat, double lrlon, dateTimeType *dt);
int sunIsUpInRegion(double ullat, double ullon, double lrlat, double lrlon, dateTimeType *dt);
int sunIsUpPoint(ae_pack *aep, dateTimeType *dt);
int stringToDt(dateTimeType *dt, char *dateStr);
void resetDateTimeFromObsTime(dateTimeType *dt, time_t obs_time);

#endif        /* _gredDateTime_H */

