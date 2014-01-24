#include "forecastOpt.h"

/*
STEP 2  Error calculation

    MeanMeasured GHI = (Sum (GHIground) ) / N            with N being the number of good (filtered) points
    MBE = (Sum (GHImod-GHIground) ) / N
    MAE = (Sum (abs(GHImod-GHIground)) ) / N
    RMSE =  ((sum((GHImod-GHIground)^2))/N)^(1/2)
    MBE% = MBE/MeanMeasured
    MAE% = MAE/MeanMeasured
    RMSE% = RMSE/MeanMeasured
'};
*/

//#define DEBUG

char ErrStr[2048];

// satellite GHI is treated as a quasi-model; it's run through the error codes but
// it's not a forecast entity.
#define getModelGHI(modelIndex) (modelIndex < 0 ? thisSample->satGHI : thisSample->modelData[hourIndex].modelGHI[modelIndex])
#define incrementModelN() (modelIndex < 0 ? (modelData->satModelError.N++) : (modelData->modelError[modelIndex].N++))

#define DEBUGHOUR 1

void clearModelStats(modelStatsType *thisModelErr);

int doErrorAnalysis(forecastInputType *fci, int hourIndex)
{
    clearHourlyErrorFields(fci, hourIndex);   

    if(!filterHourlyModelData(fci, hourIndex))
        return False;    
    if(!computeHourlyBiasErrors(fci, hourIndex))
        return False;
    if(!computeHourlyRmseErrors(fci, hourIndex))
        return False;
    
    return True;
}



void clearHourlyErrorFields(forecastInputType *fci, int hourIndex)
{
    modelErrorType *modelData = &fci->hourErrorGroup[hourIndex];
    int modelIndex;
    

    //fprintf(stderr, "Clearing stats fields for hour %d\n", hourIndex);
    // zero out all statistical values
    clearModelStats(&modelData->satModelError);
    clearModelStats(&modelData->weightedModelError);
    
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) 
        clearModelStats(&modelData->modelError[modelIndex]);
}

void clearModelStats(modelStatsType *thisModelErr)
{
    thisModelErr->sumModel_Ground = thisModelErr->sumAbs_Model_Ground = thisModelErr->sumModel_Ground_2 = 0;
    thisModelErr->mae = thisModelErr->mbe = thisModelErr->rmse = 0;  
    thisModelErr->maePct = thisModelErr->mbePct = thisModelErr->rmsePct = 0;  
    thisModelErr->N = 0; 
}


#define WRITE_FILTERED_DATA
int filterHourlyModelData(forecastInputType *fci, int hourIndex)
{
    int sampleInd, modelIndex;
    double thisGHI;
    timeSeriesType *thisSample;
    modelErrorType *modelData = &fci->hourErrorGroup[hourIndex];
    int hoursAhead = fci->hourErrorGroup[hourIndex].hoursAhead;
   
    // reset a few variables
    modelData->meanMeasuredGHI = modelData->numValidSamples = modelData->ground_N = 0;
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) 
        modelData->modelError[modelIndex].N = 0;

    for(sampleInd=0; sampleInd<fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        thisSample->isValid = True;

        // for the sat model and each forecast model, filter date/times based on all values
        for(modelIndex=-1; modelIndex < fci->numModels; modelIndex++) {
            if(modelIndex < 0 || modelData->modelError[modelIndex].isActive) { // isActive because we want forecast and reference models
                thisGHI = (modelIndex < 0 ? thisSample->satGHI : thisSample->modelData[hourIndex].modelGHI[modelIndex]);
                if(thisGHI < 5) {
//#ifdef DEBUG1
                    if(thisSample->sunIsUp) 
                        fprintf(fci->warningsFile.fp, "%s : bad sample: model %s, modelIndex = %d, hoursAheadIndex = %d : GHI = %.1f, zenith = %.1f\n", 
                            dtToStringCsv2(&thisSample->dateTime), getGenericModelName(fci, modelIndex), modelIndex, modelData->hoursAhead, thisGHI, thisSample->zenith);
//#endif                
                    thisSample->isValid = False;
                    // break;
                }     
                else { // if this model's GHI didn't trigger an isValid=False, increment N
#ifdef DEBUG1
                    fprintf(stderr, "%s hours ahead %d: good sample: model %s: GHI = %.1f\n", 
                         dtToStringCsv2(&thisSample->dateTime), modelData->hoursAhead, getGenericModelName(fci, modelIndex), thisGHI);
#endif                                   
                    if(modelIndex < 0)
                        modelData->satModelError.N++;
                    else
                        modelData->modelError[modelIndex].N++;
                }
            }
        }
        
        if(thisSample->groundGHI < MIN_GHI_VAL ) {
            thisSample->isValid = False;
        }
        else {
            modelData->ground_N++;
        }
        
        if(thisSample->isValid) {
            modelData->meanMeasuredGHI += thisSample->groundGHI;
            modelData->numValidSamples++;
#ifdef DEBUG1
            fprintf(stderr, "%s hours ahead: %d:  all good: meanMeasuredGHI=%.1f numValidSamples=%d\n", 
                 dtToStringCsv2(&thisSample->dateTime), modelData->hoursAhead, modelData->meanMeasuredGHI, modelData->numValidSamples);
#endif
        }
    }
    
    if(modelData->numValidSamples < 1) {
        fprintf(fci->warningsFile.fp, "doErrorAnalysis(): for hour index %d (%d hours ahead): got too few valid points to work with\n", hourIndex, modelData->hoursAhead);
        //FatalError("doErrorAnalysis()", "Too few valid data points to work with.");
        return False;
    }
    
