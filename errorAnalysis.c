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
#define getModelGHI(modelIndex) (modelIndex < 0 ? thisSample->satGHI : thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex])
#define incrementModelN() (modelIndex < 0 ? (modelRun->satModelStats.N++) : (modelRun->hourlyModelStats[modelIndex].N++))

#define DEBUGHOUR 1

void clearModelStats(modelStatsType *thisModelStats);

int doErrorAnalysis(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    clearHourlyErrorFields(fci, hoursAheadIndex, hoursAfterSunriseIndex);   

    if(!filterHourlyForecastData(fci, hoursAheadIndex, hoursAfterSunriseIndex))
        return False;    
    //if(!computeHourlyBiasErrors(fci, hoursAheadIndex, hoursAfterSunriseIndex))
    //    return False;
    if(!computeHourlyRmseErrors(fci, hoursAheadIndex, hoursAfterSunriseIndex))
        return False;
    
    return True;
}



void clearHourlyErrorFields(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int modelIndex;
    modelRunType *modelRun;
    modelRun = hoursAfterSunriseIndex >= 0 ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];   

    //fprintf(stderr, "Clearing stats fields for hour %d\n", hoursAheadIndex);
    // zero out all statistical values
    clearModelStats(&modelRun->satModelStats);
    clearModelStats(&modelRun->weightedModelStats);
    
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) 
        clearModelStats(&modelRun->hourlyModelStats[modelIndex]);
}

void clearModelStats(modelStatsType *thisModelStats)
{
    thisModelStats->sumModel_Ground = thisModelStats->sumAbs_Model_Ground = thisModelStats->sumModel_Ground_2 = 0;
    thisModelStats->mae = thisModelStats->mbe = thisModelStats->rmse = 0;  
    thisModelStats->maePct = thisModelStats->mbePct = thisModelStats->rmsePct = 0;  
    thisModelStats->N = 0; 
}


