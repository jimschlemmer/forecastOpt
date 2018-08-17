/* File:   forecastOpt.h
 * Author: jimschlemmer
 *
 * Created on July 30, 2013, 11:24 AM
 */

#ifndef FORECASTOPT_H
#define FORECASTOPT_H

#define NO_DUMP 0
#define DO_DUMP 1

#include <time.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
extern int errno;

#include "gridDateTime.h"
#include "gridded.h"
#include "ioUtils.h"
#include "timeUtils.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#ifndef MIN
#define MIN(x, y)       ((x) < (y) ? (x) : (y))
#define MAX(x, y)       ((x) > (y) ? (x) : (y))
#endif

#define FatalError(function, message) fatalError(function, message, __FILE__, __LINE__)

#define MAX_MODELS 10
#define MAX_SITES 500
#define MAX_HOURS_AHEAD 48
#define MAX_HOURS_AFTER_SUNRISE 16
#define MIN_GHI_VAL 5
#define MAX_KT_BINS 10

// file-level character reading limits
#define MAX_FIELDS 64
#define LINE_LENGTH 1024  

// header strings for output .csv files that possibly need to be scanned for later
#define WEIGHT_1_STR "weight 1"
#define WEIGHT_2_STR "weight 2"

#define MIN_IRR -25
#define MAX_IRR 1500

#define USE_GROUND_REF 1
#define USE_SATELLITE_REF 0

//#define isContributingModel(model) (model.isActive && !model.isReference)

typedef enum {
    MKunset, regular
} modelKindType;

typedef enum {  // OK is valid; others are rejection codes
    zenith, groundLow, satLow, nwpLow, notHAS, notKt, OK
} validType ;

typedef struct {
    char *modelName; // short cut to modelName below
    double sumModel_Ground, sumAbs_Model_Ground, sumModel_Ground_2;
    double sumModel_Sat, sumAbs_Model_Sat, sumModel_Sat_2;
    double mbe, mae, rmse;
    double mbePct, maePct, rmsePct;
    int weight; // used for current calculation
    int optimizedWeightPhase1; // the value associated with the minimized RMSE for all models in pass 1
    int optimizedWeightPhase2; // the value associated with the minimized RMSE for all models in pass 2
    int N;
    char isActive, maskSwitchOn, isReference, tooMuchDataMissing, isContributingModel;
    long long powerOfTen;
} modelStatsType;

typedef struct {
    int hoursAhead, hoursAfterSunrise; // both or just hoursAhead can be active, depending on run mode 
    int numValidSamples;
    int ground_N;
    double meanMeasuredGHI;
    modelStatsType satModelStats;
    modelStatsType hourlyModelStats[MAX_MODELS];
    modelStatsType weightedModelStatsVsGround;
    modelStatsType weightedModelStatsVsSat;
    double optimizedRMSEphase1, optimizedPctRMSEphase1;
    double optimizedRMSEphase2, optimizedPctRMSEphase2;
    long phase1RMSEcalls, phase2RMSEcalls, phase1SumWeightsCalls, phase2SumWeightsCalls;
    double correctionVarA, correctionVarB;
} modelRunType;

typedef struct {
    double modelGHI[MAX_MODELS];
    validType groupIsValid;
    double  ktSatGHI, // used for a correction scheme
            ktTargetNWP, // this is the kt that's computed as, for example, ECMWF/CLR in the first leg
            ktV4, // this is the kt that computed from a non-KTI first leg, i.e., just like old v4 -- not currently used
            ktOpt, // this is the kt that computed as the 2nd of the ktTargetNWP run, = optimizedGHI1/CLR
            optimizedGHI1, // this is the optimized GHI from the 1st leg
            optimizedGHI2, // this is the optimized GHI from the 2nd leg
            correctedOptimizedGHI;
    int ktIndexNWP, ktIndexOpt;
} forecastDataType;

// for each HA there is an NWP input file with the time series data
typedef struct {
    dateTimeType dateTime;
    double zenith, groundGHI, groundDNI, clearskyGHI, groundDiffuse, groundTemp, groundWind, groundRH, satGHI;
    forecastDataType forecastData[MAX_HOURS_AHEAD];  // for a given dateTime we will have many HAs
    char sunIsUp;
    //dateTimeType sunrise;
    int hoursAfterSunrise; 
    char *siteName; // useful for multiple site runs
} timeSeriesType;

typedef struct {
    char *modelName;
    char *modelDescription;
    int inputColumnNumber;
    int numGood, numMissing;
    double percentMissing;
    int maxhoursAhead;
    int hoursAheadIndex;
    int modelIndex;
} modelType;

// this data structure describes a single T/S file with surface, v3, NWP and clearsky data
// we'll have one of these for each such file, so it's number of NWPs * number of HAs.

typedef struct {
    modelType modelInfo;
    int hoursAhead, hoursAheadIndex;
    timeSeriesType *timeSeries;
    int numTimeSeriesSamples;
    int numGood, numMissing;
    double percentMissing;
} nwpTimeSeriesType;

// this is for site-specific forecast model on/off settings

typedef struct {
    int numModels;
    char *siteName;
    double lat, lon;
    char *modelNames[MAX_MODELS];
    int maxHoursAhead[MAX_MODELS];
} siteType;

