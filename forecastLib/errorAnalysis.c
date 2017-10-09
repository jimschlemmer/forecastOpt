#include "forecastOpt.h"
#include "forecastOptUtils.h"

typedef enum
{
    SatGHI, CorrGHI, OptGHI
} mbeSelectType;

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
int correctOptimizedGHI(forecastInputType *fci, int hoursAheadIndex);
int cmpDouble(const void *x, const void *y);
//int computeModelRMSE(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex);
double computeMBEcustom(forecastInputType *fci, mbeSelectType which, int hoursAheadIndex);
double computeMAEcustom(forecastInputType *fci, mbeSelectType which, int hoursAheadIndex);

void clearHourlyErrorFields(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int modelIndex;
    modelRunType *modelRun;
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    modelRun->meanMeasuredGHI = modelRun->numValidSamples = modelRun->ground_N = 0;

    //fprintf(stderr, "Clearing stats fields for hour %d\n", hoursAheadIndex);
    // zero out all statistical values
    clearModelStats(&modelRun->satModelStats);
    clearModelStats(&modelRun->weightedModelStatsVsGround);

    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++)
        clearModelStats(&modelRun->hourlyModelStats[modelIndex]);
}

void clearModelStats(modelStatsType *thisModelStats)
{
    thisModelStats->sumModel_Ground = thisModelStats->sumAbs_Model_Ground = thisModelStats->sumModel_Ground_2 = 0;
    thisModelStats->sumModel_Sat = thisModelStats->sumAbs_Model_Sat = thisModelStats->sumModel_Sat_2 = 0;
    thisModelStats->mae = thisModelStats->mbe = thisModelStats->rmse = 0;
    thisModelStats->maePct = thisModelStats->mbePct = thisModelStats->rmsePct = 0;
    thisModelStats->N = 0;
}

// this function sets the groupIsValid flag
// hoursAfterSunriseIndex == -1 means filter all data (across all HAS bins)

int filterForecastData(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int sampleInd, modelIndex, hoursAfterSunriseOK;
    double thisGHI;
    timeSeriesType *thisSample;
    modelRunType *modelRunStats, *modelRunData;

    // reset everything that is modified by this function
    clearHourlyErrorFields(fci, hoursAheadIndex, hoursAfterSunriseIndex);

    /*
        if(fci->modelPermutations.currentPermutationIndex == 31) {
            fprintf(stderr, "stop\n");
        }
     */
    //modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
    if(hoursAfterSunriseIndex < 0) {
        modelRunStats = &fci->hoursAheadGroup[hoursAheadIndex];
    }
    else {
        modelRunStats = &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex];
    }

    int hoursAhead = fci->hoursAheadGroup[hoursAheadIndex].hoursAhead;
    int hoursAfterSunrise = hoursAfterSunriseIndex + 1;

    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++)
        modelRunStats->hourlyModelStats[modelIndex].N = 0;

    // for all eligible samples...
    for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];

        thisSample->forecastData[hoursAheadIndex].groupIsValid = OK; // innocent until proven guilty

        // short circuit on zenith cutoff
        if(thisSample->zenith > 90) {
            thisSample->forecastData[hoursAheadIndex].groupIsValid = zenith;
            continue;
        }

        hoursAfterSunriseOK = (hoursAfterSunriseIndex < 0 || (thisSample->hoursAfterSunrise == hoursAfterSunrise));

        if(!hoursAfterSunriseOK) { // reject all samples that are not in the hoursAfterSunrise hour
            thisSample->forecastData[hoursAheadIndex].groupIsValid = notHAS;
            continue;
        }

        // filter via sat model  
        if(fci->filterWithSatModel && thisSample->satGHI < 5) {
            if(thisSample->sunIsUp && thisSample->zenith < 85)
                fprintf(fci->warningsFile.fp, "%s : bad sample: sat model, hoursAhead=%d hoursAfterSunrise=%d: GHI = %.1f, zenith = %.1f\n",
                    dtToStringCsv2(&thisSample->dateTime), hoursAhead, hoursAfterSunrise, thisSample->satGHI, thisSample->zenith);

            thisSample->forecastData[hoursAheadIndex].groupIsValid = satLow;
            //continue;
        }
        else
            modelRunStats->satModelStats.N++;
        //

        //#define DUMP_FLT
#ifdef DUMP_FLT
        // filter via switched on models
        fprintf(stderr, "FLT[HA%d/HAS%d]: %s", hoursAhead, hoursAfterSunrise, dtToStringCsv2(&thisSample->dateTime));
#endif
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            if(hoursAfterSunriseIndex < 0)
                modelRunData = &fci->hoursAfterSunriseGroup[hoursAheadIndex][thisSample->hoursAfterSunrise - 1];
            else
                modelRunData = &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex];


#ifdef DUMP_FLT
            if(modelRunData->hourlyModelStats[modelIndex].maskSwitchOn)
                fprintf(stderr, ",%s=%.0f", getModelName(fci, modelIndex), thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex]);
#endif
            if(modelRunData->hourlyModelStats[modelIndex].maskSwitchOn) { // maskSwitchOn because we want forecast and reference models
                thisGHI = thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex];
                if(thisGHI < 5) {
                    //#ifdef DEBUG1
                    if(thisSample->sunIsUp && thisSample->zenith < 85)
                        fprintf(fci->warningsFile.fp, "%s : bad sample: model %s, modelIndex=%d, hoursAhead=%d hoursAfterSunrise=%d: GHI = %.1f, zenith = %.1f\n",
                            dtToStringCsv2(&thisSample->dateTime), getModelName(fci, modelIndex), modelIndex, hoursAhead, hoursAfterSunrise, thisGHI, thisSample->zenith);
                    //#endif                
                    thisSample->forecastData[hoursAheadIndex].groupIsValid = nwpLow;
                    // break;
                }
                else { // if this model's GHI didn't trigger an groupIsValid=False, increment N
                    //#define DEBUG1
#ifdef DEBUG1
                    if(modelRunData->hoursAhead == 6) fprintf(stderr, "?%s HA%d/HAS%d good sample: model %s: GHI = %.1f\n",
                            dtToStringCsv2(&thisSample->dateTime), modelRunData->hoursAhead, hoursAfterSunriseIndex + 1, getModelName(fci, modelIndex), thisGHI);
#endif                                   
                    modelRunStats->hourlyModelStats[modelIndex].N++;
                }
            }
        }