#define WRITE_FILTERED_DATA
int filterHourlyForecastData(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int sampleInd, modelIndex, hoursAfterSunriseOK;
    double thisGHI;
    timeSeriesType *thisSample;
    modelRunType *modelRun;
    modelRun = hoursAfterSunriseIndex >= 0 ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
    
    int hoursAhead = modelRun->hoursAhead;
    int hoursAfterSunrise = modelRun->hoursAfterSunrise;
    // reset a few variables
    modelRun->meanMeasuredGHI = modelRun->numValidSamples = modelRun->ground_N = 0;
    modelRun->satModelStats.N = 0;
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) 
        modelRun->hourlyModelStats[modelIndex].N = 0;

    for(sampleInd=0; sampleInd<fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        thisSample->isValid = True;
        hoursAfterSunriseOK = (hoursAfterSunriseIndex < 0 || (thisSample->hoursAfterSunrise == hoursAfterSunrise));

        if(!hoursAfterSunriseOK) {
            thisSample->isValid = False;
            continue;
        }
        
        // deal with the sat model  
        if(thisSample->satGHI < 5) {
            if(thisSample->sunIsUp) 
                fprintf(fci->warningsFile.fp, "%s : bad sample: sat model, hoursAhead = %d : GHI = %.1f, zenith = %.1f\n", 
                    dtToStringCsv2(&thisSample->dateTime), modelRun->hoursAhead, thisSample->satGHI, thisSample->zenith);
            if(fci->filterWithSatModel)
                thisSample->isValid = False;
        }
        else
            modelRun->satModelStats.N++;
        //
        
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
            if(modelRun->hourlyModelStats[modelIndex].isActive) { // isActive because we want forecast and reference models
                thisGHI = thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex];
                if(thisGHI < 5) {
//#ifdef DEBUG1
                    if(thisSample->sunIsUp) 
                        fprintf(fci->warningsFile.fp, "%s : bad sample: model %s, modelIndex = %d, hoursAhead = %d : GHI = %.1f, zenith = %.1f\n", 
                            dtToStringCsv2(&thisSample->dateTime), getGenericModelName(fci, modelIndex), modelIndex, modelRun->hoursAhead, thisGHI, thisSample->zenith);
//#endif                
                    thisSample->isValid = False;
                    // break;
                }     
                else { // if this model's GHI didn't trigger an isValid=False, increment N
#ifdef DEBUG1
                    fprintf(stderr, "%s hours ahead %d: good sample: model %s: GHI = %.1f\n", 
                         dtToStringCsv2(&thisSample->dateTime), modelRun->hoursAhead, getGenericModelName(fci, modelIndex), thisGHI);
#endif                                   
                    modelRun->hourlyModelStats[modelIndex].N++;
                }
            }
        }
        
        if(thisSample->groundGHI < MIN_GHI_VAL ) {
            thisSample->isValid = False;
        }
        else {
            modelRun->ground_N++;
        }
        
        if(thisSample->isValid) {
            modelRun->meanMeasuredGHI += thisSample->groundGHI;
            modelRun->numValidSamples++;
#ifdef DEBUG1
            fprintf(stderr, "%s hours ahead: %d:  all good: meanMeasuredGHI=%.1f numValidSamples=%d\n", 
                 dtToStringCsv2(&thisSample->dateTime), modelRun->hoursAhead, modelRun->meanMeasuredGHI, modelRun->numValidSamples);
#endif
        }
    }
    
    if(modelRun->numValidSamples < 1) {
        fprintf(fci->warningsFile.fp, "doErrorAnalysis(): for hour index %d (%d hours ahead): got too few valid points to work with\n", hoursAheadIndex, modelRun->hoursAhead);
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
                    if(modelRun->hourlyModelStats[modelIndex].isActive) {
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
                if(modelRun->hourlyModelStats[modelIndex].isActive) {
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
    sprintf(filename, "%s/%s.filteredData.hoursAhead=%d.hoursAfterSunrise=%d.csv", fci->outputDirectory, fci->siteName, hoursAhead,hoursAfterSunrise);
    if((fp = fopen(filename, "w")) == NULL) {
        fprintf(stderr, "Couldn't open file %s\n", filename);
        exit(1);
    }
    // print the header
    fprintf(fp, "#year,month,day,hour,min,lineNum,groupIsValid,groundGHI,satGHI");
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].isActive) {
            fprintf(fp, ",%s", getGenericModelName(fci, modelIndex));
        }
    }
    fprintf(fp, "\n");
    // print the data
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
            thisSample = &fci->timeSeries[sampleInd];
            if(thisSample->sunIsUp) {  // only dump sunUp points
                fprintf(fp, "%s,%d,%s,%.0f,%.0f", dtToStringCsv2(&thisSample->dateTime), sampleInd, thisSample->isValid ? "yes" : "no", 
                        thisSample->groundGHI , thisSample->satGHI );
                for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                    if(modelRun->hourlyModelStats[modelIndex].isActive) {
                        fprintf(fp, ",%.0f", thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex]);                    
                    }
                } 
                fprintf(fp, "\n");
            }
    }
    fclose(fp);
#endif
    
    modelRun->meanMeasuredGHI /= modelRun->numValidSamples;

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

int computeHourlyBiasErrors(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int N, sampleInd, modelIndex;
    double diff;
    timeSeriesType *thisSample;
    modelStatsType *thisModelStats;
    modelRunType *modelRun;
    modelRun = hoursAfterSunriseIndex >= 0 ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
    
#ifdef DEBUG
    int count=0, hoursAhead = modelRun->hoursAhead;
#endif
    
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->isValid) {
#ifdef nDEBUG
            if(firstTime) {
                    int j;
                    fprintf(stderr, "DEBUG:\n#hoursAheadIndex=%d\n", hoursAheadIndex);
                    fprintf(stderr, "DEBUG:#year,month,day,hour,minute");
                    for(j=0; j < fci->numModels; j++) 
                        fprintf(stderr, ",%s - ground,abs(%s - ground),(%s - ground)^2", modelRun->modelError.modelName, fci->models[j].modelName, fci->models[j].modelName);
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
                thisModelStats = NULL;
                if(modelIndex < 0) {
                    diff = thisSample->satGHI - thisSample->groundGHI;
#ifdef DEBUG
                    if(hoursAhead == DEBUGHOUR) {
                        fprintf(stderr, "DEBUG:%s,satGHI=%.1f,grndGHI=%.1f,diff=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                thisSample->satGHI ,thisSample->groundGHI,diff);
                    }
#endif
                    thisModelStats = &modelRun->satModelStats;                   
                }
                else {
                    if(modelRun->hourlyModelStats[modelIndex].isActive) {
                        diff = thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] - thisSample->groundGHI;
#ifdef DEBUG
                        if(hoursAhead == DEBUGHOUR) {
                            fprintf(stderr, "DEBUG:%s,%s=%.1f,grndGHI=%.1f,diff=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                    getGenericModelName(fci, modelIndex),thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex],thisSample->groundGHI,diff);
                        }
#endif
                        thisModelStats = &modelRun->hourlyModelStats[modelIndex];                        
                    }
                }
                if(thisModelStats) {
                    thisModelStats->sumModel_Ground += diff;
                    thisModelStats->sumAbs_Model_Ground += fabs(diff);
                }
#ifdef nDEBUG                
                fprintf(stderr, ",%.1f,%.1f,%.1f", diff, fabs(diff), diff*diff);
#endif
            }
        }
    }
    
    N = modelRun->numValidSamples;
    for(modelIndex=-1; modelIndex < fci->numModels; modelIndex++) {
        thisModelStats = NULL;
        if(modelIndex < 0)
            thisModelStats = &modelRun->satModelStats;
        else {
            if(modelRun->hourlyModelStats[modelIndex].isActive) 
                thisModelStats = &modelRun->hourlyModelStats[modelIndex];    
        }
        if(thisModelStats) {
            thisModelStats->mbe = thisModelStats->sumModel_Ground / N;
            thisModelStats->mbePct = thisModelStats->mbe / modelRun->meanMeasuredGHI;
            thisModelStats->mae = thisModelStats->sumAbs_Model_Ground / N;
            thisModelStats->maePct = thisModelStats->mae / modelRun->meanMeasuredGHI;
        }
    }

    return True;
}
    
