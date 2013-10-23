/* 
 * File:   forecastOpt.h
 * Author: jimschlemmer
 *
 * Created on July 30, 2013, 11:24 AM
 */

#ifndef FORECASTOPT_H
#define	FORECASTOPT_H

#include <time.h>
#include <malloc.h>
#include <math.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dbi/dbi.h>
#include <errno.h>

#include "gridDateTime.h"
#include "ioUtils.h"
#include "timeUtils.h"

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	__cplusplus
}
#endif

#define FatalError(function, message) fatalError(function, message, __FILE__, __LINE__)

#define MAX_MODELS 16
#define MAX_SITES 16
#define MAX_HOURLY_SLOTS 64
#define MIN_GHI_VAL 5

typedef enum { MKunset, regular } modelKindType;

typedef struct {
    int columnInfoIndex;
    char *columnName;  // short cut to columnName below
    double sumModel_Ground, sumAbs_Model_Ground, sumModel_Ground_2;
    double mbe, mae, rmse; 
    double mbePct, maePct, rmsePct;
    double weight;  // used for current calculation
    double optimizedWeightPass1;  // the value associated with the minimized RMSE for all models in pass 1
    double optimizedWeightPass2;  // the value associated with the minimized RMSE for all models in pass 2
    int N;
    char isActive, isReference, isUsable;
    long long powerOfTen;
} modelStatsType;

typedef struct {
    int hoursAhead;
    int numValidSamples;
    int ground_N;
    double meanMeasuredGHI;
    modelStatsType satModelError;
    modelStatsType modelError[MAX_MODELS];
    modelStatsType weightedModelError;
    double optimizedRMSEphase1;
    double optimizedRMSEphase2;
    long phase1RMSEcalls, phase2RMSEcalls;
} modelErrorType;

typedef struct {
    double modelGHI[MAX_MODELS];
} hourGroupType;

typedef struct {
    dateTimeType dateTime;
    double zenith, groundGHI, groundDNI, clearskyGHI, groundDiffuse, groundTemp, groundWind, groundRH, satGHI, weightedModelGHI;   // this is the ground data
    hourGroupType hourGroup[MAX_HOURLY_SLOTS];
    char isValid, sunIsUp;
} timeSeriesType;

typedef struct {
    char *columnName;
    char *columnDescription;
    int  inputColumnNumber;
    int  maxHourAhead;
    int  hourGroupIndex;
    int  modelIndex;
} columnType;

// this is for site-specific forecast model on/off settings
typedef struct {
    int numModels;
    char *siteName;
    char *modelNames[MAX_MODELS];
    int maxHoursAhead[MAX_MODELS];
} siteType;

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
    char verbose;
    char multipleSites;
    double weightSumLowCutoff, weightSumHighCutoff;
    int startHourLowIndex, startHourHighIndex;
    char warningsFileName[2048];
    FILE *warningsFp;
    int numSites;
    siteType allSiteInfo[MAX_SITES];
    siteType *thisSite; // points to one of the above registered sites
    int numInputRecords;
    int numDaylightRecords;
} forecastInputType;


int doErrorAnalysis(forecastInputType *fci, int hourIndex);
char *getGenericModelName(forecastInputType *fci, int modelIndex);
int getMaxHoursAhead(forecastInputType *fci, int modelIndex);
void runOptimizer(forecastInputType *fci, int hourIndex);
int filterHourlyModelData(forecastInputType *fci, int hourIndex);
void clearHourlyErrorFields(forecastInputType *fci, int hourIndex);
int computeHourlyDifferences(forecastInputType *fci, int hourIndex);
int computeHourlyBiasErrors(forecastInputType *fci, int hourIndex);
int computeHourlyRmseErrors(forecastInputType *fci, int hourIndex);
int computeHourlyRmseErrorWeighted(forecastInputType *fci, int hourIndex);
void fatalError(char *functName, char *errStr, char *file, int linenumber);
void fatalErrorWithExitCode(char *functName, char *errStr, char *file, int linenumber, int exitCode);
int runOptimizerNested(forecastInputType *fci, int hourIndex);
char *getElapsedTime(time_t start_t);
void printHourlySummary(forecastInputType *fci, int hourIndex);
void printSummaryCsv(forecastInputType *fci);

#endif	/* FORECASTOPT_H */