#ifdef DUMP_FLT
        fprintf(stderr, " [valid=%s]\n", validString(thisSample->forecastData[hoursAheadIndex].groupIsValid));
#endif
        // filter via ground GHI
        if(thisSample->groundGHI < MIN_GHI_VAL) {
            if(thisSample->sunIsUp && thisSample->zenith < 85)
                fprintf(fci->warningsFile.fp, "%s : bad sample: ground data, hoursAhead=%d hoursAfterSunrise=%d: GHI = %.1f, zenith = %.1f\n",
                    dtToStringCsv2(&thisSample->dateTime), hoursAhead, hoursAfterSunrise, thisSample->groundGHI, thisSample->zenith);
            thisSample->forecastData[hoursAheadIndex].groupIsValid = groundLow;
        }
        else {
            modelRunStats->ground_N++;
        }

        if(thisSample->forecastData[hoursAheadIndex].groupIsValid == OK) {
            modelRunStats->meanMeasuredGHI += thisSample->groundGHI;
            modelRunStats->numValidSamples++;
#ifdef DEBUG1
            if(modelRunStats->hoursAhead == 6) fprintf(stderr, "?%s hours ahead: %d:  all good: meanMeasuredGHI=%.1f numValidSamples=%d\n",
                    dtToStringCsv2(&thisSample->dateTime), modelRunStats->hoursAhead, modelRunStats->meanMeasuredGHI, modelRunStats->numValidSamples);
#endif
        }
    }

    if(modelRunStats->numValidSamples < 1) {
        fprintf(fci->warningsFile.fp, "filterForecastData(): for hour index %d (%d hoursAhead, %d hoursAfterSunrise): got too few valid points to work with", hoursAheadIndex, hoursAhead, hoursAfterSunrise);
        //FatalError("computeModelRMSE()", "Too few valid data points to work with.");
        fflush(fci->warningsFile.fp);
        return False;
    }

#ifdef DEBUG
    if(hoursAhead == DEBUGHOUR) {
        fprintf(stderr, "DBDUMP:year,month,day,hour,min,groupgroupIsValid,groundValid,satValid,validModels..\n");
        int count = 0;
        for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
            thisSample = &fci->timeSeries[sampleInd];
#ifdef YESNO
            fprintf(stderr, "DBDUMP:%s,%d,%s,%s,%s", dtToStringCsv2(&thisSample->dateTime), count, thisSample->groupIsValid ? "yes" : "no",
                    thisSample->groundGHI > MIN_GHI_VAL ? "yes" : "no", thisSample->satGHI > MIN_GHI_VAL ? "yes" : "no");
            for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
                if(modelRunStats->hourlyModelStats[modelIndex].isActive) {
                    thisGHI = getModelGHI(modelIndex);
                    if(thisGHI > 5) {
                        fprintf(stderr, ",yes");
                    }
                    else {
                        fprintf(stderr, ",no [%s=%.1f]", getModelName(fci, modelIndex), thisGHI);
                    }
                }
            }
#else
            fprintf(stderr, "DBDUMP:%s,%d,%s,%.0f,%.0f", dtToStringCsv2(&thisSample->dateTime), count, thisSample->groupIsValid ? "yes" : "no",
                    thisSample->groundGHI, thisSample->satGHI);
            for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
                if(modelRunStats->hourlyModelStats[modelIndex].isActive) {
                    fprintf(stderr, ",%.0f", getModelGHI(modelIndex));
                }
            }
#endif            
            if(thisSample->groupIsValid)
                count++;
            fprintf(stderr, "\n");
        }
    }
#endif

    modelRunStats->meanMeasuredGHI /= modelRunStats->numValidSamples;

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

int computeModelRMSE(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    clearHourlyErrorFields(fci, hoursAheadIndex, hoursAfterSunriseIndex);

    if(!filterForecastData(fci, hoursAheadIndex, hoursAfterSunriseIndex))
        return False;
    //if(!computeHourlyBiasErrors(fci, hoursAheadIndex, hoursAfterSunriseIndex))
    //    return False;
    if(!computeHourlyRmseErrors(fci, hoursAheadIndex, hoursAfterSunriseIndex))
        return False;

    return True;
}

double computeMBEcustom(forecastInputType *fci, mbeSelectType which, int hoursAheadIndex)
{
    double diff, sumModel_Ground = 0, mbe, val;
    timeSeriesType *thisSample;
    double numValidSamples = 0;
    int sampleInd;
#ifdef DEBUG
    int count = 0, hoursAhead = modelRun->hoursAhead;
#endif

    for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->forecastData[hoursAheadIndex].groupIsValid == OK) {
            switch(which) {
                case SatGHI: val = thisSample->satGHI;
                    break;
                case OptGHI: val = thisSample->forecastData[hoursAheadIndex].optimizedGHI;
                    break;
                case CorrGHI: val = thisSample->forecastData[hoursAheadIndex].correctedOptimizedGHI;
                    break;
                default: fprintf(stderr, "Internal error in computeMBEcustom()");
                    exit(1);
            }
            diff = val - thisSample->groundGHI;
            sumModel_Ground += diff;
            numValidSamples += 1;
        }
    }

    mbe = sumModel_Ground / numValidSamples;

    return mbe;
}

