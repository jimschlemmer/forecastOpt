#include "forecastOpt.h"

typedef enum {SatGHI, CorrGHI, OptGHI} mbeSelectType;

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
int getHoursAheadIndex(forecastInputType *fci, int hoursAhead);
int getHoursAfterSunriseIndex(forecastInputType *fci, int hoursAfterSunrise);
int getModelIndex(forecastInputType *fci, char *modelName);
int correctOptimizedGHI(forecastInputType *fci, int hoursAheadIndex);
int cmpDouble(const void *x, const void *y);
double computeMBEcustom(forecastInputType *fci, mbeSelectType which);
double computeMAEcustom(forecastInputType *fci, mbeSelectType which);

int computeModelRMSE(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
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
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];   

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

int filterHourlyForecastData(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int sampleInd, modelIndex, hoursAfterSunriseOK;
    double thisGHI;
    timeSeriesType *thisSample;
    modelRunType *modelRun;
    //modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
    modelRun = hoursAfterSunriseIndex >= 0 ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
 
    int hoursAhead = fci->hoursAheadGroup[hoursAheadIndex].hoursAhead;
    int hoursAfterSunrise = hoursAfterSunriseIndex + 1;
    // reset a few variables
    modelRun->meanMeasuredGHI = modelRun->numValidSamples = modelRun->ground_N = 0;
    modelRun->satModelStats.N = 0;
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) 
        modelRun->hourlyModelStats[modelIndex].N = 0;

    for(sampleInd=0; sampleInd<fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        thisSample->isValid = True;
        
        if(thisSample->zenith > 90) {
            thisSample->isValid = False;
            continue;            
        }
        hoursAfterSunriseOK = (hoursAfterSunriseIndex < 0 || (thisSample->hoursAfterSunrise == hoursAfterSunrise));

        if(!hoursAfterSunriseOK) {
            thisSample->isValid = False;
            continue;
        }
        
        // deal with the sat model  
        if(thisSample->satGHI < 5) {
            if(thisSample->sunIsUp) 
                fprintf(fci->warningsFile.fp, "%s : bad sample: sat model, hoursAhead=%d hoursAfterSunrise=%d: GHI = %.1f, zenith = %.1f\n", 
                    dtToStringCsv2(&thisSample->dateTime), hoursAhead, hoursAfterSunrise, thisSample->satGHI, thisSample->zenith);
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
                        fprintf(fci->warningsFile.fp, "%s : bad sample: model %s, modelIndex=%d, hoursAhead=%d hoursAfterSunrise=%d: GHI = %.1f, zenith = %.1f\n", 
                            dtToStringCsv2(&thisSample->dateTime), getGenericModelName(fci, modelIndex), modelIndex, hoursAhead, hoursAfterSunrise, thisGHI, thisSample->zenith);
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
            fprintf(fci->warningsFile.fp, "%s : bad sample: ground data, hoursAhead=%d hoursAfterSunrise=%d: GHI = %.1f, zenith = %.1f\n", 
                dtToStringCsv2(&thisSample->dateTime), hoursAhead, hoursAfterSunrise, thisGHI, thisSample->zenith);
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
        fprintf(fci->warningsFile.fp, "filterForecastData(): for hour index %d (%d hoursAhead, %d hoursAfterSunrise): got too few valid points to work with", hoursAheadIndex, hoursAhead, hoursAfterSunrise);
        //FatalError("computeModelRMSE()", "Too few valid data points to work with.");
        fflush(fci->warningsFile.fp);
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
double computeMBEcustom(forecastInputType *fci, mbeSelectType which)
{
    double diff, sumModel_Ground=0, mbe, val;
    timeSeriesType *thisSample;
    double numValidSamples=0;
    int sampleInd;
#ifdef DEBUG
    int count=0, hoursAhead = modelRun->hoursAhead;
#endif
    
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->isValid) {
            switch(which) {
                case SatGHI : val = thisSample->satGHI; break;
                case OptGHI : val = thisSample->optimizedGHI; break;
                case CorrGHI: val = thisSample->correctedOptimizedGHI; break;
                default : fprintf(stderr, "Internal error in computeMBEcustom()"); exit(1);
            }
            diff = val - thisSample->groundGHI;
            sumModel_Ground += diff;
            numValidSamples += 1;
        }
    }
    
    mbe = sumModel_Ground / numValidSamples;
    
    return mbe;
}

double computeMAEcustom(forecastInputType *fci, mbeSelectType which)
{
    double diff, sumModel_Ground=0, mae, val;
    timeSeriesType *thisSample;
    double numValidSamples=0;
    int sampleInd;
#ifdef DEBUG
    int count=0, hoursAhead = modelRun->hoursAhead;
#endif
    
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->isValid) {
            switch(which) {
                case SatGHI : val = thisSample->satGHI; break;
                case OptGHI : val = thisSample->optimizedGHI; break;
                case CorrGHI: val = thisSample->correctedOptimizedGHI; break;
                default : fprintf(stderr, "Internal error in computeMAEcustom()"); exit(1);
            }
            // MAE = (Sum (abs(GHImod-GHIground)) ) / N
            diff = fabs(val - thisSample->groundGHI);
            sumModel_Ground += diff;
            numValidSamples += 1;
        }
    }
    
    mae = sumModel_Ground / numValidSamples;
    
    return mae;
}

