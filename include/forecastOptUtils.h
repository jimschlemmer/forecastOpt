/* 
 * File:   forecastOptUtils.h
 * Author: jimschlemmer
 *
 * Created on August 6, 2014, 5:53 PM
 */

#ifndef FORECASTOPTUTILS_H
#define	FORECASTOPTUTILS_H

#ifdef	__cplusplus
extern "C" {
#endif




#ifdef	__cplusplus
}
#endif

#endif	/* FORECASTOPTUTILS_H */

#define IsReference 1
#define IsNotReference 0
#define IsForecast 1
#define IsNotForecast 0

void initForecastInfo(forecastInputType *fci);
void incrementTimeSeries(forecastInputType *fci);
int  readForecastData(forecastInputType *fci);
int  readDataFromLine(forecastInputType *fci, int hoursAheadIndex, timeSeriesType *thisSample, char *fields[], int numFields);
int  parseDateTime(forecastInputType *fci, dateTimeType *dt, char **fields, int numFields);
int  parseHourIndexes(forecastInputType *fci, char *optarg);
int  parseArgs(forecastInputType *fci, int argC, char **argV);
void printByHour(forecastInputType *fci);
void printByModel(forecastInputType *fci);
void printByAnalysisType(forecastInputType *fci);
char *getColumnNameByHourModel(forecastInputType *fci, int hrInd, int modInd);
FILE *openErrorTypeFile(forecastInputType *fci, char *fileNameStr);
void printRmseTableHour(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex);
FILE *openErrorTypeFileHourly(forecastInputType *fci, char *analysisType, int hoursAheadIndex , int hoursAfterSunriseIndex, int ktIndex);
void registerSiteModel(siteType *si, char *modelName, int maxHoursAhead);
void setSite(forecastInputType *fci);
int  checkModelAgainstSite(forecastInputType *fci, char *modelName);
void setSiteInfo(forecastInputType *fci, char *line, double lat, double lon, int hoursAhead);
void dumpWeightedTimeSeries(forecastInputType *fci,int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex);
int runWeightedTimeSeriesAnalysis(forecastInputType *fci);
int readSummaryFile(forecastInputType *fci);
void studyData(forecastInputType *fci);
char *stripQuotes(char *str);
void stripComment(char *str);
int parseNumberFromString(char *str);
void copyHoursAfterData(forecastInputType *fci);
void dumpHoursAfterSunrise(forecastInputType *fci);
void dumpModelMix_EachModel_HAxHAS(forecastInputType *fci);
void dumpModelMix_EachHAS_HAxModel(forecastInputType *fci);
int getHoursAheadIndex(forecastInputType *fci, int hoursAhead);
int getHoursAfterSunriseIndex(forecastInputType *fci, int hoursAfterSunrise);
//int getModelIndex(forecastInputType *fci, char *modelName);
void parseNwpHeaderLine(forecastInputType *fci, char *filename);
int getModelIndex(forecastInputType *fci, char *modelName, int *modelIndex);
timeSeriesType *allocTimeSeriesSample(forecastInputType *fci, int hoursAheadIndex);
timeSeriesType *findTimeSeriesSample(forecastInputType *fci, dateTimeType *dt);
char *validString(validType code);