#ifdef DEBUG
    if(hoursAhead == DEBUGHOUR) {
        fprintf(stderr, "DBDUMP:year,month,day,hour,min,groupIsValid,groundValid,satValid,validModels..\n");
        int count = 0;
        for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
            thisSample = &fci->timeSeries[sampleInd];
#ifdef YESNO
            fprintf(stderr, "DBDUMP:%s,%d,%s,%s,%s", dtToStringCsv2(&thisSample->dateTime), count, thisSample->isValid ? "yes" : "no", 
                    thisSample->groundGHI > MIN_GHI_VAL ? "yes" : "no", thisSample->satGHI > MIN_GHI_VAL ? "yes" : "no");
                for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                    if(modelData->modelError[modelIndex].isActive) {
                        thisGHI = getModelGHI(modelIndex);
                        if(thisGHI > 5) {
                            fprintf(stderr, ",yes");
                        }
                        else {
                            fprintf(stderr, ",no [%s=%.1f]", getGenericModelName(fci, modelIndex), thisGHI);
                        }
                    }
                }
#else
            fprintf(stderr, "DBDUMP:%s,%d,%s,%.0f,%.0f", dtToStringCsv2(&thisSample->dateTime), count, thisSample->isValid ? "yes" : "no", 
                    thisSample->groundGHI , thisSample->satGHI );
            for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                if(modelData->modelError[modelIndex].isActive) {
                    fprintf(stderr, ",%.0f", getModelGHI(modelIndex));                    
                }
            }            
#endif            
            if(thisSample->isValid)
                count++;
            fprintf(stderr, "\n");
        }
    }
#endif
    
#ifdef WRITE_FILTERED_DATA
    FILE *fp;
    char filename[1024];
    // open the file
    sprintf(filename, "%s.filteredData.hoursAhead=%d.csv", fci->siteName, hoursAhead);
    if((fp = fopen(filename, "w")) == NULL) {
        fprintf(stderr, "Couldn't open file %s\n", filename);
        exit(1);
    }
    // print the header
    fprintf(fp, "#year,month,day,hour,min,lineNum,groupIsValid,groundGHI,satGHI");
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        if(modelData->modelError[modelIndex].isActive) {
            fprintf(fp, ",%s", getGenericModelName(fci, modelIndex));
        }
    }
    fprintf(fp, "\n");
    // print the data
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
            thisSample = &fci->timeSeries[sampleInd];
            fprintf(fp, "%s,%d,%s,%.0f,%.0f", dtToStringCsv2(&thisSample->dateTime), sampleInd, thisSample->isValid ? "yes" : "no", 
                    thisSample->groundGHI , thisSample->satGHI );
            for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                if(modelData->modelError[modelIndex].isActive) {
                    fprintf(fp, ",%.0f", thisSample->modelData[hourIndex].modelGHI[modelIndex]);                    
                }
            } 
            fprintf(fp, "\n");
    }
    fclose(fp);