int computeHourlyBiasErrors(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int N, sampleInd, modelIndex;
    double diff;
    timeSeriesType *thisSample;
    modelStatsType *thisModelStats;
    modelRunType *modelRun;
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
    
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
    modelRunType *modelRun;
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
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
int dumpHourlyOptimizedTS(forecastInputType *fci, int hoursAheadIndex)
{
    int sampleInd, modelIndex;
    double weight, weightTotal;
    timeSeriesType *thisSample;
    modelStatsType *thisModelStats, *weightedModelErr;
    modelRunType *modelRun;
    char fileName[1024];
    int hoursAfterSunrise, hoursAhead = fci->hoursAheadGroup[hoursAheadIndex].hoursAhead;
    
    // open T/S file and print header
    if(fci->runHoursAfterSunrise)
        sprintf(fileName, "%s/%s.optimizedTS.HAS.HA=%03d.csv", fci->outputDirectory, genProxySiteName(fci), hoursAhead);
    else
        sprintf(fileName, "%s/%s.optimizedTS.HA=%03d.csv", fci->outputDirectory, genProxySiteName(fci), hoursAhead);
    fci->optimizedTSFile.fileName = strdup(fileName);
    fprintf(stderr, "\n ======== Generating optimized T/S file %s\n", fci->optimizedTSFile.fileName);
    if((fci->optimizedTSFile.fp = fopen(fci->optimizedTSFile.fileName, "w")) == NULL) {
        sprintf(ErrStr, "Couldn't open file %s : %s", fci->optimizedTSFile.fileName, strerror(errno));
        FatalError("dumpHourlyOptimizedTS()", ErrStr);
    }
    // print the header
    fprintf(fci->optimizedTSFile.fp, "#site=%s lat=%.3f lon=%.3f hoursAhead=%d HAS=%s", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, hoursAhead, fci->runHoursAfterSunrise ? "yes" : "no");
    fprintf(fci->optimizedTSFile.fp, "\n#year,month,day,hour,%s", fci->runHoursAfterSunrise ? "min,HAS,groupIsValid" : "min,groupIsValid"); //groundGHI,satGHI");
    
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][0] : &fci->hoursAheadGroup[hoursAheadIndex];
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].isActive) {
            fprintf(fci->optimizedTSFile.fp, ",%s", getGenericModelName(fci, modelIndex));
        }
    }
    fprintf(fci->optimizedTSFile.fp, ",groundGHI,satGHI,optimizedGHI,correctedGHI,clearskyGHI\n");

    weightedModelErr = &modelRun->weightedModelStats;
    weightedModelErr->sumModel_Ground_2 = 0;
    
    if(!filterHourlyForecastData(fci, hoursAheadIndex, -1)) { // must re-filter the T/S data to turn on all HAS isValid bits (otherwise we end up with only the last HAS T/S data active)
        fprintf(stderr, "!!! Warning: found no filtered data from hoursAheadIndex/hoursAhead = %d/%d\n", hoursAheadIndex, hoursAhead);
        return False;
    }
    
    // compute optimizedGHI, ktClearsky, ktOptimizedGHI
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->sunIsUp) {
            // instead of each model having a separate rmse, they will have a composite rmse
            weightTotal = 0;
            thisSample->optimizedGHI = 0;
            modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][thisSample->hoursAfterSunrise - 1] : &fci->hoursAheadGroup[hoursAheadIndex];

            for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                thisModelStats = &modelRun->hourlyModelStats[modelIndex];                
                if(thisModelStats->isActive && thisModelStats->optimizedWeightPhase2 > 0) {
                    weight = ((double) thisModelStats->optimizedWeightPhase2 )/100.0;
                    thisSample->optimizedGHI += (thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] * weight);
                    weightTotal += weight;               
                }
            }    
            
            if(thisSample->clearskyGHI > 10) {
                thisSample->ktSatGHI = thisSample->satGHI/thisSample->clearskyGHI;
                thisSample->ktOptimizedGHI = thisSample->optimizedGHI/thisSample->clearskyGHI;
/*
                if(thisSample->ktSatGHI > 1.01 || thisSample->ktOptimizedGHI > 1.01)
                    fprintf(stderr, "Badval\n");
*/
            }        
        }
    }
    
    correctOptimizedGHI(fci, hoursAheadIndex);
    
    // now print it all out to the TS output file
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->sunIsUp) {
            modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][thisSample->hoursAfterSunrise - 1] : &fci->hoursAheadGroup[hoursAheadIndex];
            
            fprintf(fci->optimizedTSFile.fp, "%s", dtToStringCsv2(&thisSample->dateTime)); //, thisSample->groundGHI, thisSample->optimizedGHI);
            if(fci->runHoursAfterSunrise) {
                hoursAfterSunrise = thisSample->hoursAfterSunrise - 1;
                if(hoursAfterSunrise > fci->maxHoursAfterSunrise) {
                    sprintf(ErrStr, "Internal Error: got hoursAfterSunrise out of range: %d", hoursAfterSunrise);
                    FatalError("dumpHourlyOptimizedTS()", ErrStr);
                }
            }
            if(fci->runHoursAfterSunrise) 
                fprintf(fci->optimizedTSFile.fp, ",%d", modelRun->hoursAfterSunrise);
            fprintf(fci->optimizedTSFile.fp, ",%s", thisSample->isValid ? "yes" : "no");
            

            for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                thisModelStats = &modelRun->hourlyModelStats[modelIndex];                
                if(thisModelStats->isActive) {
                    fprintf(fci->optimizedTSFile.fp, ",%.0f", thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex]);                    
                }
            }
            fprintf(fci->optimizedTSFile.fp, ",%.0f,%.0f,%.0f,%.0f,%.0f\n", thisSample->groundGHI, thisSample->satGHI, 
                    thisSample->isValid ? thisSample->optimizedGHI : -999, 
                    thisSample->isValid ? thisSample->correctedOptimizedGHI : -999, 
                    thisSample->clearskyGHI);            
        }
    }
    
    fclose(fci->optimizedTSFile.fp);

    return True;
}

