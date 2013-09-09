#include "forecastOpt.h"

void runOptimizer(forecastInputType *fci, int hourIndex)
{
    int modelIndex, numActiveModels=0;
    modelErrorType *hourGroup = &fci->hourErrorGroup[hourIndex];
    int hoursAhead = fci->hourErrorGroup[hourIndex].hoursAhead;
    long counter, limit;
    int powerMultiple;
    double floorVal, divisorPow;
    
    // intialize things
    hourGroup->meanMeasuredGHI = hourGroup->numValidSamples = hourGroup->ground_N = 0;
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        hourGroup->modelError[modelIndex].isActive = (getMaxHoursAhead(fci, modelIndex) >= hoursAhead);
        if(hourGroup->modelError[modelIndex].isActive) {
            hourGroup->modelError[modelIndex].powerOfTen = pow(10, modelIndex);
            numActiveModels++;
        }  
    }
    
    // 10 ^ N permutations
    
    // run a counter 1..10^N
    // modulo that for every power of 10, 1..N
    
    limit = pow(10, numActiveModels);
    
    for(counter=0; counter<limit; counter++) {
        fprintf(stderr, "%ld ", counter);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
            // 22895 = 5*1 + 9*10 + 8*100 + 2*1000 + 2*10000 => we want to calculate the 5,9,8,2 and 2
            if(hourGroup->modelError[modelIndex].isActive) {
                divisorPow = hourGroup->modelError[modelIndex].powerOfTen * 10;
                floorVal = floor(counter / divisorPow) * divisorPow;            
                powerMultiple = (int) (counter - floorVal)/hourGroup->modelError[modelIndex].powerOfTen;
                hourGroup->modelError[modelIndex].weight = powerMultiple/10;
                if(hourGroup->modelError[modelIndex].powerOfTen >= counter) {   // should shave off a lot of wasted time
                    fprintf(stderr, "%02d ", powerMultiple);
                    // first we need to form the composite/weighted GHI from all forecast models
                    //runRMSEonWeightedModels(fci, hourIndex);
                }
            }
        }
        fprintf(stderr, "\n");
    }
}