double computeMAEcustom(forecastInputType *fci, mbeSelectType which, int hoursAheadIndex)
{
    double diff, sumModel_Ground = 0, mae, val;
    timeSeriesType *thisSample;
    double numValidSamples = 0;
    int sampleInd;
#ifdef DEBUG
    int count = 0, hoursAhead = modelRun->hoursAhead;
#endif

    for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->forecastData[hoursAheadIndex].groupIsValid == OK) {
            switch(which) {
                case SatGHI: val = thisSample->satGHI;
                    break;
                case OptGHI: val = thisSample->forecastData[hoursAheadIndex].optimizedGHI;
                    break;
                case CorrGHI: val = thisSample->forecastData[hoursAheadIndex].correctedOptimizedGHI;
                    break;
                default: fprintf(stderr, "Internal error in computeMAEcustom()");
                    exit(1);
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
#ifdef nDEBUG
    int firstTime = True;
#endif    
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

#ifdef DEBUG
    int count = 0, hoursAhead = modelRun->hoursAhead;
#endif

    for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->forecastData[hoursAheadIndex].groupIsValid == OK) {

#ifdef nDEBUG
            if(firstTime) {
                int j;
                fprintf(stderr, "DEBUG:\n#hoursAheadIndex=%d\n", hoursAheadIndex);
                fprintf(stderr, "DEBUG:#year,month,day,hour,minute");
                for(j = 0; j < fci->numModels; j++)
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
            for(modelIndex = -1; modelIndex < fci->numModels; modelIndex++) {
                thisModelStats = NULL;
                if(modelIndex < 0) {
                    diff = thisSample->satGHI - thisSample->groundGHI;
#ifdef DEBUG
                    if(hoursAhead == DEBUGHOUR) {
                        fprintf(stderr, "DEBUG:%s,satGHI=%.1f,grndGHI=%.1f,diff=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                thisSample->satGHI, thisSample->groundGHI, diff);
                    }
#endif
                    thisModelStats = &modelRun->satModelStats;
                }
                else {
                    if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
                        diff = thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] - thisSample->groundGHI;
#ifdef DEBUG
                        if(hoursAhead == DEBUGHOUR) {
                            fprintf(stderr, "DEBUG:%s,%s=%.1f,grndGHI=%.1f,diff=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                    getModelName(fci, modelIndex), thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex], thisSample->groundGHI, diff);
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
                fprintf(stderr, ",%.1f,%.1f,%.1f", diff, fabs(diff), diff * diff);
#endif
            }
        }
    }

    N = modelRun->numValidSamples;
    for(modelIndex = -1; modelIndex < fci->numModels; modelIndex++) {
        thisModelStats = NULL;
        if(modelIndex < 0)
            thisModelStats = &modelRun->satModelStats;
        else {
            if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn)
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
//#define nDEBUG

int computeHourlyRmseErrors(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int N, sampleInd, modelIndex;
    double diff;
    timeSeriesType *thisSample;
    modelStatsType *thisModelStats;
    modelRunType *modelRun;
#ifdef nDEBUG
    int firstTime = True;
#endif
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
#ifdef DEBUG
    int count = 0, hoursAhead = modelRun->hoursAhead;
#endif

    for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->forecastData[hoursAheadIndex].groupIsValid == OK) {
#ifdef nDEBUG
            if(firstTime) {
                int j;
                fprintf(stderr, "DEBUG:\n#hoursAheadIndex=%d, hoursAfterSunriseIndex=%d\n", hoursAheadIndex, hoursAfterSunriseIndex);
                fprintf(stderr, "DEBUG:#year,month,day,hour,minute");
                for(j = 0; j < fci->numModels; j++)
                    fprintf(stderr, ",%s - ground,abs(%s - ground),(%s - ground)^2", getModelName(fci, j), getModelName(fci, j), getModelName(fci, j));
                firstTime = False;
                fprintf(stderr, "\n");
            }
            fprintf(stderr, "DEBUG:%s", dtToStringCsv2(&thisSample->dateTime));
#endif
#ifdef DEBUG
            if(hoursAhead == DEBUGHOUR)
                fprintf(stderr, "DEBUG:group %d\n", count++);
#endif
            for(modelIndex = -1; modelIndex < fci->numModels; modelIndex++) {
                thisModelStats = NULL;
                if(modelIndex < 0) {
                    diff = thisSample->satGHI - thisSample->groundGHI;
#ifdef DEBUG
                    if(hoursAhead == DEBUGHOUR) {
                        fprintf(stderr, "DEBUG:%s,satGHI=%.1f,grndGHI=%.1f,diff=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                thisSample->satGHI, thisSample->groundGHI, diff);
                    }
#endif
                    thisModelStats = &modelRun->satModelStats;
                }
                else {
                    if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
                        diff = thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] - thisSample->groundGHI;
#ifdef DEBUG
                        if(hoursAhead == DEBUGHOUR) {
                            fprintf(stderr, "DEBUG:%s,%s=%.1f,grndGHI=%.1f,diff=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                    getModelName(fci, modelIndex), thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex], thisSample->groundGHI, diff);
                        }
#endif
                        thisModelStats = &modelRun->hourlyModelStats[modelIndex];
                        
                        // compute target model's kt -- currently ECMWF
                        if(modelIndex == fci->ktModelColumn) {
                            if(thisSample->clearskyGHI < 5)
                                thisSample->forecastData[hoursAheadIndex].ktTargetNWP = 0;
                            else
                                thisSample->forecastData[hoursAheadIndex].ktTargetNWP = thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] / thisSample->clearskyGHI;
                        }
                    }
                }
                if(thisModelStats) {
                    thisModelStats->sumModel_Ground_2 += (diff * diff);
                }
#ifdef nDEBUG                
                fprintf(stderr, ",%.1f,%.1f,%.1f", diff, fabs(diff), diff * diff);
#endif
            }
#ifdef nDEBUG
            fprintf(stderr, "\n");
#endif
        }
    }

    N = modelRun->numValidSamples;

    for(modelIndex = -1; modelIndex < fci->numModels; modelIndex++) {
        thisModelStats = NULL;
        if(modelIndex < 0)
            thisModelStats = &modelRun->satModelStats;
        else {
            if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn)
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
    modelStatsType *thisModelStats, *weightedModelStatsVsGround;
    modelRunType *modelRun;
    char fileName[1024];
    int hoursAfterSunrise, hoursAhead = fci->hoursAheadGroup[hoursAheadIndex].hoursAhead;

    // open T/S file and print header
    if(fci->runHoursAfterSunrise)
        sprintf(fileName, "%s/%s.optimizedTS.HAS.HA=%03d.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), hoursAhead, fci->modelPermutations.currentPermutationIndex);
    else
        sprintf(fileName, "%s/%s.optimizedTS.HA=%03d.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), hoursAhead, fci->modelPermutations.currentPermutationIndex);

    /*****
     * When we run the final stats using the weights just generated, we want to use all HAS in the current HA & permutation, so as to 
     * simulate an actual run of forecastRun.  We still need to grab the just generated weights according to current HA/HAS.
     *****/

    fci->optimizedTSFile.fileName = strdup(fileName);
    fprintf(stderr, "\n ======== Generating optimized T/S file %s\n", fci->optimizedTSFile.fileName);
    if((fci->optimizedTSFile.fp = fopen(fci->optimizedTSFile.fileName, "w")) == NULL) {
        sprintf(ErrStr, "Couldn't open file %s : %s", fci->optimizedTSFile.fileName, strerror(errno));
        FatalError("dumpHourlyOptimizedTS()", ErrStr);
    }
    // print the header
    fprintf(fci->optimizedTSFile.fp, "#site=%s lat=%.3f lon=%.3f hoursAhead=%d HAS=%s date span=%s-%s\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, hoursAhead, fci->runHoursAfterSunrise ? "yes" : "no",
            fci->startDateStr, fci->endDateStr);
    fprintf(fci->optimizedTSFile.fp, "#year,month,day,hour,%s", fci->runHoursAfterSunrise ? "min,HAS,groupIsValid" : "min,groupIsValid"); //groundGHI,satGHI");

    // this is to pick out the names of models that are active according to the current permutation
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][0] : &fci->hoursAheadGroup[hoursAheadIndex];
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
            fprintf(fci->optimizedTSFile.fp, ",%s,%s_wt", getModelName(fci, modelIndex), getModelName(fci, modelIndex));
        }
    }
    fprintf(fci->optimizedTSFile.fp, ",groundGHI,satGHI,optimizedGHI,clearskyGHI\n");

    weightedModelStatsVsGround = &modelRun->weightedModelStatsVsGround;
    weightedModelStatsVsGround->sumModel_Ground_2 = 0;

    if(!filterForecastData(fci, hoursAheadIndex, -1)) { // must re-filter the T/S data to turn on all HAS groupIsValid bits (otherwise we end up with only the last HAS T/S data active)
        fprintf(stderr, "!!! Warning: found no filtered data from hoursAheadIndex/hoursAhead = %d/%d\n", hoursAheadIndex, hoursAhead);
        return False;
    }

    // compute optimizedGHI, ktClearsky, ktOptimizedGHI
    for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->sunIsUp && thisSample->forecastData[hoursAheadIndex].groupIsValid == OK) {
            // instead of each model having a separate rmse, they will have a composite rmse
            weightTotal = 0;
            thisSample->forecastData[hoursAheadIndex].optimizedGHI = 0;
            modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][thisSample->hoursAfterSunrise - 1] : &fci->hoursAheadGroup[hoursAheadIndex];

            for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
                thisModelStats = &modelRun->hourlyModelStats[modelIndex];
                if(thisModelStats->maskSwitchOn && thisModelStats->optimizedWeightPhase2 > 0) {
                    weight = ((double) thisModelStats->optimizedWeightPhase2) / 100.0;
                    thisSample->forecastData[hoursAheadIndex].optimizedGHI += (thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] * weight);
                    weightTotal += weight;
                }
            }

            if(thisSample->clearskyGHI > 10) {
                thisSample->forecastData[hoursAheadIndex].ktSatGHI = thisSample->satGHI / thisSample->clearskyGHI;
                thisSample->forecastData[hoursAheadIndex].ktOptimizedGHI = thisSample->forecastData[hoursAheadIndex].optimizedGHI / thisSample->clearskyGHI;
                /*
                                if(thisSample->ktSatGHI > 1.01 || thisSample->ktOptimizedGHI > 1.01)
                                    fprintf(stderr, "Badval\n");
                 */
            }
        }
    }

    /*
        if(0)
            correctOptimizedGHI(fci, hoursAheadIndex);
     */

    // now print it all out to the TS output file
    for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
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
            // print HAS if needed
            if(fci->runHoursAfterSunrise)
                fprintf(fci->optimizedTSFile.fp, ",%d", thisSample->hoursAfterSunrise);

            // print the groupIsValid value
            fprintf(fci->optimizedTSFile.fp, ",%s", validString(thisSample->forecastData[hoursAheadIndex].groupIsValid));

            // foreach model, print GHI,weight
            for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
                thisModelStats = &modelRun->hourlyModelStats[modelIndex];
                if(thisModelStats->maskSwitchOn) {
                    fprintf(fci->optimizedTSFile.fp, ",%.0f", thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex]);
                    fprintf(fci->optimizedTSFile.fp, ",%d", thisModelStats->optimizedWeightPhase2);
                }
            }

            // now print the ground data, satellite GHI and optimized GHI
            fprintf(fci->optimizedTSFile.fp, ",%.0f,%.0f,%.0f,%.0f\n", thisSample->groundGHI, thisSample->satGHI,
                    (thisSample->forecastData[hoursAheadIndex].groupIsValid == OK) ? thisSample->forecastData[hoursAheadIndex].optimizedGHI : -999,
                    thisSample->clearskyGHI);
        }
    }

    fclose(fci->optimizedTSFile.fp);

    return True;
}