#define DEBUG_CORRECTION
int correctOptimizedGHI(forecastInputType *fci, int hoursAheadIndex)
{
    int sampleInd;
    timeSeriesType *thisSample;
    modelRunType *modelRun;
    static double *sortedKtSatGHI=NULL, *sortedKtOptimizedGHI=NULL;
    static int sortedSize = 0;
    double satMAE, satMBE, optMAEpre, optMBEpre, optMAEpost, optMBEpost;
    
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][0] : &fci->hoursAheadGroup[hoursAheadIndex];
   
    if(sortedKtSatGHI == NULL) { 
        sortedSize = fci->numTotalSamples;
        if((sortedKtSatGHI = (double *) malloc(sizeof(double) * sortedSize) ) == NULL)
            FatalError("correctOptimizedGHI()", "Memory allocation error");
        if((sortedKtOptimizedGHI = (double *) malloc(sizeof(double) * sortedSize) ) == NULL)
            FatalError("correctOptimizedGHI()", "Memory allocation error");        
    }
    else if(sortedSize != fci->numTotalSamples) {
        fprintf(stderr, "numSamples changed from %d to %d\n", sortedSize, fci->numTotalSamples);
        FatalError("correctOptimizedGHI()", "bye"); 
        
    }
    
    if(fci->correctionStatsFile.fp == NULL) {
        char tempFileName[1024];
        sprintf(tempFileName, "%s/correctionStats.%s.%s-%s.div=%d.hours=%d-%d.csv", fci->outputDirectory, genProxySiteName(fci), dtToStringDateOnly(&fci->startDate), dtToStringDateOnly(&fci->endDate), fci->numDivisions, fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead);
        fci->correctionStatsFile.fileName = strdup(tempFileName);
        if((fci->correctionStatsFile.fp = fopen(fci->correctionStatsFile.fileName, "w")) == NULL) {
            sprintf(ErrStr, "couldn't open file %s", fci->correctionStatsFile.fileName);
            FatalError("correctOptimizedGHI()", ErrStr);             
        }
        fprintf(fci->correctionStatsFile.fp, "#site,HA,A,B,satMAE,satMBE,optMAEpre,optMBEpre,optMAEpost,optMBEpost\n");
    }
    
    // first load up the kt vectors
#ifdef DEBUG_CORRECTION
    fprintf(stderr, "#year,month,day,hour,clr,satGHI,optGHI,ratioSat,ratioOpt\n");
#endif
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        sortedKtSatGHI[sampleInd] = 0;
        sortedKtOptimizedGHI[sampleInd] = 0;
        if(thisSample->sunIsUp && thisSample->isValid && thisSample->clearskyGHI > 200) {
            sortedKtSatGHI[sampleInd] = round(thisSample->satGHI)/thisSample->clearskyGHI;
            sortedKtOptimizedGHI[sampleInd] = round(thisSample->optimizedGHI)/thisSample->clearskyGHI;
        }
#ifdef DEBUG_CORRECTION
        fprintf(stderr, "%s,%.0f,%.0f,%.0f,%.4f,%.4f\n", dtToStringCsv2(&thisSample->dateTime), thisSample->clearskyGHI, thisSample->satGHI, thisSample->optimizedGHI, sortedKtSatGHI[sampleInd], sortedKtOptimizedGHI[sampleInd]);
    }
#endif
    // now sort them
    //qsort(arr, sizeof(arr)/sizeof(arr[0]), sizeof(arr[0]), cmp);
    qsort(sortedKtSatGHI, sortedSize, sizeof(double), cmpDouble);
    qsort(sortedKtOptimizedGHI, sortedSize, sizeof(double), cmpDouble);

    // get N
    int i;
    double N=0,sumSat, sumOpt;
    for(i=0; i<sortedSize; i++) {
        if(sortedKtSatGHI[i] < 0.95) {
            N = i+1;
            break;
        }
        sumSat += sortedKtSatGHI[i];
        sumOpt += sortedKtOptimizedGHI[i];
#ifdef DEBUG_CORRECTION
        //fprintf(stderr, "%d : satKt=%.4f optKt=%.4f\n", i, sortedKtSatGHI[i], sortedKtOptimizedGHI[i]);
        fprintf(stderr, "%.4f,%.4f,%.8f,%.8f\n", sortedKtSatGHI[i], sortedKtOptimizedGHI[i],sortedKtSatGHI[i], sortedKtOptimizedGHI[i]);
#endif
    }
    if(N < 1) {
        sprintf(ErrStr, "Internal error: N=0, sortedSize=%d\n", sortedSize);
        FatalError("correctOptimizedGHI()", ErrStr);
    }
    
    // calculate average sortedKtSatGHI and sortedKtOptimizedGHI for first N values    
    double SatHiAvg = sumSat/N;
    double OptHiAvg = sumOpt/N;
    
    modelRun->correctionVarA = SatHiAvg/OptHiAvg;
#ifdef DEBUG_CORRECTION
    fprintf(stderr, "\nN=%.0f SatHiAvg = %.4f OptHiAvg = %.4f correctionVarA = %.4f\n", N, SatHiAvg, OptHiAvg, modelRun->correctionVarA);
