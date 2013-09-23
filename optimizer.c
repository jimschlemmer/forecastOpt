#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include "forecastOpt.h"

void setOptimalWeights(forecastInputType *fci, int hourIndex);
double sumWeights(forecastInputType *fci, int hourIndex);

long long Counter;
time_t Start_t;
double MinRmse;
int InPass1;

void runOptimizer(forecastInputType *fci, int hourIndex)
{
    int modelIndex, numActiveModels=0;
    modelErrorType *hourGroup = &fci->hourErrorGroup[hourIndex];
    int i, hoursAhead = fci->hourErrorGroup[hourIndex].hoursAhead;
    long long counter, modulo=0, powerMultiple=0, limit;
    double modRemainder=0, weightSum, minRmse = 1000;
    time_t start_t = time(NULL);
    
    // intialize things
    clearHourlyErrorFields(fci, hourIndex);   
    
    if(!filterHourlyModelData(fci, hourIndex))
        return;
       
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        hourGroup->modelError[modelIndex].isActive = (getMaxHoursAhead(fci, modelIndex) >= hoursAhead);
        if(hourGroup->modelError[modelIndex].isActive) {
            hourGroup->modelError[modelIndex].powerOfTen = powl(10, modelIndex);
            numActiveModels++;
        }  
    }
    
    // we only have to filter data once
    
    
    // 10 ^ N permutations
    
    // run a counter 1..10^N
    // modulo that for every power of 10, 1..N
    
    limit = pow(10, numActiveModels);
    
    for(counter=1; counter<=limit; counter++) {
/*
        if(counter == 2420000030)
            fprintf(stderr, "blah\n");
*/
        //fprintf(stderr, "%ld ", counter);
        weightSum = 0;
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
            // 22895 = 5*1 + 9*10 + 8*100 + 2*1000 + 2*10000 => we want to calculate the 5,9,8,2 and 2
            if(hourGroup->modelError[modelIndex].isActive) {
                modulo = hourGroup->modelError[modelIndex].powerOfTen * 10;
                //int(($n % 1000)/100)        
                modRemainder = (counter % modulo);   // 22895 % 1000 = 2895
                powerMultiple = modRemainder/hourGroup->modelError[modelIndex].powerOfTen;  // 2895/100 = 2.895, (int) 2.895 = 2
                hourGroup->modelError[modelIndex].weight = ((double) powerMultiple)/10;    // 2/10 = 0.2
                weightSum += hourGroup->modelError[modelIndex].weight;
/*
                if(hourGroup->modelError[modelIndex].powerOfTen >= counter) {   // should shave off a lot of wasted time
                    fprintf(stderr, "%.2f ", hourGroup->modelError[modelIndex].weight);
                    // first we need to form the composite/weighted GHI from all forecast models

                }
*/
            }

        }
//                    fprintf(stderr, "\n");

        if(weightSum >= 0.9 && weightSum <= 1.1) {
            computeHourlyRmseErrorWeighted(fci, hourIndex);
            if(hourGroup->weightedModelError.rmsePct < minRmse) {
                minRmse = hourGroup->weightedModelError.rmsePct;

                fprintf(stderr, "[%lld] @ %s new low RMSE =%.3f%% wts=", counter, getElapsedTime(start_t), minRmse * 100);
                for(i=0; i < fci->numModels; i++) {
                    if(hourGroup->modelError[i].isActive) {
                        fprintf(stderr, "%.0f ", hourGroup->modelError[i].weight * 10);
                    }
                }
                fprintf(stderr, "\n");      
                if(counter >= 1000000000) 
                    fprintf(stderr, "\t high order info: count=%lld,modulo=%lld/modRem=%.1f/pwrMul=%lld/wt=%.1f\n",  
                        counter, modulo, modRemainder, powerMultiple, hourGroup->modelError[fci->numModels-1].weight);

            }
        }
    }
}

