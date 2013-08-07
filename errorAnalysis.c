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

//#define DEBUG

int doErrorAnalysis(forecastInputType *fci)
{
    // first form averages, etc.
    int sampleInd, modelNumber;
    int N = fci->numValidSamples;
    modelErrorType *thisModelErr;
    timeSeriesType *thisSample;
    double diff;
    static char firstTime = True;
    
    if(N < 1) {
        FatalError("doErrorAnalysis()", "Too few valid data points to work with.");
    }
    
    // zero out all statistical values
    for(modelNumber=0; modelNumber < fci->numModels; modelNumber++) {
            thisModelErr = &fci->models[modelNumber].modelError;
            thisModelErr->sumModel_Ground = thisModelErr->sumAbs_Model_Ground = thisModelErr->sumModel_Ground_2 = 0;
            thisModelErr->mae = thisModelErr->mbe = thisModelErr->rmse = 0;
            fci->meanMeasuredGHI = 0;
    }
    
    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->isValid) {
#ifdef DEBUG
            if(firstTime) {
                    int j;
                    fprintf(stderr, "DEBUG:#year,month,day,hour,minute");
                    for(j=0; j < fci->numModels; j++) 
                        fprintf(stderr, ",%s - ground,abs(%s - ground),(%s - ground)^2", fci->models[j].modelName, fci->models[j].modelName, fci->models[j].modelName);
                    firstTime = False;
                    fprintf(stderr, "\n)");
            }
 
            fprintf(stderr, "DEBUG:%s", dtToStringCsv2(&thisSample->dateTime));
#endif
            for(modelNumber=0; modelNumber < fci->numModels; modelNumber++) {
                diff = thisSample->modelGHIvalues[modelNumber] - thisSample->groundGHI;
                thisModelErr = &fci->models[modelNumber].modelError;
                thisModelErr->sumModel_Ground += diff;
                thisModelErr->sumAbs_Model_Ground += fabs(diff);
                thisModelErr->sumModel_Ground_2 += (diff * diff);
#ifdef DEBUG                
                fprintf(stderr, ",%.1f,%.1f,%.1f", diff, fabs(diff), diff*diff);
#endif
            }
#ifdef DEBUG                
            fprintf(stderr, "\n");
#endif
            fci->meanMeasuredGHI += thisSample->groundGHI;
        }
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
    fci->meanMeasuredGHI /= N;
    for(modelNumber=0; modelNumber < fci->numModels; modelNumber++) {
            thisModelErr = &fci->models[modelNumber].modelError;
            thisModelErr->mbe = thisModelErr->sumModel_Ground / N;
            thisModelErr->mbePct = thisModelErr->mbe / fci->meanMeasuredGHI;
            thisModelErr->mae = thisModelErr->sumAbs_Model_Ground / N;
            thisModelErr->maePct = thisModelErr->mae / fci->meanMeasuredGHI;
            thisModelErr->rmse = sqrt(thisModelErr->sumModel_Ground_2 / N);
            thisModelErr->rmsePct = thisModelErr->rmse / fci->meanMeasuredGHI;            
    }

    return True;
}