#endif
    
    // iterate MBE calculation 
    // int computeHourlyBiasErrors(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)

    modelRun->correctionVarB = 1.01;
       
    satMAE = computeMAEcustom(fci, SatGHI);
    satMBE = computeMBEcustom(fci, SatGHI);
    optMAEpre = computeMAEcustom(fci, OptGHI);
    optMBEpre = computeMBEcustom(fci, OptGHI);

    // COR = B + (A-B) * optIndex
    // X = MIN(1.025, COR * optIndex)
    // GHICor = X * GHIclear
    do {
        modelRun->correctionVarB -= 0.01;  // decrement B
        for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {  // using A & B recompute TS optGHI as correctedOptimizedGHI
            thisSample = &fci->timeSeries[sampleInd];
            if(thisSample->sunIsUp && thisSample->isValid && thisSample->clearskyGHI > 10) {
                double COR = modelRun->correctionVarB + (modelRun->correctionVarA - modelRun->correctionVarB) * thisSample->ktOptimizedGHI;
                double X = MIN(1.025, COR * thisSample->ktOptimizedGHI);
                thisSample->correctedOptimizedGHI = X * thisSample->clearskyGHI;
#ifdef DEBUG_CORRECTION_A
                fprintf(stderr, "A=%.4f B=%.4f ktOpt=%.4f COR=%.4f X=%.4f clearGHI=%.1f corrOptGHI=%.1f\n", modelRun->correctionVarA, modelRun->correctionVarB, thisSample->ktOptimizedGHI, COR, X, thisSample->clearskyGHI, thisSample->correctedOptimizedGHI);
#endif
            }
        }
        
        optMAEpost = computeMAEcustom(fci, CorrGHI);
        optMBEpost = computeMBEcustom(fci, CorrGHI);
#ifdef DEBUG_CORRECTION
        fprintf(stderr, "A=%.3f B=%.4f satMBE=%.3f optMBE=%.3f\n", modelRun->correctionVarA, modelRun->correctionVarB, satMBE, optMBEpost);
#endif

    } while(optMBEpost > satMBE && modelRun->correctionVarB > 0);
    
    fprintf(fci->correctionStatsFile.fp, "%s,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", genProxySiteName(fci), modelRun->hoursAhead, modelRun->correctionVarA, 
            modelRun->correctionVarB, satMAE, satMBE, optMAEpre, optMBEpre, optMAEpost, optMBEpost);

    return True;
}

int cmpDouble(const void *x, const void *y)
{
  double xx = *(double*)x, yy = *(double*)y;
  if (xx > yy) return -1;
  if (xx < yy) return  1;
  return 0;
}

int dumpHourlyOptimizedTS_HAS_depricated(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int sampleInd, modelIndex;
    double weight, weightTotal, thisModelGHI;
    timeSeriesType *thisSample;
    modelStatsType *thisModelStats, *weightedModelErr;
    modelRunType *modelRun;
    char fileName[1024];
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
    int hoursAhead = modelRun->hoursAhead;
    int hoursAfterSunrise = modelRun->hoursAfterSunrise;
    
    // open T/S file and print header
    if(fci->runHoursAfterSunrise)
        sprintf(fileName, "%s/%s.optimizedTS.HA=%03d_HAS=%03d.csv", fci->outputDirectory, genProxySiteName(fci), hoursAhead, hoursAfterSunrise);
    else
        sprintf(fileName, "%s/%s.optimizedTS.HA=%03d.csv", fci->outputDirectory, genProxySiteName(fci), hoursAhead);
    fci->optimizedTSFile.fileName = strdup(fileName);
    fprintf(stderr, "\n ======== Generating optimized T/S file %s\n", fci->optimizedTSFile.fileName);
    if((fci->optimizedTSFile.fp = fopen(fci->optimizedTSFile.fileName, "w")) == NULL) {
        sprintf(ErrStr, "Couldn't open file %s : %s", fci->optimizedTSFile.fileName, strerror(errno));
        FatalError("dumpHourlyOptimizedTS_HAS()", ErrStr);
    }
    // print the header
    fprintf(fci->optimizedTSFile.fp, "#site=%s lat=%.3f lon=%.3f hoursAhead=%d", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, hoursAhead);
    if(fci->runHoursAfterSunrise) 
        fprintf(fci->optimizedTSFile.fp, " hoursAfterSunrise=%d", hoursAfterSunrise);    
    fprintf(fci->optimizedTSFile.fp, "\n#year,month,day,hour,min"); //groundGHI,satGHI");
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].isActive) {
            fprintf(fci->optimizedTSFile.fp, ",%s", getGenericModelName(fci, modelIndex));
        }
    }
    fprintf(fci->optimizedTSFile.fp, ",groundGHI,satGHI,optimizedGHI\n");

    weightedModelErr = &modelRun->weightedModelStats;
    weightedModelErr->sumModel_Ground_2 = 0;
    
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->isValid) {
            // instead of each model having a separate rmse, they will have a composite rmse
            weightTotal = 0;
            thisSample->optimizedGHI = 0;
            fprintf(fci->optimizedTSFile.fp, "%s", dtToStringCsv2(&thisSample->dateTime)); //, thisSample->groundGHI, thisSample->optimizedGHI);

            for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                thisModelStats = &modelRun->hourlyModelStats[modelIndex];    
                thisModelGHI = thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex];
                if(thisModelStats->isActive) {
                    fprintf(fci->optimizedTSFile.fp, ",%.0f", thisModelGHI);                    
                }
                if(thisModelStats->isActive && thisModelGHI > 0 && thisModelStats->optimizedWeightPhase2 > 0) { // make sure thisModelGHI != -999
                    weight = ((double) thisModelStats->optimizedWeightPhase2 )/100.0;
                    thisSample->optimizedGHI += (thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] * weight);
                    weightTotal += weight;               
                }
            }                    
            fprintf(fci->optimizedTSFile.fp, ",%.0f,%.0f,%.0f\n", thisSample->groundGHI, thisSample->satGHI, thisSample->optimizedGHI);
        }
    }
    
    fclose(fci->optimizedTSFile.fp);

    return True;
}