//#define DEBUG_CORRECTION

int correctOptimizedGHI(forecastInputType *fci, int hoursAheadIndex)
{
    int sampleInd;
    timeSeriesType *thisSample;
    modelRunType *modelRun;
    static double *sortedKtSatGHI = NULL, *sortedKtOptimizedGHI = NULL;
    static int sortedSize = 0;
    double satMAE, satMBE, optMAEpre, optMBEpre, optMAEpost, optMBEpost;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][0] : &fci->hoursAheadGroup[hoursAheadIndex];

    if(sortedKtSatGHI == NULL) {
        sortedSize = fci->numTotalSamples;
        if((sortedKtSatGHI = (double *) malloc(sizeof (double) * sortedSize)) == NULL)
            FatalError("correctOptimizedGHI()", "Memory allocation error");
        if((sortedKtOptimizedGHI = (double *) malloc(sizeof (double) * sortedSize)) == NULL)
            FatalError("correctOptimizedGHI()", "Memory allocation error");
    }
    else if(sortedSize != fci->numTotalSamples) {
        fprintf(stderr, "numSamples changed from %d to %d\n", sortedSize, fci->numTotalSamples);
        FatalError("correctOptimizedGHI()", "bye");

    }

    if(fci->correctionStatsFile.fp == NULL) {
        char tempFileName[1024];
        sprintf(tempFileName, "%s/correctionStats.%s.%s-%s.div=%d.hours=%d-%d.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), dtToStringDateOnly(&fci->startDate), dtToStringDateOnly(&fci->endDate), fci->numDivisions, fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead, fci->modelPermutations.currentPermutationIndex);
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
    for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        sortedKtSatGHI[sampleInd] = 0;
        sortedKtOptimizedGHI[sampleInd] = 0;
        if(thisSample->sunIsUp && thisSample->forecastData[hoursAheadIndex].groupIsValid == OK && thisSample->clearskyGHI > 200) {
            sortedKtSatGHI[sampleInd] = round(thisSample->satGHI) / thisSample->clearskyGHI;
            sortedKtOptimizedGHI[sampleInd] = round(thisSample->forecastData[hoursAheadIndex].optimizedGHI) / thisSample->clearskyGHI;
        }
#ifdef DEBUG_CORRECTION
        fprintf(stderr, "%s,%.0f,%.0f,%.0f,%.4f,%.4f\n", dtToStringCsv2(&thisSample->dateTime), thisSample->clearskyGHI, thisSample->satGHI, thisSample->optimizedGHI, sortedKtSatGHI[sampleInd], sortedKtOptimizedGHI[sampleInd]);
#endif
    }
    // now sort them
    //qsort(arr, sizeof(arr)/sizeof(arr[0]), sizeof(arr[0]), cmp);
    qsort(sortedKtSatGHI, sortedSize, sizeof (double), cmpDouble);
    qsort(sortedKtOptimizedGHI, sortedSize, sizeof (double), cmpDouble);

    // get N
    int i;
    double N = 0, sumSat, sumOpt;
    for(i = 0; i < sortedSize; i++) {
        if(sortedKtSatGHI[i] < 0.95) {
            N = i + 1;
            break;
        }
        sumSat += sortedKtSatGHI[i];
        sumOpt += sortedKtOptimizedGHI[i];
#ifdef DEBUG_CORRECTION
        //fprintf(stderr, "%d : satKt=%.4f optKt=%.4f\n", i, sortedKtSatGHI[i], sortedKtOptimizedGHI[i]);
        fprintf(stderr, "%.4f,%.4f,%.8f,%.8f\n", sortedKtSatGHI[i], sortedKtOptimizedGHI[i], sortedKtSatGHI[i], sortedKtOptimizedGHI[i]);
#endif
    }
    if(N < 1) {
        sprintf(ErrStr, "Internal error: N=0, sortedSize=%d\n", sortedSize);
        FatalError("correctOptimizedGHI()", ErrStr);
    }

    // calculate average sortedKtSatGHI and sortedKtOptimizedGHI for first N values    
    double SatHiAvg = sumSat / N;
    double OptHiAvg = sumOpt / N;
    //#define HARD_WIRED_A_B      
#ifdef HARD_WIRED_A_B
    modelRun->correctionVarA = 1.04;

#else
    modelRun->correctionVarA = SatHiAvg / OptHiAvg;
#endif
#ifdef DEBUG_CORRECTION
    fprintf(stderr, "\nN=%.0f SatHiAvg = %.4f OptHiAvg = %.4f correctionVarA = %.4f\n", N, SatHiAvg, OptHiAvg, modelRun->correctionVarA);
#endif

    // iterate MBE calculation 
    // int computeHourlyBiasErrors(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)


    satMAE = computeMAEcustom(fci, SatGHI, hoursAheadIndex);
    satMBE = computeMBEcustom(fci, SatGHI, hoursAheadIndex);
    optMAEpre = computeMAEcustom(fci, OptGHI, hoursAheadIndex);
    optMBEpre = computeMBEcustom(fci, OptGHI, hoursAheadIndex);

    // COR = B + (A-B) * optIndex
    // X = MIN(1.025, COR * optIndex)
    // GHICor = X * GHIclear
    modelRun->correctionVarB = 1.01;

    do {
#ifdef HARD_WIRED_A_B
        modelRun->correctionVarB = 0.7; // Temporary test
#else
        modelRun->correctionVarB -= 0.01; // decrement B
#endif

        for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) { // using A & B recompute TS optGHI as correctedOptimizedGHI
            thisSample = &fci->timeSeries[sampleInd];
            if(thisSample->sunIsUp && thisSample->forecastData[hoursAheadIndex].groupIsValid == OK && thisSample->clearskyGHI > 10) {
                double COR = modelRun->correctionVarB + (modelRun->correctionVarA - modelRun->correctionVarB) * thisSample->forecastData[hoursAheadIndex].ktOptimizedGHI;
                double X = MIN(1.025, COR * thisSample->forecastData[hoursAheadIndex].ktOptimizedGHI);
                thisSample->forecastData[hoursAheadIndex].correctedOptimizedGHI = X * thisSample->clearskyGHI;
#ifdef DEBUG_CORRECTION_A
                fprintf(stderr, "A=%.4f B=%.4f ktOpt=%.4f COR=%.4f X=%.4f clearGHI=%.1f corrOptGHI=%.1f\n", modelRun->correctionVarA, modelRun->correctionVarB, thisSample->ktOptimizedGHI, COR, X, thisSample->clearskyGHI, thisSample->correctedOptimizedGHI);
#endif
            }
        }

        optMAEpost = computeMAEcustom(fci, CorrGHI, hoursAheadIndex);
        optMBEpost = computeMBEcustom(fci, CorrGHI, hoursAheadIndex);
#ifdef DEBUG_CORRECTION
        fprintf(stderr, "A=%.3f B=%.4f satMBE=%.3f optMBE=%.3f\n", modelRun->correctionVarA, modelRun->correctionVarB, satMBE, optMBEpost);
#endif

#ifdef HARD_WIRED_A_B        
    } while(0 && optMBEpost > satMBE && modelRun->correctionVarB > 0);
