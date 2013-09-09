#include "forecastOpt.h"
#include "satModel.h"

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

#define DEBUG


// satellite GHI is treated as a quasi-model; it's run through the error codes but
// it's not a forecast entity.
#define getModelGHI(modelIndex) (modelIndex < 0 ? thisSample->satGHI : thisSample->hourGroup[hourIndex].modelGHI[modelIndex])
#define incrementModelN(modelIndex) (modelIndex < 0 ? (hourGroup->satModelError.N++) : (hourGroup->modelError[modelIndex].N++))
#define getModelN(modelIndex) (modelIndex < 0 ? (hourGroup->satModelError.N) : (hourGroup->modelError[modelIndex].N))

#define DEBUGHOUR 1

int doErrorAnalysis(forecastInputType *fci, int hourIndex)
{
    // first form averages, etc.
    int modelIndex;
    int N = 0;
    modelErrorType *hourGroup = &fci->hourErrorGroup[hourIndex];
    modelStatsType *thisModelErr;
    int hoursAhead = fci->hourErrorGroup[hourIndex].hoursAhead;
#ifdef DEBUG
    static int firstTime = True;
#endif
    
    if(!filterHourlyModelData(fci, hourIndex))
        return False;
       
    clearHourlyErrorFields(fci, hourIndex);
    
    computeHourlyDifferences(fci, hourIndex);
    
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
    
                        if(hoursAhead == DEBUGHOUR) 
                            fprintf(stderr, "blorf\n");

    N = hourGroup->numValidSamples;
    hourGroup->meanMeasuredGHI /= N;
    for(modelIndex=-1; modelIndex < fci->numModels; modelIndex++) {
        thisModelErr = NULL;
        if(modelIndex < 0)
            thisModelErr = &fci->hourErrorGroup[hourIndex].satModelError;
        else {
            if(hourGroup->modelError[modelIndex].isActive) 
                thisModelErr = &fci->hourErrorGroup[hourIndex].modelError[modelIndex];    
        }
        if(thisModelErr) {
            thisModelErr->mbe = thisModelErr->sumModel_Ground / N;
            thisModelErr->mbePct = thisModelErr->mbe / hourGroup->meanMeasuredGHI;
            thisModelErr->mae = thisModelErr->sumAbs_Model_Ground / N;
            thisModelErr->maePct = thisModelErr->mae / hourGroup->meanMeasuredGHI;
            thisModelErr->rmse = sqrt(thisModelErr->sumModel_Ground_2 / N);
            thisModelErr->rmsePct = thisModelErr->rmse / hourGroup->meanMeasuredGHI; 
        }
    }

    return True;
}

void clearHourlyErrorFields(forecastInputType *fci, int hourIndex)
{
    modelErrorType *hourGroup = &fci->hourErrorGroup[hourIndex];
    int modelIndex;
    modelStatsType *thisModelErr;
    
    // zero out all statistical values
    for(modelIndex=-1; modelIndex < fci->numModels; modelIndex++) {
        if(modelIndex < 0)
            thisModelErr = &hourGroup->satModelError;
        else
            thisModelErr = &hourGroup->modelError[modelIndex];

        thisModelErr->sumModel_Ground = thisModelErr->sumAbs_Model_Ground = thisModelErr->sumModel_Ground_2 = 0;
        thisModelErr->mae = thisModelErr->mbe = thisModelErr->rmse = 0;  
        thisModelErr->maePct = thisModelErr->mbePct = thisModelErr->rmsePct = 0;  
        thisModelErr->N = 0;
    }
}

void addErrorSumData(modelStatsType *thisModelErr)
{
    
}