char *getElapsedTime(time_t start_t)
{
    static char timeStr[1024];
    
    time_t now_t = time(NULL);
    time_t elapsed_t = now_t - start_t;
    
    int days = elapsed_t / 86400;
    elapsed_t -= (days * 86400);
    int hours = elapsed_t / 3600;
    elapsed_t -= (hours * 3600);
    int minutes = elapsed_t / 60;
    elapsed_t -= (minutes * 60);
    int seconds = elapsed_t;
    
    if(days > 0) 
        sprintf(timeStr, "%d days, %02d:%02d:%02d", days, hours, minutes, seconds);
    else 
        sprintf(timeStr, "%02d:%02d:%02d", hours, minutes, seconds);

    return(timeStr);
}

#define MIN_WEIGHT_SUM 0.79
#define MAX_WEIGHT_SUM 1.21

// Macro that's a bit lengthy but which makes the nested loop more readable
#define runRMSE() weightSum = sumWeights(fci, hourIndex); if(weightSum >= fci->weightSumLowCutoff && weightSum <= fci->weightSumHighCutoff) { Counter++; computeHourlyRmseErrorWeighted(fci, hourIndex); if(hourGroup->weightedModelError.rmsePct < MinRmse) { MinRmse = hourGroup->weightedModelError.rmsePct; setOptimalWeights(fci,hourIndex); } }