int computeHourlyRmseErrorWeighted(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int N, sampleInd, modelIndex;
    double diff;
    double weight, weightTotal;
    timeSeriesType *thisSample;
    modelStatsType *thisModelStats, *weightedModelErr;
    modelRunType *modelRun;
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

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
            thisSample->optimizedGHI = 0;
            for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                thisModelStats = &modelRun->hourlyModelStats[modelIndex];
                if(thisModelStats->isActive && thisModelStats->weight > 0) {
                    weight = ((double) thisModelStats->weight)/100.0;
                    thisSample->optimizedGHI += (thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] * weight);
                    weightTotal += weight;               
#ifdef DEBUG_2
                    if(hoursAhead == DEBUGHOUR && weight > 0.01) {
                            fprintf(stderr, "DEBUG:%s,%s=%.1f,weight=%.2f,weightedGHI=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                    getGenericModelName(fci, modelIndex),thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex],weight,thisSample->optimizedGHI);
                    }
#endif
                }
            }
/*
            if(weightTotal > 1.1) 
                fprintf(stderr, "Internal Error: model weights sum to %.2f\n", weightTotal);
*/
            
            diff = thisSample->optimizedGHI - thisSample->groundGHI;
            weightedModelErr->sumModel_Ground_2 += (diff * diff);
        }
    }
    
   
    N = modelRun->numValidSamples;   
    weightedModelErr->rmse = sqrt(weightedModelErr->sumModel_Ground_2 / N);
    weightedModelErr->rmsePct = weightedModelErr->rmse / modelRun->meanMeasuredGHI; 

//#define PRINT_ALL_WEIGHTS
#ifdef PRINT_ALL_WEIGHTS
    fprintf(stderr, "weights: ");
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        thisModelStats = &modelRun->hourlyModelStats[modelIndex];
        if(modelRun->hourlyModelStats[modelIndex].isActive) 
            fprintf(stderr, "%d ", thisModelStats->weight);
    }
    fprintf(stderr, "RSME=%.2f\n", weightedModelErr->rmsePct);
#endif
    
#ifdef DEBUG_2
    fprintf(stderr, "weightedRMSE: N=%d, sumModel_Ground_2=%.1f, totalWeights=%.1f\n", N, weightedModelErr->sumModel_Ground_2, weightTotal);
#endif
    return True;
}

// This is where we read in the model weights that were generated in a previous run
// The weights will be applied to the T/S forecast file of a particular site
// 
// Notes: 
//   this file should be parsed after the config and forecast T/S files.
//   we have to turn on isActive/isUsable flags
//   we need to check that all referenced models are legal model names

