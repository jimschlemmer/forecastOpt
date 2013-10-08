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


// satellite GHI is treated as a quasi-model; it's run through the error codes but
// it's not a forecast entity.
#define getModelGHI(modelIndex) (modelIndex < 0 ? thisSample->satGHI : thisSample->hourGroup[hourIndex].modelGHI[modelIndex])
#define incrementModelN() (modelIndex < 0 ? (hourGroup->satModelError.N++) : (hourGroup->modelError[modelIndex].N++))

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
    modelErrorType *hourGroup = &fci->hourErrorGroup[hourIndex];
    int modelIndex;
    

    fprintf(stderr, "Clearing stats fields for hour %d\n", hourIndex);
    // zero out all statistical values
    clearModelStats(&hourGroup->satModelError);
    clearModelStats(&hourGroup->weightedModelError);
    
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) 
        clearModelStats(&hourGroup->modelError[modelIndex]);
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
    modelErrorType *hourGroup = &fci->hourErrorGroup[hourIndex];
    int hoursAhead = fci->hourErrorGroup[hourIndex].hoursAhead;
   
    // reset a few variables
    hourGroup->meanMeasuredGHI = hourGroup->numValidSamples = hourGroup->ground_N = 0;
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) 
        hourGroup->modelError[modelIndex].N = 0;

    // for the each model: does it even go out to the current hoursAhead?
    // use isActive to keep track of this info
    // use usUsale to signify isActive and not a reference forecast model (such as persistence)
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        hourGroup->modelError[modelIndex].isActive = (getMaxHoursAhead(fci, modelIndex) >= hoursAhead);
        hourGroup->modelError[modelIndex].isUsable = hourGroup->modelError[modelIndex].isActive && !hourGroup->modelError[modelIndex].isReference;
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
//#ifdef DEBUG1
                    if(thisSample->sunIsUp) 
                        fprintf(fci->warningsFp, "%s : bad sample: model %s %d hours ahead: GHI = %.1f, zenith = %.1f\n", 
                            dtToStringCsv2(&thisSample->dateTime), getGenericModelName(fci, modelIndex), hourGroup->hoursAhead, thisGHI, thisSample->zenith);
//#endif                
                    thisSample->isValid = False;
                    // break;
                }     
                else { // if this model's GHI didn't trigger an isValid=False, increment N
#ifdef DEBUG1
                    fprintf(stderr, "%s hours ahead %d: good sample: model %s: GHI = %.1f\n", 
                         dtToStringCsv2(&thisSample->dateTime), hourGroup->hoursAhead, getGenericModelName(fci, modelIndex), thisGHI);
#endif                
                    incrementModelN();
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
    
    if(hourGroup->numValidSamples < 1) {
        fprintf(fci->warningsFp, "doErrorAnalysis(): for hour index %d (%d hours ahead): got too few valid points to work with\n", hourIndex, hourGroup->hoursAhead);
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
    
#ifdef WRITE_FILTERED_DATA
    FILE *fp;
    char filename[1024];
    // open the file
    sprintf(filename, "%s.filteredData.hourAhead=%d.csv", fci->siteName, hoursAhead);
    if((fp = fopen(filename, "w")) == NULL) {
        fprintf(stderr, "Couldn't open file %s\n", filename);
        exit(1);
    }
    // print the header
    fprintf(fp, "#year,month,day,hour,min,lineNum,groupIsValid,groundGHI,satGHI");
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        if(hourGroup->modelError[modelIndex].isActive) {
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
                if(hourGroup->modelError[modelIndex].isActive) {
                    fprintf(fp, ",%.0f", thisSample->hourGroup[hourIndex].modelGHI[modelIndex]);                    
                }
            } 
            fprintf(fp, "\n");
    }
    fclose(fp);
#endif
    
    hourGroup->meanMeasuredGHI /= hourGroup->numValidSamples;

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
                }
#ifdef nDEBUG                
                fprintf(stderr, ",%.1f,%.1f,%.1f", diff, fabs(diff), diff*diff);
#endif
            }
        }
    }
    
    N = hourGroup->numValidSamples;
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
        }
    }

    return True;
}
    
int computeHourlyRmseErrors(forecastInputType *fci, int hourIndex)
{
    int N, sampleInd, modelIndex;
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
                    thisModelErr->sumModel_Ground_2 += (diff * diff);
                }
#ifdef nDEBUG                
                fprintf(stderr, ",%.1f,%.1f,%.1f", diff, fabs(diff), diff*diff);
#endif
            }
        }
    }
    
    N = hourGroup->numValidSamples;

    for(modelIndex=-1; modelIndex < fci->numModels; modelIndex++) {
        thisModelErr = NULL;
        if(modelIndex < 0)
            thisModelErr = &fci->hourErrorGroup[hourIndex].satModelError;
        else {
            if(hourGroup->modelError[modelIndex].isActive) 
                thisModelErr = &fci->hourErrorGroup[hourIndex].modelError[modelIndex];    
        }
        if(thisModelErr) {
            thisModelErr->rmse = sqrt(thisModelErr->sumModel_Ground_2 / N);
            thisModelErr->rmsePct = thisModelErr->rmse / hourGroup->meanMeasuredGHI; 
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
    modelErrorType *hourGroup = &fci->hourErrorGroup[hourIndex];
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
                        fprintf(stderr, ",%s - ground,abs(%s - ground),(%s - ground)^2", hourGroup->modelError.modelName, fci->models[j].modelName, fci->models[j].modelName);
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
                if(hourGroup->modelError[modelIndex].isActive) {
                    weight = thisModelErr->weight;
                    thisSample->weightedModelGHI += (thisSample->hourGroup[hourIndex].modelGHI[modelIndex] * weight);
                    weightTotal += weight;               
#ifdef DEBUG_2
                    if(hoursAhead == DEBUGHOUR /*&& weightedModelErr->weight > 0.01*/) {
                        fprintf(stderr, "DEBUG:%s,%s=%.1f,weight=%.2f,weightedGHI=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                                getGenericModelName(fci, modelIndex),thisSample->hourGroup[hourIndex].modelGHI[modelIndex],weight,thisSample->weightedModelGHI);
                    }
#endif
                }
            }
            diff = thisSample->weightedModelGHI - thisSample->groundGHI;
            weightedModelErr->sumModel_Ground_2 += (diff * diff);
        }
    }
    
    
    N = hourGroup->numValidSamples;   
    weightedModelErr->rmse = sqrt(weightedModelErr->sumModel_Ground_2 / N);
    weightedModelErr->rmsePct = weightedModelErr->rmse / hourGroup->meanMeasuredGHI; 

#ifdef DEBUG_2
    fprintf(stderr, "weightedRMSE: N=%d, sumModel_Ground_2=%.1f, totalWeights=%.1f\n", N, weightedModelErr->sumModel_Ground_2, weightTotal);
#endif
    return True;
}

