#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include "forecastOpt.h"

void saveModelWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex);
double sumWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex);
void dumpWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunrise, int phase);

time_t Start_t;
double MinRmse;
int InPass1;

void runOptimizer(forecastInputType *fci, int hoursAheadIndex)
{
    int modelIndex, numActiveModels=0;
    modelRunType *modelData = &fci->hoursAheadGroup[hoursAheadIndex];
    int i, hoursAhead = fci->hoursAheadGroup[hoursAheadIndex].hoursAhead;
    long long counter, modulo=0, powerMultiple=0, limit;
    double modRemainder=0, weightSum, minRmse = 1000;
    time_t start_t = time(NULL);
    
    // intialize things
    //clearHourlyErrorFields(fci, hoursAheadIndex);   
    
    if(!filterHourlyForecastData(fci, hoursAheadIndex, -1))
        return;
       
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        modelData->hourlyModelStats[modelIndex].isActive = (!modelData->hourlyModelStats[modelIndex].missingData && getMaxHoursAhead(fci, modelIndex) >= hoursAhead);
        if(modelData->hourlyModelStats[modelIndex].isActive) {
            modelData->hourlyModelStats[modelIndex].powerOfTen = powl(10, modelIndex);
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
            if(modelData->hourlyModelStats[modelIndex].isActive) {
                modulo = modelData->hourlyModelStats[modelIndex].powerOfTen * 10;
                //int(($n % 1000)/100)        
                modRemainder = (counter % modulo);   // 22895 % 1000 = 2895
                powerMultiple = modRemainder/modelData->hourlyModelStats[modelIndex].powerOfTen;  // 2895/100 = 2.895, (int) 2.895 = 2
                modelData->hourlyModelStats[modelIndex].weight = ((double) powerMultiple)/10;    // 2/10 = 0.2
                weightSum += modelData->hourlyModelStats[modelIndex].weight;
/*
                if(modelData->hourlyModelStats[modelIndex].powerOfTen >= counter) {   // should shave off a lot of wasted time
                    fprintf(stderr, "%.2f ", modelData->hourlyModelStats[modelIndex].weight);
                    // first we need to form the composite/weighted GHI from all forecast models

                }
*/
            }

        }
//                    fprintf(stderr, "\n");

        if(weightSum >= 0.9 && weightSum <= 1.1) {
            computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, -1);
            if(modelData->weightedModelStats.rmsePct < minRmse) {
                minRmse = modelData->weightedModelStats.rmsePct;

                fprintf(stderr, "[%lld] @ %s new low RMSE =%.3f%% wts=", counter, getElapsedTime(start_t), minRmse * 100);
                for(i=0; i < fci->numModels; i++) {
                    if(modelData->hourlyModelStats[i].isActive) {
                        fprintf(stderr, "%.0f ", modelData->hourlyModelStats[i].weight * 10);
                    }
                }
                fprintf(stderr, "\n");      
                if(counter >= 1000000000) 
                    fprintf(stderr, "\t high order info: count=%lld,modulo=%lld/modRem=%.1f/pwrMul=%lld/wt=%.1f\n",  
                        counter, modulo, modRemainder, powerMultiple, modelData->hourlyModelStats[fci->numModels-1].weight);

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
#define runRMSE() weightSum = sumWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex); if(weightSum >= fci->weightSumLowCutoff && weightSum <= fci->weightSumHighCutoff) { modelRun->phase1RMSEcalls++; computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex); if(modelRun->weightedModelStats.rmsePct < MinRmse) { MinRmse = modelRun->weightedModelStats.rmsePct; saveModelWeights(fci,hoursAheadIndex,hoursAfterSunriseIndex); } }
#define runRMSENoSumCheck() modelRun->phase1RMSEcalls++; computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex); if(modelRun->weightedModelStats.rmsePct < MinRmse) { MinRmse = modelRun->weightedModelStats.rmsePct; saveModelWeights(fci,hoursAheadIndex,hoursAfterSunriseIndex); }
#define runRMSE_2() weightSum = sumWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex); if(weightSum >= 0.97 && weightSum <= 1.03) { modelRun->phase2RMSEcalls++; computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex); if(modelRun->weightedModelStats.rmsePct < MinRmse) { MinRmse = modelRun->weightedModelStats.rmsePct; saveModelWeights(fci,hoursAheadIndex,hoursAfterSunriseIndex); } }

int runOptimizerNested(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int numDivisions = 10;
    double refinementBase, increment;  // just a real version of the above
    int modelIndex, numActiveModels=0;
    int i1,i2,i3,i4,i5,i6,i7,i8,i9,i10;
    modelStatsType *me[MAX_MODELS+1]; //*me1, *me2, *me3, *me4, *me5, *me6, *me7, *me8, *me9, *me10;
    double weightSum;
    modelRunType *modelRun;

    modelRun = hoursAfterSunriseIndex >= 0 ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
        
    InPass1 = True;    
    // intialize things
    // clearHourlyErrorFields(fci, hoursAheadIndex);   no,no
    
//    if(!filterHourlyForecastData(fci, hoursAheadIndex, hoursAfterSunriseIndex))
//        return False;
       
    //int hoursAhead = fci->hoursAheadGroup[hoursAheadIndex].hoursAhead;
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        //modelData->hourlyModelStats[modelIndex].isActive = (getMaxHoursAhead(fci, modelIndex) >= hoursAhead);
        if(modelRun->hourlyModelStats[modelIndex].isUsable) {
            // activeModel[numActiveModels] = modelIndex;   
            me[numActiveModels+1] = &modelRun->hourlyModelStats[modelIndex];
            numActiveModels++;
        }  
    }
    
    increment = 0.1;
    MinRmse = 1000;
    modelRun->phase1RMSEcalls = 0;
    Start_t = time(NULL);
    
    for(i1=0; i1<=numDivisions; i1++) {
        me[1]->weight = i1 * increment;
        //fprintf(stderr, "Model 0, weight %.1f\n", modelRun->hourlyModelStats[0].weight);
        for(i2=0; i2<=numDivisions; i2++) {
            me[2]->weight = i2 * increment;
            //fprintf(stderr, "Model 1, weight %.1f\n", modelRun->hourlyModelStats[1].weight);
            if(numActiveModels == 2) {
                runRMSE();
                continue;  // these continues short-circuit the loops below 
            } else {            
                for(i3=0; i3<=numDivisions; i3++) {  
                    me[3]->weight = i3 * increment;
                    if(numActiveModels == 3) {
                        runRMSE();
                        continue;               
                    } else {
                        for(i4=0; i4<=numDivisions; i4++) {           
                            me[4]->weight = i4 * increment;
                            if(numActiveModels == 4) {
                                runRMSE();
                                continue;               
                            } else {
                                for(i5=0; i5<=numDivisions; i5++) {           
                                    me[5]->weight = i5 * increment;
                                    if(numActiveModels == 5) {
                                        runRMSE();
                                        continue;               
                                    } else {
                                        for(i6=0; i6<=numDivisions; i6++) {           
                                            me[6]->weight = i6 * increment;
                                            if(numActiveModels == 6) {
                                                runRMSE();
                                                continue;               
                                            } else {
                                                for(i7=0; i7<=numDivisions; i7++) {
                                                    me[7]->weight = i7 * increment;
                                                    if(numActiveModels == 7) {
                                                        runRMSE();
                                                        continue;               
                                                    } else {
                                                        for(i8=0; i8<=numDivisions; i8++) {
                                                            me[8]->weight = i8 * increment;
                                                            if(numActiveModels == 8) {
                                                                runRMSE();
                                                                continue;               
                                                            } else {
                                                                for(i9=0; i9<=numDivisions; i9++) {
                                                                    me[9]->weight = i9 * increment;
                                                                    if(numActiveModels == 9) {
                                                                        runRMSE();
                                                                        continue;               
                                                                    } else {
                                                                        for(i10=0; i10<=numDivisions; i10++) {   
                                                                            me[10]->weight = i10 * increment;
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
                                                }  // that's 
                                            }  // a
                                        } // lot
                                    } // of
                                } // right
                            } // parentheses
                        }
                    }
                }
            }
        }
    }

    InPass1 = False;

    fprintf(stderr, "\n=== Elapsed time for phase 1: %s [RMSE calls = %ld] [RMSE = %.2f%%]\n", getElapsedTime(Start_t), modelRun->phase1RMSEcalls, modelRun->optimizedRMSEphase1 * 100);
    dumpWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex, 1);
            
    if(fci->skipPhase2) {
        modelRun->optimizedRMSEphase2 = modelRun->optimizedRMSEphase1;
        return True;
    }
    
    refinementBase = -0.05;
    increment = .01;
    MinRmse = 1000;
    modelRun->phase2RMSEcalls = 0;
    Start_t = time(NULL);
    
    for(i1=0; i1<=numDivisions; i1++) {
        me[1]->weight = me[1]->optimizedWeightPass1 <= 0 ? 0 : me[1]->optimizedWeightPass1 + refinementBase + (i1 * increment);
        if(me[1]->weight <= 0)
            i1 = numDivisions;   // short circuit this since the weight starts at zero
        //fprintf(stderr, "[%s] Model 0, i1 = %d origWeight = %.1f weight = %.3f\n", getElapsedTime(Start_t), i1, modelRun->hourlyModelStats[0].optimizedWeightPass1, modelRun->hourlyModelStats[0].weight);
        for(i2=0; i2<=numDivisions; i2++) {
            me[2]->weight = me[2]->optimizedWeightPass1 <= 0 ? 0 : me[2]->optimizedWeightPass1 + refinementBase + (i2 * increment);
            if(me[2]->weight <= 0)
                i2 = numDivisions;  
            //fprintf(stderr, "[%s] Model 1, i2 = %d origWeight = %.1f weight = %.3f\n", getElapsedTime(Start_t), i2, modelRun->hourlyModelStats[1].optimizedWeightPass1, modelRun->hourlyModelStats[1].weight);
            if(numActiveModels == 2) {
                runRMSE_2();
                continue;
            } else {            
                for(i3=0; i3<=numDivisions; i3++) {  
                    me[3]->weight = me[3]->optimizedWeightPass1 <= 0 ? 0 : me[3]->optimizedWeightPass1 + refinementBase + (i3 * increment);            
                    if(me[3]->weight <= 0)
                        i3 = numDivisions;
                    if(numActiveModels == 3) {
                        runRMSE_2();
                        continue;               
                    } else {
                        for(i4=0; i4<=numDivisions; i4++) {           
                            me[4]->weight = me[4]->optimizedWeightPass1 <= 0 ? 0 : me[4]->optimizedWeightPass1 + refinementBase + (i4 * increment);            
                            if(me[4]->weight <= 0)
                                i4 = numDivisions;
                            if(numActiveModels == 4) {
                                runRMSE_2();
                                continue;               
                            } else {
                                for(i5=0; i5<=numDivisions; i5++) {           
                                    me[5]->weight = me[5]->optimizedWeightPass1 <= 0 ? 0 : me[5]->optimizedWeightPass1 + refinementBase + (i5 * increment);            
                                    if(me[5]->weight < 0)
                                        i5 = numDivisions;
                                    if(numActiveModels == 5) {
                                        runRMSE_2();
                                        continue;               
                                    } else {
                                        for(i6=0; i6<=numDivisions; i6++) {           
                                            me[6]->weight = me[6]->optimizedWeightPass1 <= 0 ? 0 : me[6]->optimizedWeightPass1 + refinementBase + (i6 * increment);            
                                            if(me[6]->weight < 0)
                                                i6 = numDivisions;
                                            if(numActiveModels == 6) {
                                                runRMSE_2();
                                                continue;               
                                            } else {
                                                for(i7=0; i7<=numDivisions; i7++) {
                                                    me[7]->weight = me[7]->optimizedWeightPass1 <= 0 ? 0 : me[7]->optimizedWeightPass1 + refinementBase + (i7 * increment);
                                                    if(me[7]->weight < 0)
                                                        i7 = numDivisions;
                                                    if(numActiveModels == 7) {
                                                        runRMSE_2();
                                                        continue;               
                                                    } else {
                                                        for(i8=0; i8<=numDivisions; i8++) {
                                                            me[8]->weight = me[8]->optimizedWeightPass1 <= 0 ? 0 : me[8]->optimizedWeightPass1 + refinementBase + (i8 * increment);           
                                                            if(me[8]->weight <= 0)
                                                                i8 = numDivisions;
                                                            if(numActiveModels == 8) {
                                                                runRMSE_2();
                                                                continue;               
                                                            } else {
                                                                for(i9=0; i9<=numDivisions; i9++) {
                                                                    me[9]->weight = me[9]->optimizedWeightPass1 <= 0 ? 0 : me[9]->optimizedWeightPass1 + refinementBase + (i9 * increment);
                                                                    if(me[9]->weight <= 0)
                                                                        i9 = numDivisions;
                                                                    if(numActiveModels == 9) {
                                                                        runRMSE_2();
                                                                        continue;               
                                                                    } else {
                                                                        for(i10=0; i10<=numDivisions; i10++) {   
                                                                            me[10]->weight = me[10]->optimizedWeightPass1 <= 0 ? 0 : me[10]->optimizedWeightPass1 + refinementBase + (i10 * increment);
                                                                            if(me[10]->weight <= 0)
                                                                                i10 = numDivisions;
                                                                            if(numActiveModels == 10) {
                                                                                runRMSE_2();
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
        
    fprintf(stderr, "\n=== Elapsed time for phase 2: %s [RMSE calls = %ld] [RMSE = %.2f%%]\n", getElapsedTime(Start_t), modelRun->phase2RMSEcalls, modelRun->optimizedRMSEphase2 * 100);
    dumpWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex, 2);
    
    return True;
}


void dumpWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int phase)
{
    int modelIndex;
    modelRunType *modelRun;

    modelRun = hoursAfterSunriseIndex >= 0 ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    fprintf(stderr, "\n=== Phase %d weights for %s hours ahead %d", phase, fci->siteName, fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
    if(hoursAfterSunriseIndex >= 0)
        fprintf(stderr, ", hours after sunrise %d", hoursAfterSunriseIndex+1);
    fprintf(stderr, ":\n");

    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].isUsable) {           
            fprintf(stderr, "\t%-35s = %-8.2f\n", getGenericModelName(fci, modelIndex), phase < 2 ? modelRun->hourlyModelStats[modelIndex].optimizedWeightPass1 : modelRun->hourlyModelStats[modelIndex].optimizedWeightPass2);
        }
    }    
    fprintf(stderr, "\t\n[Phase %d RMSE = %.2f%%]\n\n", phase, (phase < 2 ? modelRun->optimizedRMSEphase1 : modelRun->optimizedRMSEphase2) * 100);   
}


void saveModelWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int modelIndex;
    modelRunType *modelRun;

    modelRun = hoursAfterSunriseIndex >= 0 ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];    

    MinRmse = modelRun->weightedModelStats.rmsePct;

    if(fci->verbose) {
        if(InPass1)
            fprintf(stderr, "[%ld] @ %s new low RMSE (pass 1) = %.3f%% wts=", modelRun->phase1RMSEcalls, getElapsedTime(Start_t), MinRmse * 100);
        else
            fprintf(stderr, "[%ld] @ %s new low RMSE (pass 2) = %.3f%% wts=", modelRun->phase2RMSEcalls, getElapsedTime(Start_t), MinRmse * 100);
    }
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].isUsable) {
            if(InPass1) {
                modelRun->hourlyModelStats[modelIndex].optimizedWeightPass1 = modelRun->hourlyModelStats[modelIndex].weight;
                if(fci->verbose) 
                    fprintf(stderr, "%.0f ", modelRun->hourlyModelStats[modelIndex].weight * 10);
            }
            else {
                modelRun->hourlyModelStats[modelIndex].optimizedWeightPass2 = modelRun->hourlyModelStats[modelIndex].weight;
                if(fci->verbose) 
                    fprintf(stderr, "%.0f ", modelRun->hourlyModelStats[modelIndex].weight * 100);
            }
        }
    }
    if(fci->verbose)
        fprintf(stderr, "\n");
    
    if(InPass1)
        modelRun->optimizedRMSEphase1 = MinRmse;
    else
        modelRun->optimizedRMSEphase2 = MinRmse;
}

double sumWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int modelIndex;
    static double weightSum;
    modelRunType *modelRun;

    modelRun = hoursAfterSunriseIndex >= 0 ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    weightSum = 0;
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].isUsable) {
            weightSum += modelRun->hourlyModelStats[modelIndex].weight;
            if(weightSum > fci->weightSumHighCutoff) // no need to keep adding to a number that's breached the max weight sum allowed
                return weightSum; 
        }
    }
    
    return weightSum;    
}