int readModelMixFile(forecastInputType *fci)
{
    char line[LINE_LENGTH], saveLine[LINE_LENGTH];
    char *fields[MAX_FIELDS];
    int numFields, expectedNumFields, i;
    char *mixModelNames[MAX_MODELS];
    int numMixModelNames=0;
 
    if(!fci->gotConfig) 
        FatalError("readModelMixFile()", "Config (model description) file not read in yet.");
    if(!fci->gotForecast) 
        FatalError("readModelMixFile()", "Forecast file not read in yet.");
    
    if((fci->modelMixFileInput.fp = fopen(fci->modelMixFileInput.fileName, "r")) == NULL) {
        fprintf(stderr, "Couldn't open model mix file %s\n", fci->modelMixFileInput.fileName);
        return FALSE;
    }
    
/*  
    modelMix files can look like:
    #siteName=Penn_State_PA,lat=40.72,lon=-77.93
    HA,ncep_GFS_sfc_DSWRF_surface_inst,NDFD_global,ecmwf_ghi,N
    42,0,34,20,0,42,2274
    48,0,30,16,0,50,2300

    or 

    #siteName=Penn_State_PA,lat=40.72,lon=-77.93
    HA,HAS,ncep_GFS_sfc_DSWRF_surface_inst,NDFD_global,cm,ecmwf_ghi,N
    1,1,76,22,0,0,81
    1,2,26,34,36,0,180
*/

    fgets(line, LINE_LENGTH, fci->modelMixFileInput.fp);
    strcpy(saveLine, line);
    // split by space then by =
    numFields = split(line, fields, MAX_FIELDS, " ");  /* split line */
    if(numFields < 3) {
        fprintf(stderr, "Error parsing %s: header line 1: expecting at least 3 fields but got %d: %s\n", fci->modelMixFileInput.fileName, numFields, saveLine);
        exit(1);
    }
    
    // not sure this is strictly necessary
//    setSiteInfo(fci, line);
//    fci->lat = atof(fields[1]);
//    fci->lon = atof(fields[2]);
    
    // now we have to parse the column names line. ex:
    // HA,HAS,ncep_GFS_sfc_DSWRF_surface_inst,NDFD_global,cm,ecmwf_ghi,N

    char *linePtr;
    fgets(line, LINE_LENGTH, fci->modelMixFileInput.fp);  
    linePtr = (*line == '#') ? line+1 : line;  // skip #, if present
    numFields = split(linePtr, fields, MAX_FIELDS, ",");  /* split line */
    expectedNumFields = numFields;  
    
    if(strcmp(fields[0], "HA") != 0) {
        sprintf(ErrStr, "Got header line without HA as first column: %s", line);
        FatalError("readModelMixFile()", ErrStr);
    }
    // now, if the next column is HAS we need to run with hour-ahead
    fci->runHoursAfterSunrise = (strcmp(fields[1], "HAS") == 0);
    
    // read model names...
    int startCol = fci->runHoursAfterSunrise ? 2 : 1;
    for(i=startCol; i<numFields; i++) {
        mixModelNames[i] = strdup(fields[i]);
        numMixModelNames++;
    }
    
    // read in weights 
    int weight, lineNumber = 3;
    int fieldCol, hoursAhead, hoursAfterSunrise;
    int hoursAheadIndex, hoursAfterSunriseIndex, modelIndex;
    int lowIndex, highIndex;
    char *modelName;
    modelRunType *modelRun;
    
    lowIndex = -1;
    highIndex = -1;
    
    while(fgets(line, LINE_LENGTH, fci->modelMixFileInput.fp)) {
        strcpy(saveLine, line);
        numFields = split(line, fields, MAX_FIELDS, ",");  /* split line */
        if(numFields != expectedNumFields) { // just make sure that all lines have the same number of fields     
            sprintf(ErrStr, "in file %s expecting %d fields but got %d at line %d\n", fci->modelMixFileInput.fileName, numFields, expectedNumFields, lineNumber);
            FatalError("readModelMixFile()", ErrStr);
        } 
        
        // need to establish correct modelIndex and hourIndex (and hourAfterSunriseIndex)
        fieldCol = 0;
        hoursAhead = atoi(fields[fieldCol++]);
        if(hoursAhead < 1 || hoursAhead > 500) {
            sprintf(ErrStr, "in file %s HA=%d is out of range at line %d", fci->modelMixFileInput.fileName, hoursAhead, lineNumber);
            FatalError("readModelMixFile()", ErrStr);
        }
        hoursAheadIndex = getHoursAheadIndex(fci, hoursAhead);
        if(lowIndex < 0) 
            lowIndex = hoursAheadIndex;
        if(hoursAheadIndex > highIndex)
            highIndex = hoursAheadIndex;
        
        if(fci->runHoursAfterSunrise) {
            hoursAfterSunrise = atoi(fields[fieldCol++]);
            if(hoursAfterSunrise < 1 || hoursAfterSunrise > 20) {
                sprintf(ErrStr, "in file %s HAS=%d is out of range at line %d", fci->modelMixFileInput.fileName, hoursAfterSunrise, lineNumber);
                FatalError("readModelMixFile()", ErrStr);
            }
            hoursAfterSunriseIndex = hoursAfterSunrise - 1;
            if(hoursAfterSunriseIndex > fci->maxHoursAfterSunrise)
                fci->maxHoursAfterSunrise = hoursAfterSunriseIndex;
        }    
        
        for(; fieldCol < numFields-1; fieldCol++) {  // HA,[HAS],<model1>,Mmodel2>,...,<modeli>,N] => only want <models>
            modelName = mixModelNames[fieldCol];
            if((modelIndex = getModelIndex(fci, modelName)) < 0)
                continue;
            weight = atoi(fields[fieldCol]);
            if(weight != -999 && (weight < 0 || weight > 100)) {
                sprintf(ErrStr, "in file %s weight=%d is out of range at line %d", fci->modelMixFileInput.fileName, weight, lineNumber);
                FatalError("readModelMixFile()", ErrStr);                
            }
            modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];              
            modelRun->hourlyModelStats[modelIndex].isReference = False;
            modelRun->hourlyModelStats[modelIndex].isActive = weight != -999;
            modelRun->hourlyModelStats[modelIndex].isUsable = (weight != -999);  // !isReference && isActive
            modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase1 = weight;  // set all the weights to this one weight
            modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase2 = weight;
            modelRun->hourlyModelStats[modelIndex].weight = weight;
        }
        lineNumber++;
    }
    
    fci->startHourLowIndex = lowIndex;
    fci->startHourHighIndex = highIndex;
    
    fprintf(stderr, "=== Finished reading weights file %s\n", fci->modelMixFileInput.fileName);
    fprintf(stderr, "=== Start hour index = %d (%d hours ahead)\n", fci->startHourLowIndex, fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead);
    fprintf(stderr, "=== End   hour index = %d (%d hours ahead)\n", fci->startHourHighIndex, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead);

    return True;
}

int getHoursAheadIndex(forecastInputType *fci, int hoursAhead)
{
    int hoursAheadIndex;
    
    for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex<=fci->startHourHighIndex; hoursAheadIndex++) {
        if(fci->hoursAheadGroup[hoursAheadIndex].hoursAhead == hoursAhead)
             return hoursAheadIndex;
    }
    return -1;
}

int getHoursAfterSunriseIndex(forecastInputType *fci, int hoursAfterSunrise)
{
    int hoursAfterSunriseIndex;
    
    for(hoursAfterSunriseIndex=0; hoursAfterSunriseIndex<=fci->startHourHighIndex; hoursAfterSunriseIndex++) {
        if(fci->hoursAfterSunriseGroup[0][hoursAfterSunriseIndex].hoursAfterSunrise == hoursAfterSunrise)
             return hoursAfterSunriseIndex;
    }
    return -1;
}