int computeHourlyRmseErrors(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int N, sampleInd, modelIndex;
    double diff;
    timeSeriesType *thisSample;
    modelStatsType *thisModelStats;
#ifdef DEBUG
    int count=0, hoursAhead = modelRun->hoursAhead;
#endif
    modelRunType *modelRun;
    modelRun = hoursAfterSunriseIndex >= 0 ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
    
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->isValid) {
#ifdef nDEBUG
            if(firstTime) {
                    int j;
                    fprintf(stderr, "DEBUG:\n#hoursAheadIndex=%d\n", hoursAheadIndex);
                    fprintf(stderr, "DEBUG:#year,month,day,hour,minute");
                    for(j=0; j < fci->numModels; j++) 
                        fprintf(stderr, ",%s - ground,abs(%s - ground),(%s - ground)^2", modelRun->modelError.modelName, fci->models[j].modelName, fci->models[j].modelName);
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
                thisModelStats = NULL;
                if(modelIndex < 0) {
                    diff = thisSample->satGHI - thisSample->groundGHI;
#ifdef DEBUG
                    if(hoursAhead == DEBUGHOUR) {
                        fprintf(stderr, "DEBUG:%s,satGHI=%.1f,grndGHI=%.1f,diff=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                thisSample->satGHI ,thisSample->groundGHI,diff);
                    }
#endif
                    thisModelStats = &modelRun->satModelStats;                   
                }
                else {
                    if(modelRun->hourlyModelStats[modelIndex].isActive) {
                        diff = thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] - thisSample->groundGHI;
#ifdef DEBUG
                        if(hoursAhead == DEBUGHOUR) {
                            fprintf(stderr, "DEBUG:%s,%s=%.1f,grndGHI=%.1f,diff=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                    getGenericModelName(fci, modelIndex),thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex],thisSample->groundGHI,diff);
                        }
#endif
                        thisModelStats = &modelRun->hourlyModelStats[modelIndex];                        
                    }
                }
                if(thisModelStats) {
                    thisModelStats->sumModel_Ground_2 += (diff * diff);
                }
#ifdef nDEBUG                
                fprintf(stderr, ",%.1f,%.1f,%.1f", diff, fabs(diff), diff*diff);
#endif
            }
        }
    }
    
    N = modelRun->numValidSamples;

    for(modelIndex=-1; modelIndex < fci->numModels; modelIndex++) {
        thisModelStats = NULL;
        if(modelIndex < 0)
            thisModelStats = &modelRun->satModelStats;
        else {
            if(modelRun->hourlyModelStats[modelIndex].isActive) 
                thisModelStats = &modelRun->hourlyModelStats[modelIndex];    
        }
        if(thisModelStats) {
            thisModelStats->rmse = sqrt(thisModelStats->sumModel_Ground_2 / N);
            thisModelStats->rmsePct = thisModelStats->rmse / modelRun->meanMeasuredGHI; 
        }
    }

    return True;
}
 
//#define DEBUG_2