#else
    } while(optMBEpost > satMBE && modelRun->correctionVarB > 0);
#endif

    fprintf(fci->correctionStatsFile.fp, "%s,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", genProxySiteName(fci), modelRun->hoursAhead, modelRun->correctionVarA,
            modelRun->correctionVarB, satMAE, satMBE, optMAEpre, optMBEpre, optMAEpost, optMBEpost);

    return True;
}

int cmpDouble(const void *x, const void *y)
{
    double xx = *(double*) x, yy = *(double*) y;
    if(xx > yy) return -1;
    if(xx < yy) return 1;
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
        sprintf(fileName, "%s/%s.optimizedTS.HA=%03d_HAS=%03d.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), hoursAhead, hoursAfterSunrise, fci->modelPermutations.currentPermutationIndex);
    else
        sprintf(fileName, "%s/%s.optimizedTS.HA=%03d.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), hoursAhead, fci->modelPermutations.currentPermutationIndex);
    fci->optimizedTSFile.fileName = strdup(fileName);
    fprintf(stderr, "\n ======== Generating optimized T/S file %s\n", fci->optimizedTSFile.fileName);
    if((fci->optimizedTSFile.fp = fopen(fci->optimizedTSFile.fileName, "w")) == NULL) {
        sprintf(ErrStr, "Couldn't open file %s : %s", fci->optimizedTSFile.fileName, strerror(errno));
        FatalError("dumpHourlyOptimizedTS_HAS()", ErrStr);
    }
    // print the header
    fprintf(fci->optimizedTSFile.fp, "#site=%s lat=%.3f lon=%.3f hoursAhead=%d date span=%s-%s", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, hoursAhead, fci->startDateStr, fci->endDateStr);
    if(fci->runHoursAfterSunrise)
        fprintf(fci->optimizedTSFile.fp, " hoursAfterSunrise=%d", hoursAfterSunrise);
    fprintf(fci->optimizedTSFile.fp, "\n#year,month,day,hour,min"); //groundGHI,satGHI");
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
            fprintf(fci->optimizedTSFile.fp, ",%s", getModelName(fci, modelIndex));
        }
    }
    fprintf(fci->optimizedTSFile.fp, ",groundGHI,satGHI,optimizedGHI\n");

    weightedModelErr = &modelRun->weightedModelStatsVsGround;
    weightedModelErr->sumModel_Ground_2 = 0;

    for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->forecastData[hoursAheadIndex].groupIsValid == OK) {
            // instead of each model having a separate rmse, they will have a composite rmse
            weightTotal = 0;
            thisSample->forecastData[hoursAheadIndex].optimizedGHI = 0;
            fprintf(fci->optimizedTSFile.fp, "%s", dtToStringCsv2(&thisSample->dateTime)); //, thisSample->groundGHI, thisSample->optimizedGHI);

            for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
                thisModelStats = &modelRun->hourlyModelStats[modelIndex];
                thisModelGHI = thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex];
                if(thisModelStats->maskSwitchOn) {
                    fprintf(fci->optimizedTSFile.fp, ",%.0f", thisModelGHI);
                }
                if(thisModelStats->maskSwitchOn && thisModelGHI > 0 && thisModelStats->optimizedWeightPhase2 > 0) { // make sure thisModelGHI != -999
                    weight = ((double) thisModelStats->optimizedWeightPhase2) / 100.0;
                    thisSample->forecastData[hoursAheadIndex].optimizedGHI += (thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] * weight);
                    weightTotal += weight;
                }
            }
            fprintf(fci->optimizedTSFile.fp, ",%.0f,%.0f,%.0f\n", thisSample->groundGHI, thisSample->satGHI, thisSample->forecastData[hoursAheadIndex].optimizedGHI);
        }
    }

    fclose(fci->optimizedTSFile.fp);

    return True;
}