int getModelIndex(forecastInputType *fci, char *modelName)
{
    int modelIndex;
    char *currName;
    
    for(modelIndex=0; modelIndex<=fci->numModels; modelIndex++) {
        currName = getGenericModelName(fci, modelIndex);
        if(strcmp(currName, modelName) == 0)
             return modelIndex;
    }
    return -1;
}

char *parseEquals(char *inString)
{
    char *fields[MAX_FIELDS], saveStr[LINE_LENGTH];
    int numFields;
    
    strcpy(saveStr, inString);
    numFields = split(inString, fields, MAX_FIELDS, "=");
    if(numFields != 2) {
        fprintf(stderr, "Error parsing header line: can't figure siteName: %s\n", saveStr);
        return NULL;
    }
    
    return fields[1];
}

int dumpModelMixRMSE(forecastInputType *fci, int hoursAheadIndex)
{
    int N=0, sampleInd, modelIndex, hoursAfterSunriseIndex;
    double diff;
    double weight, weightTotal, meanMeasuredGHI;
    timeSeriesType *thisSample;
    modelStatsType *thisModelStats, *weightedModelErr, *satModelErr;
    modelRunType *modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][0] : &fci->hoursAheadGroup[hoursAheadIndex];   
    static char fileName[1024];
    
    int hoursAhead = fci->hoursAheadGroup[hoursAheadIndex].hoursAhead;

    // open modelMix file
    if(fci->modelMixFileOutput.fp == NULL) {
        sprintf(fileName, "%s/%s.modelMix.%s.csv", fci->outputDirectory, genProxySiteName(fci), fci->runHoursAfterSunrise ? "HA_HAS" : "HA");
        fci->modelMixFileOutput.fileName = strdup(fileName);
        fprintf(stderr, "\n ======== Generating model mix file %s\n", fci->modelMixFileOutput.fileName);
        if((fci->modelMixFileOutput.fp = fopen(fci->modelMixFileOutput.fileName, "w")) == NULL) {
            sprintf(ErrStr, "Couldn't open file %s : %s", fci->modelMixFileOutput.fileName, strerror(errno));
            FatalError("dumpModelMix()", ErrStr);
        }
 
        fprintf(fci->modelMixFileOutput.fp, "#site=%s lat=%.3f lon=%.3f\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon);
        if(fci->runHoursAfterSunrise)
            fprintf(fci->modelMixFileOutput.fp, "HA,HAS");
        else 
            fprintf(fci->modelMixFileOutput.fp, "HA");
        
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {    
            //if(!modelRun->hourlyModelStats[modelIndex].isReference) {
                fprintf(fci->modelMixFileOutput.fp, ",%s", getGenericModelName(fci, modelIndex));
            //}
        }
        fprintf(fci->modelMixFileOutput.fp, ",N\n");
    }
    
    // HA-only mode
    if(!fci->runHoursAfterSunrise) {
        fprintf(fci->modelMixFileOutput.fp, "%d", hoursAhead);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
            thisModelStats = &modelRun->hourlyModelStats[modelIndex];
            //if(!modelRun->hourlyModelStats[modelIndex].isReference) {
                if(modelRun->hourlyModelStats[modelIndex].isActive) {
                    weight = fci->skipPhase2 ? thisModelStats->optimizedWeightPhase1 : thisModelStats->optimizedWeightPhase2;
                    fprintf(fci->modelMixFileOutput.fp, ",%.0f", weight);
                }
                else
                    fprintf(fci->modelMixFileOutput.fp, ",-999");  // model is no longer active at this HA -- put a place mark in
            //}
        }
        fprintf(fci->modelMixFileOutput.fp, ",%d\n", modelRun->numValidSamples);
        fflush(fci->modelMixFileOutput.fp);                
        return True;
    }
    
    // HAS mode
    weightedModelErr = &fci->hoursAheadGroup[hoursAheadIndex].weightedModelStats; //&modelRun->weightedModelStats;
    satModelErr = &fci->hoursAheadGroup[hoursAheadIndex].satModelStats;
    weightedModelErr->sumModel_Ground_2 = weightedModelErr->rmse = weightedModelErr->rmsePct = 0;
    satModelErr->sumModel_Ground_2 = satModelErr->rmse = satModelErr->rmsePct = 0;