int computeHourlyRmseErrorWeighted(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int N, sampleInd, modelIndex;
    double diff;
    double weight, weightTotal;
    timeSeriesType *thisSample;
    modelStatsType *thisModelStats, *weightedModelErr;
    modelRunType *modelRun;
    modelRun = hoursAfterSunriseIndex >= 0 ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

#ifdef DEBUG_2
    int count=0, hoursAhead = modelRun->hoursAhead;
#endif
    weightedModelErr = &modelRun->weightedModelStats;
    weightedModelErr->sumModel_Ground_2 = 0;
    
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->isValid) {
#ifdef nDEBUG
            if(firstTime) {
                    int j;
                    fprintf(stderr, "DEBUG:\n#hoursAheadIndex=%d\n", hoursAheadIndex);
                    fprintf(stderr, "DEBUG:#year,month,day,hour,minute");
                    for(j=0; j < fci->numModels; j++) 
                        fprintf(stderr, ",%s - ground,abs(%s - ground),(%s - ground)^2", modelRun->modelError.modelName, fci->models[j].modelName, fci->models[j].modelName);
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
                thisModelStats = &modelRun->hourlyModelStats[modelIndex];
                if(modelRun->hourlyModelStats[modelIndex].isActive) {
                    weight = thisModelStats->weight;
                    thisSample->weightedModelGHI += (thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] * weight);
                    weightTotal += weight;               
#ifdef DEBUG_2
                  if(hoursAhead == DEBUGHOUR && weightedModelErr->weight > 0.01) {
                        fprintf(stderr, "DEBUG:%s,%s=%.1f,weight=%.2f,weightedGHI=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                getGenericModelName(fci, modelIndex),thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex],weight,thisSample->weightedModelGHI);
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
    
    
    N = modelRun->numValidSamples;   
    weightedModelErr->rmse = sqrt(weightedModelErr->sumModel_Ground_2 / N);
    weightedModelErr->rmsePct = weightedModelErr->rmse / modelRun->meanMeasuredGHI; 

#ifdef DEBUG_2
    fprintf(stderr, "weightedRMSE: N=%d, sumModel_Ground_2=%.1f, totalWeights=%.1f\n", N, weightedModelErr->sumModel_Ground_2, weightTotal);
#endif
    return True;
}

#define DEBUG_3

int computeHourlyRmseErrorWeighted_AllHoursAfterSunrise(forecastInputType *fci, int hoursAheadIndex)
{
    int N=0, sampleInd, modelIndex, hoursAfterSunriseIndex;
    double diff;
    double weight, weightTotal, meanMeasuredGHI;
    timeSeriesType *thisSample;
    modelStatsType *thisModelStats, *weightedModelErr;
    modelRunType *modelRun;

    weightedModelErr = &fci->hoursAheadGroup[hoursAheadIndex].weightedModelStats; //&modelRun->weightedModelStats;
    weightedModelErr->sumModel_Ground_2 = weightedModelErr->rmse = weightedModelErr->rmsePct = 0;
    
    for(hoursAfterSunriseIndex=0; hoursAfterSunriseIndex<fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
        if(!filterHourlyForecastData(fci, hoursAheadIndex, hoursAfterSunriseIndex))  // must re-filter the T/S data for each HA/HAS combination
            //return False;
            continue;
        modelRun = &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex];
        // N += modelRun->numValidSamples;
        // meanMeasuredGHI += modelRun->meanMeasuredGHI;
        for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
            thisSample = &fci->timeSeries[sampleInd];
            if(thisSample->isValid) {
#ifdef DEBUG_3
                fprintf(stderr, "DEBUG:%s,HA=%d,HAS=%d,", dtToStringCsv2(&thisSample->dateTime), modelRun->hoursAhead, modelRun->hoursAfterSunrise);
#endif
    #ifdef nDEBUG
                if(firstTime) {
                        int j;
                        fprintf(stderr, "DEBUG:\n#hoursAheadIndex=%d\n", hoursAheadIndex);
                        fprintf(stderr, "DEBUG:#year,month,day,hour,minute");
                        for(j=0; j < fci->numModels; j++) 
                            fprintf(stderr, ",%s - ground,abs(%s - ground),(%s - ground)^2", modelRun->modelError.modelName, fci->models[j].modelName, fci->models[j].modelName);
                        firstTime = False;
                        fprintf(stderr, "\n)");
                }

                fprintf(stderr, "DEBUG:%s", dtToStringCsv2(&thisSample->dateTime));
    #endif
    #ifdef nDEBUG_2
                if(modelRun->hoursAhead == DEBUGHOUR)
                    fprintf(stderr, "DEBUG:group %d\n", count++);
    #endif
                // instead of each model having a separate rmse, they will have a composite rmse
                weightTotal = 0;
                thisSample->weightedModelGHI = 0;
                
                N++;
                meanMeasuredGHI += thisSample->groundGHI;
                
                for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                    thisModelStats = &modelRun->hourlyModelStats[modelIndex];
                    if(modelRun->hourlyModelStats[modelIndex].isActive) {
                        weight = fci->skipPhase2 ? thisModelStats->optimizedWeightPass1 : thisModelStats->optimizedWeightPass2;
                        // add up weight * GHI for each included forecast model
                        thisSample->weightedModelGHI += (thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] * weight);
                        weightTotal += weight;               
    #ifdef DEBUG_3
                        //if(hoursAhead == DEBUGHOUR && weightedModelErr->weight > 0.01*/) {
                        if(weight > 0.01)
                            fprintf(stderr, "%s=%.1f * %.2f,", 
                                    getGenericModelName(fci, modelIndex),thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex],weight);
                        //}
    #endif
                    }
                }
#ifdef DEBUG_3
                fprintf(stderr, "weightedGHI=%.1f,groundGHI=%.1f\n",thisSample->weightedModelGHI, thisSample->groundGHI);
#endif
                if(weightTotal > 1.1 || weightTotal < 0.9) 
                    fprintf(stderr, "Internal Error: model weights sum to %.2f\n", weightTotal);

                diff = thisSample->weightedModelGHI - thisSample->groundGHI;
                weightedModelErr->sumModel_Ground_2 += (diff * diff);
            }
        }
    }
        
    //N = modelRun->numValidSamples;   
    meanMeasuredGHI /= N;
    weightedModelErr->rmse = sqrt(weightedModelErr->sumModel_Ground_2 / N);
    weightedModelErr->rmsePct = weightedModelErr->rmse / meanMeasuredGHI; 