#endif
    
    modelData->meanMeasuredGHI /= modelData->numValidSamples;

    return True;    
}

    /*
    STEP 2  Error calculation

    MeanMeasured GHI = (Sum (GHIground) ) / N            with N being the number of good (filtered) points
    MBE = (Sum (GHImod-GHIground) ) / N
    MAE = (Sum (abs(GHImod-GHIground)) ) / N
    RMSE =  ((sum((GHImod-GHIground)^2))/N)^(1/2)
    MBE% = MBE/MeanMeasured
    MAE% = MAE/MeanMeasured
    RMSE% = RMSE/MeanMeasured
    */
    
    // all sums are now calculated
    // now do the error computations for mae, mbe, and rmse

int computeHourlyBiasErrors(forecastInputType *fci, int hourIndex)
{
    int N, sampleInd, modelIndex;
    double diff;
    timeSeriesType *thisSample;
    modelErrorType *modelData = &fci->hourErrorGroup[hourIndex];
    modelStatsType *thisModelErr;
#ifdef DEBUG
    int count=0, hoursAhead = fci->hourErrorGroup[hourIndex].hoursAhead;
#endif
    
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->isValid) {
#ifdef nDEBUG
            if(firstTime) {
                    int j;
                    fprintf(stderr, "DEBUG:\n#hourIndex=%d\n", hourIndex);
                    fprintf(stderr, "DEBUG:#year,month,day,hour,minute");
                    for(j=0; j < fci->numModels; j++) 
                        fprintf(stderr, ",%s - ground,abs(%s - ground),(%s - ground)^2", modelData->modelError.modelName, fci->models[j].modelName, fci->models[j].modelName);
                    firstTime = False;
                    fprintf(stderr, "\n)");
            }
 
            fprintf(stderr, "DEBUG:%s", dtToStringCsv2(&thisSample->dateTime));
#endif
            
#ifdef DEBUG
            if(hoursAhead == DEBUGHOUR)
                fprintf(stderr, "DEBUG:group %d\n", count++);
#endif
            for(modelIndex=-1; modelIndex < fci->numModels; modelIndex++) {
                thisModelErr = NULL;
                if(modelIndex < 0) {
                    diff = thisSample->satGHI - thisSample->groundGHI;
#ifdef DEBUG
                    if(hoursAhead == DEBUGHOUR) {
                        fprintf(stderr, "DEBUG:%s,satGHI=%.1f,grndGHI=%.1f,diff=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                thisSample->satGHI ,thisSample->groundGHI,diff);
                    }
#endif
                    thisModelErr = &fci->hourErrorGroup[hourIndex].satModelError;                   
                }
                else {
                    if(modelData->modelError[modelIndex].isActive) {
                        diff = thisSample->modelData[hourIndex].modelGHI[modelIndex] - thisSample->groundGHI;
#ifdef DEBUG
                        if(hoursAhead == DEBUGHOUR) {
                            fprintf(stderr, "DEBUG:%s,%s=%.1f,grndGHI=%.1f,diff=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                    getGenericModelName(fci, modelIndex),thisSample->modelData[hourIndex].modelGHI[modelIndex],thisSample->groundGHI,diff);
                        }
#endif
                        thisModelErr = &fci->hourErrorGroup[hourIndex].modelError[modelIndex];                        
                    }
                }
                if(thisModelErr) {
                    thisModelErr->sumModel_Ground += diff;
                    thisModelErr->sumAbs_Model_Ground += fabs(diff);
                }
#ifdef nDEBUG                
                fprintf(stderr, ",%.1f,%.1f,%.1f", diff, fabs(diff), diff*diff);
#endif
            }
        }
    }
    
    N = modelData->numValidSamples;
    for(modelIndex=-1; modelIndex < fci->numModels; modelIndex++) {
        thisModelErr = NULL;
        if(modelIndex < 0)
            thisModelErr = &fci->hourErrorGroup[hourIndex].satModelError;
        else {
            if(modelData->modelError[modelIndex].isActive) 
                thisModelErr = &fci->hourErrorGroup[hourIndex].modelError[modelIndex];    
        }
        if(thisModelErr) {
            thisModelErr->mbe = thisModelErr->sumModel_Ground / N;
            thisModelErr->mbePct = thisModelErr->mbe / modelData->meanMeasuredGHI;
            thisModelErr->mae = thisModelErr->sumAbs_Model_Ground / N;
            thisModelErr->maePct = thisModelErr->mae / modelData->meanMeasuredGHI;
        }
    }

    return True;
}
    