int computeHourlyRmseErrorWeighted(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int useGroundReference)
{
    int sampleInd, modelIndex;
    double diff;
    double weight, weightTotal;
    timeSeriesType *thisSample;
    modelStatsType *thisModelStats, *weightedModelStats;
    modelRunType *modelRun;
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

#ifdef DEBUG_2
    int count = 0, hoursAhead = modelRun->hoursAhead;
#endif
    if(useGroundReference)
        weightedModelStats = &modelRun->weightedModelStatsVsGround;
    else
        weightedModelStats = &modelRun->weightedModelStatsVsSat;

    weightedModelStats->sumModel_Ground_2 = 0;

    for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->forecastData[hoursAheadIndex].groupIsValid == OK) {

#ifdef DEBUG_2
            if(hoursAhead == DEBUGHOUR)
                fprintf(stderr, "DEBUG:group %d\n", count++);
#endif
            // instead of each model having a separate rmse, they will have a composite rmse
            weightTotal = 0;
            thisSample->forecastData[hoursAheadIndex].optimizedGHI = 0;
            for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
                thisModelStats = &modelRun->hourlyModelStats[modelIndex];
                if(thisModelStats->maskSwitchOn && thisModelStats->weight > 0) {
                    weight = ((double) thisModelStats->weight) / 100.0;
                    thisSample->forecastData[hoursAheadIndex].optimizedGHI += (thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] * weight);
                    weightTotal += weight;
#ifdef DEBUG_2
                    if(hoursAhead == DEBUGHOUR && weight > 0.01) {
                        fprintf(stderr, "DEBUG:%s,%s=%.1f,weight=%.2f,weightedGHI=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                getModelName(fci, modelIndex), thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex], weight, thisSample->optimizedGHI);
                    }
#endif
                }
            }
            /*
                        if(weightTotal > 1.1) 
                            fprintf(stderr, "Internal Error: model weights sum to %.2f\n", weightTotal);
             */

            if(useGroundReference)
                diff = thisSample->forecastData[hoursAheadIndex].optimizedGHI - thisSample->groundGHI;
            else
                diff = thisSample->forecastData[hoursAheadIndex].optimizedGHI - thisSample->satGHI;

            weightedModelStats->sumModel_Ground_2 += (diff * diff);
        }
    }

    weightedModelStats->N = modelRun->numValidSamples;
    weightedModelStats->rmse = sqrt(weightedModelStats->sumModel_Ground_2 / weightedModelStats->N);
    weightedModelStats->rmsePct = weightedModelStats->rmse / modelRun->meanMeasuredGHI;

    //#define PRINT_ALL_WEIGHTS
#ifdef PRINT_ALL_WEIGHTS
    fprintf(stderr, "[HA=%d HAS=%d] weights: ", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead, hoursAfterSunriseIndex + 1);
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        thisModelStats = &modelRun->hourlyModelStats[modelIndex];
        if(modelRun->hourlyModelStats[modelIndex].isActive)
            fprintf(stderr, "modelIndex=%d weight=%d ", modelIndex, thisModelStats->weight);
    }
    fprintf(stderr, "RSME=%.1f N=%d\n", weightedModelStats->rmsePct * 100, weightedModelStats->N);
#endif

#ifdef DEBUG_2
    fprintf(stderr, "weightedRMSE: rmsePct=%.1f sumModel_Ground_2=%.1f, N=%d, totalWeights=%.1f\n", weightedModelStats->rmsePct * 100, weightedModelStats->sumModel_Ground_2, N, weightTotal);
#endif
    return True;
}

// This function dumps modelMix files for HA and HAS modes
// For HAS also computes RMSE and dumps that to forecastSummary files

int dumpModelMixRMSE(forecastInputType *fci, int hoursAheadIndex)
{
    int N = 0, sampleInd, modelIndex, hoursAfterSunriseIndex;
    double diff;
    double weight, weightTotal, meanMeasuredGHI;
    timeSeriesType *thisSample;
    modelStatsType *thisModelStats, *weightedErrVsGroundAllHAS, *satModelErr;
    modelRunType *modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][0] : &fci->hoursAheadGroup[hoursAheadIndex];
    static char fileName[1024];
#ifdef nDEBUG
    int firstTime = True;