#ifdef DEBUG_3
    fprintf(stderr, "HA=%d/HAS=1..%d RMSE: N=%d, sumModel_Ground_2=%.1f, meanMeasuredGHI=%.1f, RMSE=%.1f, %%RMSE = %.02f\n", 
            modelRun->hoursAhead, fci->maxHoursAfterSunrise, N, weightedModelErr->sumModel_Ground_2, meanMeasuredGHI, weightedModelErr->rmse, weightedModelErr->rmsePct*100);
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
    int sampleInd, modelIndex, hoursAheadIndex, numModelsReporting;
    timeSeriesType *thisSample;
    
    sprintf(tempFileName, "%s/%s.numModelsReporting.csv", fci->outputDirectory, fci->siteName);
    fci->modelsAttendenceFile.fileName = strdup(tempFileName);
    
    if((fci->modelsAttendenceFile.fp = fopen(fci->modelsAttendenceFile.fileName, "w")) == NULL) {
        sprintf(ErrStr, "Couldn't open file %s: %s", fci->modelsAttendenceFile.fileName, strerror(errno));
        FatalError("dumpNumModelsReportingTable()", ErrStr);
    }
    
    fprintf(fci->modelsAttendenceFile.fp, "#Number of Models Reporting for site %s, lat=%.3f, lon=%.3f, ha='hours ahead'\n", fci->siteName, fci->lat, fci->lon);
    fprintf(fci->modelsAttendenceFile.fp, "#year,month,day,hour,minute");
    for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        fprintf(fci->modelsAttendenceFile.fp, ",ha_%d", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
    }
    fprintf(fci->modelsAttendenceFile.fp, "\n");
    
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->sunIsUp) {
            fprintf(fci->modelsAttendenceFile.fp, "%s", dtToStringCsv2(&thisSample->dateTime));
            for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) { 
                numModelsReporting = 0;
                for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                    if(fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isUsable)  // is this model turned on for this hoursAhead?
                        if(thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] >= 5)  // is the value for GHI good?
                            numModelsReporting++;
                }
                fprintf(fci->modelsAttendenceFile.fp, ",%d", numModelsReporting);
            }    
            fprintf(fci->modelsAttendenceFile.fp, "\n");
        }
    }
    
    fclose(fci->modelsAttendenceFile.fp);
}
