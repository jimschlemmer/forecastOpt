/* 
 * File:   forecastOpt.h
 * Author: jimschlemmer
 *
 * Created on July 30, 2013, 11:24 AM
 */

#ifndef FORECASTOPT_H
#define	FORECASTOPT_H

#include "gridDateTime.h"
#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	__cplusplus
}
#endif

#define MAX_MODELS 16

typedef enum { MKunset, regular } modelKindType;

typedef struct {
    double sumModel_Ground, sumAbs_Model_Ground, sumModel_Ground_2;
    double mbe, mae, rmse; 
    double mbePct, maePct, rmsePct;
} modelErrorType;

typedef struct {
    char *modelName;
    modelKindType modelKind;
    modelErrorType modelError;
} modelType;

typedef struct {
    dateTimeType dateTime;
    char   isValid;
    double zenith, groundGHI, groundDNI, clearskyGHI, groundDiffuse, groundTemp, groundWind, groundRH;   // this is the ground data
    double modelGHIvalues[MAX_MODELS];
} timeSeriesType;

typedef struct {
    char *columnName;
    char *columnDescription;
    int  inputColumnNumber;
} columnType;

typedef struct {
    columnType readColumns[64];
    int numReadColumns;
    int numModels;
    modelType models[MAX_MODELS];
    int numSamples;
    int numValidSamples;
    double meanMeasuredGHI;
    timeSeriesType *timeSeries;
    char *siteGroup;
    char *siteName;
    double lat, lon;
    int zenithCol, groundGHICol, groundDNICol, groundDiffuseCol, groundTempCol, groundWindCol, satGHICol, clearskyGHICol, startModelsColumnNumber;
} forecastInputType;

int doErrorAnalysis(forecastInputType *fci);

#endif	/* FORECASTOPT_H */