int computeHourlyRmseErrors(forecastInputType *fci, int hourIndex)
{
    int N, sampleInd, modelIndex;
    double diff;
    timeSeriesType *thisSample;
    modelErrorType *modelData = &fci->hourErrorGroup[hourIndex];
    modelStatsType *thisModelErr;
#ifdef DEBUG
    int count=0, hoursAhead = fci->hourErrorGroup[hourIndex].hoursAhead;
#endif
    
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->isValid) {
#ifdef nDEBUG
            if(firstTime) {
                    int j;
                    fprintf(stderr, "DEBUG:\n#hourIndex=%d\n", hourIndex);
                    fprintf(stderr, "DEBUG:#year,month,day,hour,minute");
                    for(j=0; j < fci->numModels; j++) 
                        fprintf(stderr, ",%s - ground,abs(%s - ground),(%s - ground)^2", modelData->modelError.modelName, fci->models[j].modelName, fci->models[j].modelName);
                    firstTime = False;
                    fprintf(stderr, "\n)");
            }
 
            fprintf(stderr, "DEBUG:%s", dtToStringCsv2(&thisSample->dateTime));
#endif
            
#ifdef DEBUG
            if(hoursAhead == DEBUGHOUR)
                fprintf(stderr, "DEBUG:group %d\n", count++);
#endif
            for(modelIndex=-1; modelIndex < fci->numModels; modelIndex++) {
                thisModelErr = NULL;
                if(modelIndex < 0) {
                    diff = thisSample->satGHI - thisSample->groundGHI;
#ifdef DEBUG
                    if(hoursAhead == DEBUGHOUR) {
                        fprintf(stderr, "DEBUG:%s,satGHI=%.1f,grndGHI=%.1f,diff=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                thisSample->satGHI ,thisSample->groundGHI,diff);
                    }
#endif
                    thisModelErr = &fci->hourErrorGroup[hourIndex].satModelError;                   
                }
                else {
                    if(modelData->modelError[modelIndex].isActive) {
                        diff = thisSample->modelData[hourIndex].modelGHI[modelIndex] - thisSample->groundGHI;
#ifdef DEBUG
                        if(hoursAhead == DEBUGHOUR) {
                            fprintf(stderr, "DEBUG:%s,%s=%.1f,grndGHI=%.1f,diff=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                    getGenericModelName(fci, modelIndex),thisSample->modelData[hourIndex].modelGHI[modelIndex],thisSample->groundGHI,diff);
                        }
#endif
                        thisModelErr = &fci->hourErrorGroup[hourIndex].modelError[modelIndex];                        
                    }
                }
                if(thisModelErr) {
                    thisModelErr->sumModel_Ground_2 += (diff * diff);
                }
#ifdef nDEBUG                
                fprintf(stderr, ",%.1f,%.1f,%.1f", diff, fabs(diff), diff*diff);
#endif
            }
        }
    }
    
    N = modelData->numValidSamples;

    for(modelIndex=-1; modelIndex < fci->numModels; modelIndex++) {
        thisModelErr = NULL;
        if(modelIndex < 0)
            thisModelErr = &fci->hourErrorGroup[hourIndex].satModelError;
        else {
            if(modelData->modelError[modelIndex].isActive) 
                thisModelErr = &fci->hourErrorGroup[hourIndex].modelError[modelIndex];    
        }
        if(thisModelErr) {
            thisModelErr->rmse = sqrt(thisModelErr->sumModel_Ground_2 / N);
            thisModelErr->rmsePct = thisModelErr->rmse / modelData->meanMeasuredGHI; 
        }
    }

    return True;
}
 
//#define DEBUG_2

