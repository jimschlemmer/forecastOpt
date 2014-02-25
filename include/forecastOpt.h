/* 
 * File:   forecastOpt.h
 * Author: jimschlemmer
 *
 * Created on July 30, 2013, 11:24 AM
 */

#ifndef FORECASTOPT_H
#define	FORECASTOPT_H

#include <time.h>
#include <ctype.h>
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
extern int errno;

#include "gridDateTime.h"
#include "ioUtils.h"
#include "timeUtils.h"

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	__cplusplus
}
#endif

#ifndef MIN
#define MIN(x, y)       ((x) < (y) ? (x) : (y))
#define MAX(x, y)       ((x) > (y) ? (x) : (y))
#endif

#define FatalError(function, message) fatalError(function, message, __FILE__, __LINE__)

#define MAX_MODELS 16
#define MAX_SITES 16
#define MAX_HOURS_AHEAD 64
#define MIN_GHI_VAL 5
#define MAX_HOURS_AFTER_SUNRISE 16

// header strings for output .csv files that possibly need to be scanned for later
#define WEIGHT_1_STR "weight 1"
#define WEIGHT_2_STR "weight 2"

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
    char isActive, isReference, isUsable, missingData;
    long long powerOfTen;
} modelStatsType;

typedef struct {
    int hoursAhead, hoursAfterSunrise;  // both or just hoursAhead can be active, depending on run mode 
    int numValidSamples;
    int ground_N;
    double meanMeasuredGHI;
    modelStatsType satModelStats;
    modelStatsType hourlyModelStats[MAX_MODELS];
    modelStatsType weightedModelStats;
    double optimizedRMSEphase1;
    double optimizedRMSEphase2;
    long phase1RMSEcalls, phase2RMSEcalls;
} modelRunType;

typedef struct {
    double modelGHI[MAX_MODELS];
} modelDataType;

typedef struct {
    dateTimeType dateTime;
    double zenith, groundGHI, groundDNI, clearskyGHI, groundDiffuse, groundTemp, groundWind, groundRH, satGHI, weightedModelGHI;   // this is the ground data
    modelDataType forecastData[MAX_HOURS_AHEAD];
    char isValid, sunIsUp;
    dateTimeType sunrise;
    int hoursAfterSunrise;
} timeSeriesType;

typedef struct {
    char *columnName;
    char *columnDescription;
    int  inputColumnNumber;
    int  maxhoursAhead;
    int  hoursAheadIndex;
    int  modelIndex;
    int numGood, numMissing;
    double percentMissing;
} columnType;

// this is for site-specific forecast model on/off settings
typedef struct {
    int numModels;
    char *siteName;
    char *modelNames[MAX_MODELS];
    int maxHoursAhead[MAX_MODELS];
} siteType;

typedef struct {
    char *fileName;
    FILE *fp;
    int  lineNumber;
    char *headerLine;  // not used by all
} fileType;

typedef struct {
    fileType forecastTableFile;
    fileType warningsFile;
    fileType descriptionFile;
    fileType modelsAttendenceFile;
    fileType summaryFile;
    columnType columnInfo[MAX_MODELS * MAX_HOURS_AHEAD];
    modelRunType hoursAheadGroup[MAX_HOURS_AHEAD];
    modelRunType hoursAfterSunriseGroup[MAX_HOURS_AFTER_SUNRISE][MAX_HOURS_AFTER_SUNRISE];
    int hoursAheadMap[MAX_HOURS_AHEAD];
    int numColumnInfoEntries;
    int numModels;
    int maxModelIndex;
    int numTotalSamples;
    timeSeriesType *timeSeries;
    char *siteGroup;
    char *siteName;
    char *outputDirectory;
    double lat, lon;
    int zenithCol, groundGHICol, groundDNICol, groundDiffuseCol, groundTempCol, groundWindCol, satGHICol, clearskyGHICol, startModelsColumnNumber;
    dateTimeType startDate, endDate;
    char verbose;
    char filterWithSatModel;
    char multipleSites;
    double weightSumLowCutoff, weightSumHighCutoff;
    int startHourLowIndex, startHourHighIndex;
    int numSites;
    siteType allSiteInfo[MAX_SITES];
    siteType *thisSite; // points to one of the above registered sites
    int numInputRecords;
    int numDaylightRecords;
    char runWeightedErrorAnalysis;
    char *forecastHeaderLine;
    int forecastLineNumber;
    char runOptimizer, skipPhase2;
    char runHoursAfterSunrise;
    int maxHoursAfterSunrise;
    char timeSpanStr[256];
} forecastInputType;

int doErrorAnalysis(forecastInputType *fci, int hourIndex, int hoursAfterSunriseIndex);
char *getGenericModelName(forecastInputType *fci, int modelIndex);
int getMaxHoursAhead(forecastInputType *fci, int modelIndex);
void runOptimizer(forecastInputType *fci, int hourIndex);
int filterHourlyForecastData(forecastInputType *fci, int hourIndex, int hoursAfterSunriseIndex);
void clearHourlyErrorFields(forecastInputType *fci, int hourIndex, int hoursAfterSunriseIndex);
int computeHourlyDifferences(forecastInputType *fci, int hourIndex);
int computeHourlyBiasErrors(forecastInputType *fci, int hourIndex, int hoursAfterSunriseIndex);
int computeHourlyRmseErrors(forecastInputType *fci, int hourIndex, int hoursAfterSunriseIndex);
int computeHourlyRmseErrorWeighted(forecastInputType *fci, int hourIndex, int hoursAfterSunriseIndex);
int dumpModelMix(forecastInputType *fci, int hoursAheadIndex);
void fatalError(char *functName, char *errStr, char *file, int linenumber);
void fatalErrorWithExitCode(char *functName, char *errStr, char *file, int linenumber, int exitCode);
int runOptimizerNested(forecastInputType *fci, int hourIndex, int hoursAfterSunriseIndex);
char *getElapsedTime(time_t start_t);
void printHourlySummary(forecastInputType *fci, int hourIndex, int hoursAfterSunriseIndex);
void printHoursAheadSummaryCsv(forecastInputType *fci);
void dumpNumModelsReportingTable(forecastInputType *fci);

#endif	/* FORECASTOPT_H */