#endif    

    int hoursAhead = fci->hoursAheadGroup[hoursAheadIndex].hoursAhead;

    // open modelMix file
    if(fci->modelMixFileOutput.fp == NULL) {
        sprintf(fileName, "%s/%s.modelMix.%s.allPermutations.csv", fci->outputDirectory, genProxySiteName(fci), fci->runHoursAfterSunrise ? "HA_HAS" : "HA");
        fci->modelMixFileOutput.fileName = strdup(fileName);
        fprintf(stderr, "\n ======== Generating model mix file %s\n", fci->modelMixFileOutput.fileName);
        if((fci->modelMixFileOutput.fp = fopen(fci->modelMixFileOutput.fileName, "w")) == NULL) {
            sprintf(ErrStr, "Couldn't open file %s : %s", fci->modelMixFileOutput.fileName, strerror(errno));
            FatalError("dumpModelMix()", ErrStr);
        }

        fprintf(fci->modelMixFileOutput.fp, "#site=%s lat=%.3f lon=%.3f\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon);
        if(fci->runHoursAfterSunrise)
            fprintf(fci->modelMixFileOutput.fp, "perm,HA,HAS");
        else
            fprintf(fci->modelMixFileOutput.fp, "perm,HA");

        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            if(!modelRun->hourlyModelStats[modelIndex].isReference) {
                fprintf(stderr, "modelIndex = %d modelName = %s\n", modelIndex, getModelName(fci, modelIndex));
                fprintf(fci->modelMixFileOutput.fp, ",%s", getModelName(fci, modelIndex));
            }
        }
        fprintf(fci->modelMixFileOutput.fp, ",N,RMSE_GRND,%%RMSE_GRND\n");
    }

    //
    // HA-only mode
    //
    if(!fci->runHoursAfterSunrise) {
        fprintf(fci->modelMixFileOutput.fp, "%d,%d", fci->modelPermutations.currentPermutationIndex, hoursAhead);
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            thisModelStats = &modelRun->hourlyModelStats[modelIndex];
            //if(!modelRun->hourlyModelStats[modelIndex].isReference) {
            if(isContributingModel(&modelRun->hourlyModelStats[modelIndex])) {
                weight = fci->skipPhase2 ? thisModelStats->optimizedWeightPhase1 : thisModelStats->optimizedWeightPhase2;
                fprintf(fci->modelMixFileOutput.fp, ",%.0f", weight);
            }
            else if(!modelRun->hourlyModelStats[modelIndex].isReference)
                fprintf(fci->modelMixFileOutput.fp, ",-999"); // model is not active
            //}
        }
        fprintf(fci->modelMixFileOutput.fp, ",%d,%.0f,%.1f\n", modelRun->numValidSamples, modelRun->optimizedRMSEphase2, modelRun->optimizedPctRMSEphase2 * 100);
        fflush(fci->modelMixFileOutput.fp);
        return True;
    }

    //
    // HAS mode
    //

    // set up some shorthands
    weightedErrVsGroundAllHAS = &fci->hoursAheadGroup[hoursAheadIndex].weightedModelStatsVsGround;
    satModelErr = &fci->hoursAheadGroup[hoursAheadIndex].satModelStats;

    // zero out RMSE variables
    weightedErrVsGroundAllHAS->sumModel_Ground_2 = weightedErrVsGroundAllHAS->rmse = weightedErrVsGroundAllHAS->rmsePct = 0;
    satModelErr->sumModel_Ground_2 = satModelErr->rmse = satModelErr->rmsePct = 0;

    //#define DEBUG_HAS    
    // now do RMSE

    // open summary file forecastSummary.HAS.*
    if(fci->summaryFile.fp == NULL) {
        char *startDateStr = strdup(dtToStringDateOnly(&fci->startDate));
        char *endDateStr = strdup(dtToStringDateOnly(&fci->endDate));
        sprintf(fileName, "%s/forecastSummary.HAS.%s.%s-%s.div=%d.hours=%d-%d.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), startDateStr, endDateStr, fci->numDivisions, fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead, fci->modelPermutations.currentPermutationIndex);
        fci->summaryFile.fileName = strdup(fileName);
        if((fci->summaryFile.fp = fopen(fci->summaryFile.fileName, "w")) == NULL) {
            sprintf(ErrStr, "Couldn't open file %s : %s", fci->summaryFile.fileName, strerror(errno));
            FatalError("printHoursAheadSummaryCsv()", ErrStr);
        }
        // print the header
        fprintf(fci->summaryFile.fp, "#site=%s lat=%.3f lon=%.3f divisions=%d start date=%s end date=%s\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, fci->numDivisions, startDateStr, endDateStr);
        fprintf(fci->summaryFile.fp, "#hoursAhead,group N,sat RMSE,p2RMSE\n");
        free(startDateStr);
        free(endDateStr);
    }

    for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
        int hoursAfterSunrise = hoursAfterSunriseIndex + 1;

        if(!filterForecastData(fci, hoursAheadIndex, hoursAfterSunriseIndex)) // must re-filter the T/S data for each HA/HAS combination
            //return False;
            continue;

        modelRun = &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex];

        fprintf(fci->modelMixFileOutput.fp, "%d,%d,%d", fci->modelPermutations.currentPermutationIndex, hoursAhead, hoursAfterSunrise);
        // N += modelRun->numValidSamples;
        // meanMeasuredGHI += modelRun->meanMeasuredGHI;
        for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
            thisSample = &fci->timeSeries[sampleInd];
            if(thisSample->forecastData[hoursAheadIndex].groupIsValid == OK) {
#ifdef DEBUG_HAS
                fprintf(stderr, "DEBUG:%s,%s,HA=%d,HAS=%d,", dtToStringCsv2(&thisSample->dateTime), thisSample->siteName, modelRun->hoursAhead, modelRun->hoursAfterSunrise);
#endif
#ifdef nDEBUG
                if(firstTime) {
                    int j;
                    fprintf(stderr, "DEBUG:\n#hoursAheadIndex=%d\n", hoursAheadIndex);
                    fprintf(stderr, "DEBUG:#year,month,day,hour,minute");
                    for(j = 0; j < fci->numModels; j++)
                        fprintf(stderr, ",%s - ground,abs(%s - ground),(%s - ground)^2", getModelName(fci, j), getModelName(fci, j), getModelName(fci, j));
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
                thisSample->forecastData[hoursAheadIndex].optimizedGHI = 0;
                N++;
                meanMeasuredGHI += thisSample->groundGHI;

                for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
                    thisModelStats = &modelRun->hourlyModelStats[modelIndex];
                    if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
                        weight = fci->skipPhase2 ? thisModelStats->optimizedWeightPhase1 : thisModelStats->optimizedWeightPhase2;
                        weight /= 100.0; // weights are kept as ints 0-100; make 24 => .24
                        // add up weight * GHI for each included forecast model
                        if(thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] < 0) {
                            fprintf(stderr, "Problem: trying to add in a negative GHI: %.1f [%s, hoursAhead=%d, model=%s]\n", thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex],
                                    dtToStringCsv2(&thisSample->dateTime), hoursAhead, thisModelStats->modelName);
                        }
                        thisSample->forecastData[hoursAheadIndex].optimizedGHI += (thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] * weight);
                        weightTotal += weight;