int computeHourlyRmseErrorWeighted(forecastInputType *fci, int hourIndex)
{
    int N, sampleInd, modelIndex;
    double diff;
    double weight, weightTotal;
    timeSeriesType *thisSample;
    modelErrorType *modelData = &fci->hourErrorGroup[hourIndex];
    modelStatsType *thisModelErr, *weightedModelErr = &fci->hourErrorGroup[hourIndex].weightedModelError;

#ifdef DEBUG_2
    int count=0, hoursAhead = fci->hourErrorGroup[hourIndex].hoursAhead;
#endif
    
    weightedModelErr->sumModel_Ground_2 = 0;
    
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->isValid) {
#ifdef nDEBUG
            if(firstTime) {
                    int j;
                    fprintf(stderr, "DEBUG:\n#hourIndex=%d\n", hourIndex);
                    fprintf(stderr, "DEBUG:#year,month,day,hour,minute");
                    for(j=0; j < fci->numModels; j++) 
                        fprintf(stderr, ",%s - ground,abs(%s - ground),(%s - ground)^2", modelData->modelError.modelName, fci->models[j].modelName, fci->models[j].modelName);
                    firstTime = False;
                    fprintf(stderr, "\n)");
            }
 
            fprintf(stderr, "DEBUG:%s", dtToStringCsv2(&thisSample->dateTime));
#endif
#ifdef DEBUG_2
            if(hoursAhead == DEBUGHOUR)
                fprintf(stderr, "DEBUG:group %d\n", count++);
#endif
            // instead of each model having a separate rmse, they will have a composite rmse
            weightTotal = 0;
            thisSample->weightedModelGHI = 0;
            for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                thisModelErr = &fci->hourErrorGroup[hourIndex].modelError[modelIndex];
                if(modelData->modelError[modelIndex].isActive) {
                    weight = thisModelErr->weight;
                    thisSample->weightedModelGHI += (thisSample->modelData[hourIndex].modelGHI[modelIndex] * weight);
                    weightTotal += weight;               
#ifdef DEBUG_2
                    if(hoursAhead == DEBUGHOUR && weightedModelErr->weight > 0.01*/) {
                        fprintf(stderr, "DEBUG:%s,%s=%.1f,weight=%.2f,weightedGHI=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                getGenericModelName(fci, modelIndex),thisSample->modelData[hourIndex].modelGHI[modelIndex],weight,thisSample->weightedModelGHI);
                    }
#endif
                }
            }
            if(weightTotal > 1.1) 
                fprintf(stderr, "Internal Error: model weights sum to %.2f\n", weightTotal);
            
            diff = thisSample->weightedModelGHI - thisSample->groundGHI;
            weightedModelErr->sumModel_Ground_2 += (diff * diff);
        }
    }
    
    
    N = modelData->numValidSamples;   
    weightedModelErr->rmse = sqrt(weightedModelErr->sumModel_Ground_2 / N);
    weightedModelErr->rmsePct = weightedModelErr->rmse / modelData->meanMeasuredGHI; 

#ifdef DEBUG_2
    fprintf(stderr, "weightedRMSE: N=%d, sumModel_Ground_2=%.1f, totalWeights=%.1f\n", N, weightedModelErr->sumModel_Ground_2, weightTotal);
#endif
    return True;
}

void dumpNumModelsReportingTable(forecastInputType *fci)
{
    //             hours ahead
    //
    // datetime1 nmr1, nmr2, nmr3, ..
    // datetime2
    // datetime3
    // ...
    char tempFileName[2048];
    int sampleInd, modelIndex, hourIndex, numModelsReporting;
    timeSeriesType *thisSample;
    
    sprintf(tempFileName, "%s/%s.numModelsReporting.csv", fci->outputDirectory, fci->siteName);
    fci->modelsAttendenceFile.fileName = strdup(tempFileName);
    
    if((fci->modelsAttendenceFile.fp = fopen(fci->modelsAttendenceFile.fileName, "w")) == NULL) {
        sprintf(ErrStr, "Couldn't open file %s: %s", fci->modelsAttendenceFile.fileName, strerror(errno));
        FatalError("dumpNumModelsReportingTable()", ErrStr);
    }
    
    fprintf(fci->modelsAttendenceFile.fp, "#Number of Models Reporting for site %s, lat=%.3f, lon=%.3f, ha='hours ahead'\n", fci->siteName, fci->lat, fci->lon);
    fprintf(fci->modelsAttendenceFile.fp, "#year,month,day,hour,minute");
    for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
        fprintf(fci->modelsAttendenceFile.fp, ",ha_%d", fci->hourErrorGroup[hourIndex].hoursAhead);
    }
    fprintf(fci->modelsAttendenceFile.fp, "\n");
    
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->sunIsUp) {
            fprintf(fci->modelsAttendenceFile.fp, "%s", dtToStringCsv2(&thisSample->dateTime));
            for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) { 
                numModelsReporting = 0;
                for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                    if(fci->hourErrorGroup[hourIndex].modelError[modelIndex].isUsable)  // is this model turned on for this hoursAhead?
                        if(thisSample->modelData[hourIndex].modelGHI[modelIndex] >= 5)  // is the value for GHI good?
                            numModelsReporting++;
                }
                fprintf(fci->modelsAttendenceFile.fp, ",%d", numModelsReporting);
            }    
            fprintf(fci->modelsAttendenceFile.fp, "\n");
        }
    }
    
    fclose(fci->modelsAttendenceFile.fp);
}