typedef struct {
    char *fileName;
    FILE *fp;
    int lineNumber;
    char *headerLine; // not used by all
} fileType;

typedef struct {
    int numPermutations; // e.g., 32 for numModels==5
    int masks[MAX_MODELS]; // 0001 0010 0100 1000, etc.  
    int currentPermutationIndex; // one of switchIndexes
    int modelSwitches[MAX_MODELS]; // = mask[i] applied to currentPermutationIndex
} permutationType;

typedef struct {
    fileType forecastTableFile;
    fileType warningsFile;
    fileType configFile;
    fileType weightTableFile;
    fileType modelsAttendenceFile;
    fileType summaryFile;
    char *modelMixDirectory;
    fileType modelMixFileOutput;
    fileType modelMixFileInput;
    fileType optimizedTSFile;
    fileType correctionStatsFile;
    modelType modelInfo[MAX_MODELS * MAX_HOURS_AHEAD];
    modelRunType hoursAheadGroup[MAX_HOURS_AHEAD];
    //modelRunType hoursAfterSunriseGroup[MAX_HOURS_AHEAD][MAX_HOURS_AFTER_SUNRISE][MAX_KT_BINS];
    modelRunType ***hoursAfterSunriseGroup;
    int numKtBins;
    int numColumnInfoEntries;
    int numModelsRegistered;
    int numModels;
    int numContribModels;
    int numHeaderFields;
    int numDivisions;
    int increment1, increment2, refinementBase;
    int weightSumLowCutoff, weightSumHighCutoff;
    int inPhase1;
    int maxModelIndex;
    int numTotalSamples;
    int siteIndex;
    char *inputDirectory;
    fileType inputFiles[1024];
    int numInputFiles;
    char *outputDirectory;
    int groundGHICol, groundDNICol, satGHICol, clearskyGHICol, ktModelColumn, startModelsColumnNumber;
    char *ktModelColumnName;
    dateTimeType startDate, endDate;
    char *startDateStr, *endDateStr;
    char verbose;
    char filterWithSatModel;
    char multipleSites;
    int startHourLowIndex, startHourHighIndex;
    int numSites;
    timeSeriesType *timeSeries;
    // each model and time horizon now has it's own time series
    // fci->nwpTimeSeries[hoursAheadIndex].
    // nwpTimeSeriesType nwpTimeSeries[MAX_HOURS_AHEAD]; // something like 7 * 200
    siteType allSiteInfo[MAX_SITES];
    siteType *thisSite; // points to one of the above registered sites
    int numInputRecords;
    int numDaylightRecords;
    char runWeightedErrorAnalysis;
    char forecastHeaderLine1[LINE_LENGTH], forecastHeaderLine2[LINE_LENGTH];
    char *delimiter;
    int forecastLineNumber;
    char runOptimizer, skipPhase2;
    char runHoursAfterSunrise;
    char useSatelliteDataAsRef;
    int maxHoursAfterSunrise;
    int maxHoursAheadIndex;
    char gotConfigFile, gotForecastFile;
    char doModelPermutations;
    permutationType modelPermutations;
    char doKtNWP, doKtOpt, doKtBootstrap, inKtBootstrap, doKtAndNonKt;
} forecastInputType;

char *getModelName(forecastInputType *fci, int modelIndex);
int getMaxHoursAhead(forecastInputType *fci, int modelIndex);
void runOptimizer(forecastInputType *fci, int hourIndex);
int computeHourlyDifferences(forecastInputType *fci, int hourIndex);
int computeHourlyBiasErrors(forecastInputType *fci, int hourIndex, int hoursAfterSunriseIndex, int ktIndex);
int computeHourlyRmseErrors(forecastInputType *fci, int hourIndex, int hoursAfterSunriseIndex, int ktIndex);
int computeModelRMSE(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex);
int computeHourlyRmseErrorWeighted(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex,  int ktIndex, int useGroundReference);
int dumpModelMixRMSE(forecastInputType *fci, int hoursAheadIndex);
void fatalError(char *functName, char *errStr, char *file, int linenumber);
void fatalErrorWithExitCode(char *functName, char *errStr, char *file, int linenumber, int exitCode);
int runOptimizerNested(forecastInputType *fci, int hourIndex, int hoursAfterSunriseIndex, int ktIndex);
char *getElapsedTime(time_t start_t);
void printHoursAheadSummaryCsv(forecastInputType *fci);
void dumpNumModelsReportingTable(forecastInputType *fci);
char *genProxySiteName(forecastInputType *fci);
int readModelMixFile(forecastInputType *fci);
int dumpHourlyOptimizedTS(forecastInputType *fci, int hoursAheadIndex);
void genPermutationMatrix(forecastInputType *fci);
void initPermutationSwitches(forecastInputType *fci);
void setPermutationSwitches(forecastInputType *fci, int permutationIndex);
int isContributingModel(modelStatsType *model);
void setModelSwitches(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int kitIndex, int permutationIndex);
timeSeriesType *getNextTimeSeriesSample(forecastInputType *fci, int hoursAheadIndex);
void printHourlySummary(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex);
void setKtIndex(forecastInputType *fci, timeSeriesType *thisTS, int hoursAheadIndex);
int optimizeGHI(forecastInputType *fci, int hoursAheadIndex);

#endif /* FORECASTOPT_H */