#ifdef DEBUG_HAS
                        //if(hoursAhead == DEBUGHOUR && weightedModelStatsVsGround->weight > 0.01*/) {
                        /*
                                                if(weight > 0.01)
                                                    fprintf(stderr, "%s=%.1f * %.2f,", 
                                                            getModelName(fci, modelIndex),thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex],weight);
                                                //}
                         */
#endif
                    }
                }
#ifdef DEBUG_HAS
                fprintf(stderr, "weightedGHI=%.1f,groundGHI=%.1f\n", thisSample->optimizedGHI, thisSample->groundGHI);
#endif
                if(weightTotal > 1.5 || weightTotal < 0.5) {
                    sprintf(ErrStr, "Internal Error: model weights sum to %.2f, expecting 1.0\n", weightTotal);
                    FatalError("dumpModelMixRMSE()", ErrStr);
                }

                diff = thisSample->forecastData[hoursAheadIndex].optimizedGHI - thisSample->groundGHI;
                weightedErrVsGroundAllHAS->sumModel_Ground_2 += (diff * diff);

                diff = thisSample->satGHI - thisSample->groundGHI;
                satModelErr->sumModel_Ground_2 += (diff * diff);
            }
        }

        // this is the modelMixFile print loop
        // all the data referenced here has been calculated previously and elsewhere

        // first we use the final optimal model mix to compute RMSE bases on the satGHI
        // and store that in modelRun->weightedModelStatsVsSat

        //computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex, USE_SATELLITE_REF); 

        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            thisModelStats = &modelRun->hourlyModelStats[modelIndex];
            if(isContributingModel(&modelRun->hourlyModelStats[modelIndex])) {
                weight = fci->skipPhase2 ? thisModelStats->optimizedWeightPhase1 : thisModelStats->optimizedWeightPhase2;
                fprintf(fci->modelMixFileOutput.fp, ",%.0f", weight);
            }
            else if(!modelRun->hourlyModelStats[modelIndex].isReference)
                fprintf(fci->modelMixFileOutput.fp, ",-999"); // model is no longer active at this HA -- put a place mark in
        }
        /*
                fprintf(fci->modelMixFileOutput.fp, ",%d,%.0f,%.1f,%.0f,%.1f\n", 
                        modelRun->numValidSamples, modelRun->optimizedRMSEphase2, modelRun->optimizedPctRMSEphase2 * 100, modelRun->weightedModelStatsVsSat.rmse, modelRun->weightedModelStatsVsSat.rmsePct * 100);
         */
        fprintf(fci->modelMixFileOutput.fp, ",%d,%.0f,%.1f\n",
                modelRun->numValidSamples, modelRun->optimizedRMSEphase2, modelRun->optimizedPctRMSEphase2 * 100);
        fflush(fci->modelMixFileOutput.fp);
    } // for(hoursAfterSunriseIndex=0;...)

    //N = modelRun->numValidSamples;   
    meanMeasuredGHI /= N;
    weightedErrVsGroundAllHAS->rmse = sqrt(weightedErrVsGroundAllHAS->sumModel_Ground_2 / N);
    weightedErrVsGroundAllHAS->rmsePct = weightedErrVsGroundAllHAS->rmse / meanMeasuredGHI;

    satModelErr->rmse = sqrt(satModelErr->sumModel_Ground_2 / N);
    satModelErr->rmsePct = satModelErr->rmse / meanMeasuredGHI;

    fprintf(stderr, "HA=%d/HAS=1..%d RMSE: N=%d, sumModel_Ground_2=%.1f, meanMeasuredGHI=%.1f, RMSE=%.1f, %%RMSE = %.02f\n",
            hoursAhead, fci->maxHoursAfterSunrise, N, weightedErrVsGroundAllHAS->sumModel_Ground_2, meanMeasuredGHI, weightedErrVsGroundAllHAS->rmse, weightedErrVsGroundAllHAS->rmsePct * 100);
    fprintf(fci->summaryFile.fp, "%d,%d,%.1f,%.1f\n", hoursAhead, N, satModelErr->rmsePct * 100, weightedErrVsGroundAllHAS->rmsePct * 100);
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

    sprintf(tempFileName, "%s/%s.numModelsReporting.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), fci->modelPermutations.currentPermutationIndex);
    fci->modelsAttendenceFile.fileName = strdup(tempFileName);

    if((fci->modelsAttendenceFile.fp = fopen(fci->modelsAttendenceFile.fileName, "w")) == NULL) {
        sprintf(ErrStr, "Couldn't open file %s: %s", fci->modelsAttendenceFile.fileName, strerror(errno));
        FatalError("dumpNumModelsReportingTable()", ErrStr);
    }

    fprintf(fci->modelsAttendenceFile.fp, "#Number of Models Reporting for site %s, lat=%.3f, lon=%.3f, ha='hours ahead'\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon);
    fprintf(fci->modelsAttendenceFile.fp, "#year,month,day,hour,minute");
    for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        fprintf(fci->modelsAttendenceFile.fp, ",ha_%d", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
    }
    fprintf(fci->modelsAttendenceFile.fp, "\n");

    for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->sunIsUp) {
            fprintf(fci->modelsAttendenceFile.fp, "%s", dtToStringCsv2(&thisSample->dateTime));
            for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
                numModelsReporting = 0;
                for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
                    if(isContributingModel(&fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex])) { // is this model turned on for this hoursAhead?
                        if(thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] >= 5) // is the value for GHI good?
                            numModelsReporting++;
                    }
                }
                fprintf(fci->modelsAttendenceFile.fp, ",%d", numModelsReporting);
            }
            fprintf(fci->modelsAttendenceFile.fp, "\n");
        }
    }

    fclose(fci->modelsAttendenceFile.fp);
}