//#define DEBUG_HAS
    
    // now do RMSE
    if(fci->summaryFile.fp == NULL) {
        char *startDateStr = strdup(dtToStringDateOnly(&fci->startDate));
        char   *endDateStr = strdup(dtToStringDateOnly(&fci->endDate));
        sprintf(fileName, "%s/forecastSummary.HAS.%s.%s-%s.div=%d.hours=%d-%d.csv", fci->outputDirectory, genProxySiteName(fci), startDateStr, endDateStr, fci->numDivisions, fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead);
        fci->summaryFile.fileName = strdup(fileName);
        if((fci->summaryFile.fp = fopen(fci->summaryFile.fileName, "w")) == NULL) {
            sprintf(ErrStr, "Couldn't open file %s : %s", fci->summaryFile.fileName, strerror(errno));
            FatalError("printHoursAheadSummaryCsv()", ErrStr);
        }
        // print the header
        fprintf(fci->summaryFile.fp, "#site=%s lat=%.3f lon=%.3f divisions=%d start date=%s end date=%s\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, fci->numDivisions, startDateStr, endDateStr);
        fprintf(fci->summaryFile.fp, "#hoursAhead,group N,sat RMSE,p2RMSE\n");
        free(startDateStr); free(endDateStr);
    }
    
    for(hoursAfterSunriseIndex=0; hoursAfterSunriseIndex<fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
        int hoursAfterSunrise = hoursAfterSunriseIndex + 1;

        if(!filterHourlyForecastData(fci, hoursAheadIndex, hoursAfterSunriseIndex))  // must re-filter the T/S data for each HA/HAS combination
            //return False;
            continue;
        modelRun = &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex];
        fprintf(fci->modelMixFileOutput.fp, "%d,%d", hoursAhead, hoursAfterSunrise);
        // N += modelRun->numValidSamples;
        // meanMeasuredGHI += modelRun->meanMeasuredGHI;
        for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
            thisSample = &fci->timeSeries[sampleInd];
            if(thisSample->isValid) {
#ifdef DEBUG_HAS
                fprintf(stderr, "DEBUG:%s,%s,HA=%d,HAS=%d,", dtToStringCsv2(&thisSample->dateTime), thisSample->siteName, modelRun->hoursAhead, modelRun->hoursAfterSunrise);
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
                thisSample->optimizedGHI = 0;
                
                N++;
                meanMeasuredGHI += thisSample->groundGHI;
                
                for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                    thisModelStats = &modelRun->hourlyModelStats[modelIndex];
                    if(modelRun->hourlyModelStats[modelIndex].isActive) {
                        weight = fci->skipPhase2 ? thisModelStats->optimizedWeightPhase1 : thisModelStats->optimizedWeightPhase2;
                        weight /= 100.0;  // weights are kept as ints 0-100; make 24 => .24
                        // add up weight * GHI for each included forecast model
                        if(thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] < 0) {
                            fprintf(stderr, "Problem: trying to add in a negative GHI: %.1f [%s, hoursAhead=%d, model=%s]\n", thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex],
                                    dtToStringCsv2(&thisSample->dateTime), hoursAhead, thisModelStats->columnName);
                        }
                        thisSample->optimizedGHI += (thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] * weight);
                        weightTotal += weight;               
    #ifdef DEBUG_HAS
                        //if(hoursAhead == DEBUGHOUR && weightedModelErr->weight > 0.01*/) {
/*
                        if(weight > 0.01)
                            fprintf(stderr, "%s=%.1f * %.2f,", 
                                    getGenericModelName(fci, modelIndex),thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex],weight);
                        //}
*/
    #endif
                    }
                }
#ifdef DEBUG_HAS
                fprintf(stderr, "weightedGHI=%.1f,groundGHI=%.1f\n",thisSample->optimizedGHI, thisSample->groundGHI);
#endif
                if(weightTotal > 1.5 || weightTotal < 0.5) 
                    fprintf(stderr, "Internal Error: model weights sum to %.2f\n", weightTotal);

                diff = thisSample->optimizedGHI - thisSample->groundGHI;
                weightedModelErr->sumModel_Ground_2 += (diff * diff);
                diff = thisSample->satGHI - thisSample->groundGHI;
                satModelErr->sumModel_Ground_2 += (diff * diff);
            }
        }
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
            thisModelStats = &modelRun->hourlyModelStats[modelIndex];
            if(modelRun->hourlyModelStats[modelIndex].isActive) {
                weight = fci->skipPhase2 ? thisModelStats->optimizedWeightPhase1 : thisModelStats->optimizedWeightPhase2;
                fprintf(fci->modelMixFileOutput.fp, ",%.0f", weight);
            }
            else
                fprintf(fci->modelMixFileOutput.fp, ",-999");  // model is no longer active at this HA -- put a place mark in
        }
        fprintf(fci->modelMixFileOutput.fp, ",%d\n", modelRun->numValidSamples);
        fflush(fci->modelMixFileOutput.fp);        
    }
          
    //N = modelRun->numValidSamples;   
    meanMeasuredGHI /= N;
    weightedModelErr->rmse = sqrt(weightedModelErr->sumModel_Ground_2 / N);
    weightedModelErr->rmsePct = weightedModelErr->rmse / meanMeasuredGHI; 
    satModelErr->rmse = sqrt(satModelErr->sumModel_Ground_2 / N);
    satModelErr->rmsePct = satModelErr->rmse / meanMeasuredGHI;
    
    fprintf(stderr, "HA=%d/HAS=1..%d RMSE: N=%d, sumModel_Ground_2=%.1f, meanMeasuredGHI=%.1f, RMSE=%.1f, %%RMSE = %.02f\n", 
            hoursAhead, fci->maxHoursAfterSunrise, N, weightedModelErr->sumModel_Ground_2, meanMeasuredGHI, weightedModelErr->rmse, weightedModelErr->rmsePct*100);
    fprintf(fci->summaryFile.fp, "%d,%d,%.1f,%.1f\n", hoursAhead, N, satModelErr->rmsePct*100, weightedModelErr->rmsePct*100);
    fflush(fci->summaryFile.fp);                

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
    
    sprintf(tempFileName, "%s/%s.numModelsReporting.csv", fci->outputDirectory, genProxySiteName(fci));
    fci->modelsAttendenceFile.fileName = strdup(tempFileName);
    
    if((fci->modelsAttendenceFile.fp = fopen(fci->modelsAttendenceFile.fileName, "w")) == NULL) {
        sprintf(ErrStr, "Couldn't open file %s: %s", fci->modelsAttendenceFile.fileName, strerror(errno));
        FatalError("dumpNumModelsReportingTable()", ErrStr);
    }
    
    fprintf(fci->modelsAttendenceFile.fp, "#Number of Models Reporting for site %s, lat=%.3f, lon=%.3f, ha='hours ahead'\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon);
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