int filterHourlyModelData(forecastInputType *fci, int hourIndex)
{
    int sampleInd, modelIndex;
    double thisGHI;
    timeSeriesType *thisSample;
    modelErrorType *hourGroup = &fci->hourErrorGroup[hourIndex];
    int hoursAhead = fci->hourErrorGroup[hourIndex].hoursAhead;

    hourGroup->meanMeasuredGHI = hourGroup->numValidSamples = hourGroup->ground_N = 0;
    
    // for the each model: does it even go out to the current hoursAhead?
    // use isActive to keep track of this info

    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        hourGroup->modelError[modelIndex].isActive = (getMaxHoursAhead(fci, modelIndex) >= hoursAhead);
#ifdef DEBUG
        fprintf(stderr, "For hours ahead %d and model %s, state = %s\n", hoursAhead, getGenericModelName(fci, modelIndex), hourGroup->modelError[modelIndex].isActive ? "active" : "inactive");
#endif
    }
  
    for(sampleInd=0; sampleInd<fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        thisSample->isValid = True;

        // for the sat model and each forecast model, filter date/times based on all values
        for(modelIndex=-1; modelIndex < fci->numModels; modelIndex++) {
            if(modelIndex < 0 || hourGroup->modelError[modelIndex].isActive) {
                thisGHI = getModelGHI(modelIndex);
                if(thisGHI < 5) {
#ifdef DEBUG1
                    fprintf(stderr, "%s hours ahead %d: bad sample: model %s: GHI = %.1f\n", 
                            dtToStringCsv2(&thisSample->dateTime), hourGroup->hoursAhead, getGenericModelName(fci, modelIndex), thisGHI);
#endif                
                    thisSample->isValid = False;
                    // break;
                }     
                else { // if this model's GHI didn't trigger an isValid=False, increment N
#ifdef DEBUG1
                    fprintf(stderr, "%s hours ahead %d: good sample: model %s: GHI = %.1f\n", 
                         dtToStringCsv2(&thisSample->dateTime), hourGroup->hoursAhead, getGenericModelName(fci, modelIndex), thisGHI);
#endif                
                    incrementModelN(modelIndex);
                }
            }
        }
        
        if(thisSample->groundGHI < MIN_GHI_VAL ) {
            thisSample->isValid = False;
        }
        else {
            hourGroup->ground_N++;
        }
        
        if(thisSample->isValid) {
            hourGroup->meanMeasuredGHI += thisSample->groundGHI;
            hourGroup->numValidSamples++;
#ifdef DEBUG1
            fprintf(stderr, "%s hours ahead: %d:  all good: meanMeasuredGHI=%.1f numValidSamples=%d\n", 
                 dtToStringCsv2(&thisSample->dateTime), hourGroup->hoursAhead, hourGroup->meanMeasuredGHI, hourGroup->numValidSamples);
#endif
        }
    }
    
    fprintf(stderr, "\nHR%d=== Summary for hour %d (number of good samples) ===\n", hoursAhead, hoursAhead);
    fprintf(stderr, "HR%d\t%-40s : %d\n", hoursAhead, "N for group", hourGroup->numValidSamples);
    fprintf(stderr, "HR%d\t%-40s : %d\n", hoursAhead, "ground GHI", hourGroup->ground_N);
    for(modelIndex=-1; modelIndex < fci->numModels; modelIndex++) {
        if(modelIndex < 0 || hourGroup->modelError[modelIndex].isActive)
            fprintf(stderr, "HR%d\t%-40s : %d\n", hoursAhead, getGenericModelName(fci, modelIndex), getModelN(modelIndex));
    }
    
    if(hourGroup->numValidSamples < 1) {
        fprintf(stderr, "doErrorAnalysis(): for hour index %d (%d hours ahead): got too few valid points to work with\n", hourIndex, hourGroup->hoursAhead);
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
                    if(hourGroup->modelError[modelIndex].isActive) {
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
                if(hourGroup->modelError[modelIndex].isActive) {
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
    
    return True;    
}

int computeHourlyDifferences(forecastInputType *fci, int hourIndex)
{
    int sampleInd, modelIndex;
    double diff;
    timeSeriesType *thisSample;
    modelErrorType *hourGroup = &fci->hourErrorGroup[hourIndex];
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
                        fprintf(stderr, ",%s - ground,abs(%s - ground),(%s - ground)^2", hourGroup->modelError.modelName, fci->models[j].modelName, fci->models[j].modelName);
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
                    if(hourGroup->modelError[modelIndex].isActive) {
                        diff = thisSample->hourGroup[hourIndex].modelGHI[modelIndex] - thisSample->groundGHI;
#ifdef DEBUG
                        if(hoursAhead == DEBUGHOUR) {
                            fprintf(stderr, "DEBUG:%s,%s=%.1f,grndGHI=%.1f,diff=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                    getGenericModelName(fci, modelIndex),thisSample->hourGroup[hourIndex].modelGHI[modelIndex],thisSample->groundGHI,diff);
                        }
#endif
                        thisModelErr = &fci->hourErrorGroup[hourIndex].modelError[modelIndex];                        
                    }
                }
                if(thisModelErr) {
                    thisModelErr->sumModel_Ground += diff;
                    thisModelErr->sumAbs_Model_Ground += fabs(diff);
                    thisModelErr->sumModel_Ground_2 += (diff * diff);
                }
#ifdef nDEBUG                
                fprintf(stderr, ",%.1f,%.1f,%.1f", diff, fabs(diff), diff*diff);
#endif
            }
        }
    }
    
    return True;
}
    
