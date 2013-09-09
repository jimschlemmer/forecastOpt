/* 
 * File:   forecastOpt.h
 * Author: jimschlemmer
 *
 * Created on July 30, 2013, 11:24 AM
 */

#ifndef FORECASTOPT_H
#define	FORECASTOPT_H

#include <math.h>
#include "gridDateTime.h"
#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	__cplusplus
}
#endif

#define MAX_MODELS 16
#define MAX_HOURLY_SLOTS 64
#define MIN_GHI_VAL 5

typedef enum { MKunset, regular } modelKindType;

typedef struct {
    int columnInfoIndex;
    char *columnName;  // short cut to columnName below
    double sumModel_Ground, sumAbs_Model_Ground, sumModel_Ground_2;
    double mbe, mae, rmse; 
    double mbePct, maePct, rmsePct;
    double weight;
    int N;
    int isActive;
    int powerOfTen;
} modelStatsType;

typedef struct {
    int hoursAhead;
    int numValidSamples;
    int ground_N;
    double meanMeasuredGHI;
    modelStatsType satModelError;
    modelStatsType modelError[MAX_MODELS];
} modelErrorType;

typedef struct {
    double modelGHI[MAX_MODELS];
} hourGroupType;

typedef struct {
    dateTimeType dateTime;
    double zenith, groundGHI, groundDNI, clearskyGHI, groundDiffuse, groundTemp, groundWind, groundRH, satGHI;   // this is the ground data
    hourGroupType hourGroup[MAX_HOURLY_SLOTS];
    int isValid;
} timeSeriesType;

typedef struct {
    char *columnName;
    char *columnDescription;
    int  inputColumnNumber;
    int  maxHourAhead;
    int  hourGroupIndex;
    int  modelIndex;
} columnType;

typedef struct {
    columnType columnInfo[MAX_MODELS * MAX_HOURLY_SLOTS];
    modelErrorType hourErrorGroup[MAX_HOURLY_SLOTS];
    int numColumnInfoEntries;
    int numModels;
    int numHourGroups;
    int numTotalSamples;
    timeSeriesType *timeSeries;
    char *siteGroup;
    char *siteName;
    char *outputDirectory;
    double lat, lon;
    int zenithCol, groundGHICol, groundDNICol, groundDiffuseCol, groundTempCol, groundWindCol, satGHICol, clearskyGHICol, startModelsColumnNumber;
    dateTimeType startDate, endDate;
} forecastInputType;

int doErrorAnalysis(forecastInputType *fci, int hourIndex);
char *getGenericModelName(forecastInputType *fci, int modelIndex);
int getMaxHoursAhead(forecastInputType *fci, int modelIndex);
void runOptimizer(forecastInputType *fci, int hourIndex);
int filterHourlyModelData(forecastInputType *fci, int hourIndex);
void clearHourlyErrorFields(forecastInputType *fci, int hourIndex);
int computeHourlyDifferences(forecastInputType *fci, int hourIndex);

#endif	/* FORECASTOPT_H */