int runOptimizerNested(forecastInputType *fci, int hourIndex)
{
    int numDivisions = 10;
    double refinementBase, increment;  // just a real version of the above
    int modelIndex, numActiveModels=0;
    int i1,i2,i3,i4,i5,i6,i7,i8,i9,i10;
    double weightSum;
    modelErrorType *hourGroup = &fci->hourErrorGroup[hourIndex];
 
    InPass1 = True;
    
    // intialize things
    clearHourlyErrorFields(fci, hourIndex);   
    
    if(!filterHourlyModelData(fci, hourIndex))
        return False;
       
    int hoursAhead = fci->hourErrorGroup[hourIndex].hoursAhead;
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        hourGroup->modelError[modelIndex].isActive = (getMaxHoursAhead(fci, modelIndex) >= hoursAhead);
        if(hourGroup->modelError[modelIndex].isActive) {
            numActiveModels++;
        }  
    }
    
    increment = 0.1;
    MinRmse = 1000;
    Counter = 0;
    Start_t = time(NULL);
    
    for(i1=0; i1<numDivisions; i1++) {
        hourGroup->modelError[0].weight = i1 * increment;
        //fprintf(stderr, "Model 0, weight %.1f\n", hourGroup->modelError[0].weight);
        for(i2=0; i2<numDivisions; i2++) {
            hourGroup->modelError[1].weight = i2 * increment;
            //fprintf(stderr, "Model 1, weight %.1f\n", hourGroup->modelError[1].weight);
            if(numActiveModels == 2) {
                runRMSE();
                continue;
            } else {            
                for(i3=0; i3<numDivisions; i3++) {  
                    hourGroup->modelError[2].weight = i3 * increment;
                    if(numActiveModels == 3) {
                        runRMSE();
                        continue;               
                    } else {
                        for(i4=0; i4<numDivisions; i4++) {           
                            hourGroup->modelError[3].weight = i4 * increment;
                            if(numActiveModels == 4) {
                                runRMSE();
                                continue;               
                            } else {
                                for(i5=0; i5<numDivisions; i5++) {           
                                    hourGroup->modelError[4].weight = i5 * increment;
                                    if(numActiveModels == 5) {
                                        runRMSE();
                                        continue;               
                                    } else {
                                        for(i6=0; i6<numDivisions; i6++) {           
                                            hourGroup->modelError[5].weight = i6 * increment;
                                            if(numActiveModels == 6) {
                                                runRMSE();
                                                continue;               
                                            } else {
                                                for(i7=0; i7<numDivisions; i7++) {
                                                    hourGroup->modelError[6].weight = i7 * increment;
                                                    if(numActiveModels == 7) {
                                                        runRMSE();
                                                        continue;               
                                                    } else {
                                                        for(i8=0; i8<numDivisions; i8++) {
                                                            hourGroup->modelError[7].weight = i8 * increment;
                                                            if(numActiveModels == 8) {
                                                                runRMSE();
                                                                continue;               
                                                            } else {
                                                                for(i9=0; i9<numDivisions; i9++) {
                                                                    hourGroup->modelError[8].weight = i9 * increment;
                                                                    if(numActiveModels == 9) {
                                                                        runRMSE();
                                                                        continue;               
                                                                    } else {
                                                                        for(i10=0; i10<numDivisions; i10++) {   
                                                                            hourGroup->modelError[9].weight = i10 * increment;
                                                                            if(numActiveModels == 10) {
                                                                                runRMSE();
                                                                                continue;               
                                                                            } else {
                                                                                fprintf(stderr, "Got to end of nested loop\n");
                                                                                exit(1);
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    InPass1 = False;

    fprintf(stderr, "\n=== Elapsed time for phase 1: %s [RMSE calls = %lld] [RMSE = %.2f%%]\n", getElapsedTime(Start_t), Counter, hourGroup->optimizedRMSE * 100);

    refinementBase = -0.05;
    increment = .01;
    MinRmse = 1000;
    Counter = 0;
    Start_t = time(NULL);
    
    for(i1=0; i1<numDivisions; i1++) {
        hourGroup->modelError[0].weight = hourGroup->modelError[0].optimizedWeightPass1 + refinementBase + (i1 * increment);
        if(hourGroup->modelError[0].weight < 0)
            continue;
        //fprintf(stderr, "[%s] Model 0, i1 = %d origWeight = %.1f weight = %.3f\n", getElapsedTime(Start_t), i1, hourGroup->modelError[0].optimizedWeightPass1, hourGroup->modelError[0].weight);
        for(i2=0; i2<numDivisions; i2++) {
            hourGroup->modelError[1].weight = hourGroup->modelError[1].optimizedWeightPass1 + refinementBase + (i2 * increment);
            if(hourGroup->modelError[1].weight < 0)
                continue;
            //fprintf(stderr, "[%s] Model 1, i2 = %d origWeight = %.1f weight = %.3f\n", getElapsedTime(Start_t), i2, hourGroup->modelError[1].optimizedWeightPass1, hourGroup->modelError[1].weight);
            if(numActiveModels == 2) {
                runRMSE();
                continue;
            } else {            
                for(i3=0; i3<numDivisions; i3++) {  
                    hourGroup->modelError[2].weight = hourGroup->modelError[2].optimizedWeightPass1 + refinementBase + (i3 * increment);            
                    if(hourGroup->modelError[2].weight < 0)
                        continue;
                    if(numActiveModels == 3) {
                        runRMSE();
                        continue;               
                    } else {
                        for(i4=0; i4<numDivisions; i4++) {           
                            hourGroup->modelError[3].weight = hourGroup->modelError[3].optimizedWeightPass1 + refinementBase + (i4 * increment);            
                            if(hourGroup->modelError[3].weight < 0)
                                continue;
                            if(numActiveModels == 4) {
                                runRMSE();
                                continue;               
                            } else {
                                for(i5=0; i5<numDivisions; i5++) {           
                                    hourGroup->modelError[4].weight = hourGroup->modelError[4].optimizedWeightPass1 + refinementBase + (i5 * increment);            
                                    if(hourGroup->modelError[4].weight < 0)
                                        continue;
                                    if(numActiveModels == 5) {
                                        runRMSE();
                                        continue;               
                                    } else {
                                        for(i6=0; i6<numDivisions; i6++) {           
                                            hourGroup->modelError[5].weight = hourGroup->modelError[5].optimizedWeightPass1 + refinementBase + (i6 * increment);            
                                            if(hourGroup->modelError[5].weight < 0)
                                                continue;
                                            if(numActiveModels == 6) {
                                                runRMSE();
                                                continue;               
                                            } else {
                                                for(i7=0; i7<numDivisions; i7++) {
                                                    hourGroup->modelError[6].weight = hourGroup->modelError[6].optimizedWeightPass1 + refinementBase + (i7 * increment);
                                                    if(hourGroup->modelError[6].weight < 0)
                                                        continue;
                                                    if(numActiveModels == 7) {
                                                        runRMSE();
                                                        continue;               
                                                    } else {
                                                        for(i8=0; i8<numDivisions; i8++) {
                                                            hourGroup->modelError[7].weight = hourGroup->modelError[7].optimizedWeightPass1 + refinementBase + (i8 * increment);           
                                                            if(hourGroup->modelError[7].weight < 0)
                                                                continue;
                                                            if(numActiveModels == 8) {
                                                                runRMSE();
                                                                continue;               
                                                            } else {
                                                                for(i9=0; i9<numDivisions; i9++) {
                                                                    hourGroup->modelError[8].weight = hourGroup->modelError[8].optimizedWeightPass1 + refinementBase + (i9 * increment);
                                                                    if(hourGroup->modelError[8].weight < 0)
                                                                        continue;
                                                                    if(numActiveModels == 9) {
                                                                        runRMSE();
                                                                        continue;               
                                                                    } else {
                                                                        for(i10=0; i10<numDivisions; i10++) {   
                                                                            hourGroup->modelError[9].weight = hourGroup->modelError[9].optimizedWeightPass1 + refinementBase + (i10 * increment);
                                                                            if(hourGroup->modelError[9].weight < 0)
                                                                                continue;                                                                         
                                                                            if(numActiveModels == 10) {
                                                                                runRMSE();
                                                                                continue;               
                                                                            } else {
                                                                                fprintf(stderr, "Got to end of nested loop\n");
                                                                                exit(1);
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
        
    fprintf(stderr, "\n=== Elapsed time for phase 2: %s [RMSE calls = %lld] [RMSE = %.2f%%]\n", getElapsedTime(Start_t), Counter, hourGroup->optimizedRMSE * 100);

    fprintf(stderr, "\n=== Final weights for %s hours ahead %d:\n", fci->siteName, hoursAhead);
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        if(hourGroup->modelError[modelIndex].isActive) {           
            fprintf(stderr, "\t%-35s = %-8.2f\n", getGenericModelName(fci, modelIndex), hourGroup->modelError[modelIndex].optimizedWeightPass2);
        }
    }    
    fprintf(stderr, "\t\n[Final RMSE = %.2f%%]\n\n", hourGroup->optimizedRMSE * 100);
    
    return True;
}

void setOptimalWeights(forecastInputType *fci, int hourIndex)
{
    int modelIndex;
    modelErrorType *hourGroup = &fci->hourErrorGroup[hourIndex];
    MinRmse = hourGroup->weightedModelError.rmsePct;

    if(fci->verbose) {
        if(InPass1)
            fprintf(stderr, "[%lld] @ %s new low RMSE (pass 1) = %.3f%% wts=", Counter, getElapsedTime(Start_t), MinRmse * 100);
        else
            fprintf(stderr, "[%lld] @ %s new low RMSE (pass 2) = %.3f%% wts=", Counter, getElapsedTime(Start_t), MinRmse * 100);
    }
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        if(hourGroup->modelError[modelIndex].isActive) {
            if(InPass1) {
                hourGroup->modelError[modelIndex].optimizedWeightPass1 = hourGroup->modelError[modelIndex].weight;
                if(fci->verbose) 
                    fprintf(stderr, "%.0f ", hourGroup->modelError[modelIndex].weight * 10);
            }
            else {
                hourGroup->modelError[modelIndex].optimizedWeightPass2 = hourGroup->modelError[modelIndex].weight;
                if(fci->verbose) 
                    fprintf(stderr, "%.0f ", hourGroup->modelError[modelIndex].weight * 100);
            }
        }
    }
    if(fci->verbose)
        fprintf(stderr, "\n");
    
    hourGroup->optimizedRMSE = MinRmse;
}

double sumWeights(forecastInputType *fci, int hourIndex)
{
    int modelIndex;
    modelErrorType *hourGroup = &fci->hourErrorGroup[hourIndex];
    static double weightSum;

    weightSum = 0;
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        if(hourGroup->modelError[modelIndex].isActive) {
            weightSum += hourGroup->modelError[modelIndex].weight;
        }
    }
    
    return weightSum;    
}