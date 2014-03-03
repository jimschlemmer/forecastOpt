#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include "forecastOpt.h"

void saveModelWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex);
int sumWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex);
void dumpWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunrise, int phase);
int runRMSEwithWeights(forecastInputType *fci, modelRunType *modelRun, int hoursAheadIndex, int hoursAfterSunriseIndex);
int weightSumLimitExceeded(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int maxModelIndex);

time_t Start_t;
double MinRmse;

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
                        fprintf(stderr, "%d ", modelData->hourlyModelStats[i].weight);
                    }
                }
                fprintf(stderr, "\n");      
                if(counter >= 1000000000) 
                    fprintf(stderr, "\t high order info: count=%lld,modulo=%lld/modRem=%.1f/pwrMul=%lld/wt=%d\n",  
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

int runRMSEwithWeights(forecastInputType *fci, modelRunType *modelRun, int hoursAheadIndex, int hoursAfterSunriseIndex) {
    int weightSum;
    double minRmse;
    
    if(fci->inPhase1) {
        minRmse = modelRun->optimizedRMSEphase1;
        modelRun->phase1SumWeightsCalls++; 
    }
    else {
        minRmse = modelRun->optimizedRMSEphase2;
        modelRun->phase2SumWeightsCalls++;
    }
    
    weightSum = sumWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex); 
    if(weightSum > fci->weightSumHighCutoff)
        return False;
    if(weightSum >= fci->weightSumLowCutoff && weightSum <= fci->weightSumHighCutoff) { 
        if(fci->inPhase1) modelRun->phase1RMSEcalls++; 
        else              modelRun->phase2RMSEcalls++;
        computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex); 
        if(modelRun->weightedModelStats.rmsePct < minRmse) { 
            if(fci->inPhase1) modelRun->optimizedRMSEphase1 =  modelRun->weightedModelStats.rmsePct;
            else              modelRun->optimizedRMSEphase2 =  modelRun->weightedModelStats.rmsePct;
            saveModelWeights(fci,hoursAheadIndex,hoursAfterSunriseIndex); 
        } 
    }
    return True;
}

// Macro that's a bit lengthy but which makes the nested loop more readable
#define runRMSE() weightSum = sumWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex); modelRun->phase1SumWeightsCalls++; if(weightSum >= fci->weightSumLowCutoff && weightSum <= fci->weightSumHighCutoff) { modelRun->phase1RMSEcalls++; computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex); if(modelRun->weightedModelStats.rmsePct < MinRmse) { MinRmse = modelRun->weightedModelStats.rmsePct; saveModelWeights(fci,hoursAheadIndex,hoursAfterSunriseIndex); } }
#define runRMSENoSumCheck() modelRun->phase1RMSEcalls++; computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex); if(modelRun->weightedModelStats.rmsePct < MinRmse) { MinRmse = modelRun->weightedModelStats.rmsePct; saveModelWeights(fci,hoursAheadIndex,hoursAfterSunriseIndex); }
#define runRMSE_2() weightSum = sumWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex); modelRun->phase2SumWeightsCalls++; if(weightSum >= 97 && weightSum <= 103) { modelRun->phase2RMSEcalls++; computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex); if(modelRun->weightedModelStats.rmsePct < MinRmse) { MinRmse = modelRun->weightedModelStats.rmsePct; saveModelWeights(fci,hoursAheadIndex,hoursAfterSunriseIndex); } }

#define CheckWeights(n) { if(((numActiveModels - n) > 3) && weightSumLimitExceeded(fci, hoursAheadIndex, hoursAfterSunriseIndex, n)) break; }
#define RunRmse() { runRMSEwithWeights(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex); continue; }

int runOptimizerNested(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int modelIndex, numActiveModels=0;
    int i1,i2,i3,i4,i5,i6,i7,i8,i9,i10;
    modelStatsType *stats[MAX_MODELS+1]; //*stats[1], *stats[2], ...
    modelRunType *modelRun;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
        
    fci->inPhase1 = True;    
    // intialize things
    // clearHourlyErrorFields(fci, hoursAheadIndex);   no,no
    
//    if(!filterHourlyForecastData(fci, hoursAheadIndex, hoursAfterSunriseIndex))
//        return False;
       
    //int hoursAhead = fci->hoursAheadGroup[hoursAheadIndex].hoursAhead;
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        //modelData->hourlyModelStats[modelIndex].isActive = (getMaxHoursAhead(fci, modelIndex) >= hoursAhead);
        if(modelRun->hourlyModelStats[modelIndex].isUsable) {
            // activeModel[numActiveModels] = modelIndex;   
            stats[numActiveModels+1] = &modelRun->hourlyModelStats[modelIndex];
            numActiveModels++;
        }  
    }
    
/*
    For numDivisions =  5, increment1 = 100/5 = 20 => 0,20,40,60,80,100
    For numDivisions =  7, increment1 = 100/7 = 14 => 7,14,28,42,56,70,84,98
    For numDivisions = 10, increment1 = 100/10 = 10 => 10,20,30,40,50,60,70,80,90,100
*/
    //fci->numDivisions = 5;
    fci->increment1 = 100 / fci->numDivisions;
    modelRun->optimizedRMSEphase1 = 1000;
    fprintf(stderr, "======= numDivisions = %d\n", fci->numDivisions);
    fprintf(stderr, "======= increment1 = %d\n\n", fci->increment1);
    modelRun->phase1RMSEcalls = 0;
    modelRun->phase2RMSEcalls = 0;
    modelRun->phase1SumWeightsCalls = 0;
    modelRun->phase2SumWeightsCalls = 0;
    Start_t = time(NULL);
 
    for(i1=0; i1<=fci->numDivisions; i1++) {
        stats[1]->weight = i1 * fci->increment1;
        //fprintf(stderr, "Model 0, weight %.1f\n", modelRun->hourlyModelStats[0].weight);
        for(i2=0; i2<=fci->numDivisions; i2++) {
            stats[2]->weight = i2 * fci->increment1;
            CheckWeights(2)  // if the sum of weights exceeds fci->weightSumHighCutoff, bail
            //fprintf(stderr, "Model 1, weight %.1f\n", modelRun->hourlyModelStats[1].weight);
            if(numActiveModels == 2) {
                RunRmse()  
            } else {            
                for(i3=0; i3<=fci->numDivisions; i3++) {  
                    stats[3]->weight = i3 * fci->increment1;
                    CheckWeights(3)  // this involves a "break" to short circuit current for() loop)
                    if(numActiveModels == 3) 
                        RunRmse()   // this involves a "continue" to short circuit the loops below
                    else {
                        for(i4=0; i4<=fci->numDivisions; i4++) {           
                            stats[4]->weight = i4 * fci->increment1;
                            CheckWeights(4)
                            if(numActiveModels == 4) 
                                RunRmse()
                            else {
                                for(i5=0; i5<=fci->numDivisions; i5++) {           
                                    stats[5]->weight = i5 * fci->increment1;
                                    CheckWeights(5)
                                    if(numActiveModels == 5) 
                                        RunRmse()
                                    else {
                                        for(i6=0; i6<=fci->numDivisions; i6++) {           
                                            stats[6]->weight = i6 * fci->increment1;
                                            CheckWeights(6)
                                            if(numActiveModels == 6) 
                                                RunRmse()
                                            else {
                                                for(i7=0; i7<=fci->numDivisions; i7++) {
                                                    stats[7]->weight = i7 * fci->increment1;
                                                    CheckWeights(7)
                                                    if(numActiveModels == 7) 
                                                        RunRmse()
                                                    else {
                                                        for(i8=0; i8<=fci->numDivisions; i8++) {
                                                            stats[8]->weight = i8 * fci->increment1;
                                                            CheckWeights(8)
                                                            if(numActiveModels == 8) 
                                                                RunRmse()
                                                            else {
                                                                for(i9=0; i9<=fci->numDivisions; i9++) {
                                                                    stats[9]->weight = i9 * fci->increment1;
                                                                    CheckWeights(9)
                                                                    if(numActiveModels == 9) 
                                                                        RunRmse()               
                                                                    else {
                                                                        for(i10=0; i10<=fci->numDivisions; i10++) {   
                                                                            stats[10]->weight = i10 * fci->increment1;
                                                                            CheckWeights(10)
                                                                            if(numActiveModels == 10) 
                                                                                RunRmse()               
                                                                            else {
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

    fci->inPhase1 = False;

    fprintf(stderr, "\n=== Elapsed time for phase 1: %s [RMSE calls = %ld] [RMSE = %.2f%%]\n", getElapsedTime(Start_t), modelRun->phase1RMSEcalls, modelRun->optimizedRMSEphase1 * 100);
    dumpWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex, 1);

    // copy over optimized weights from phase 1 to phase 2 in case phase 2 doesn't improve on phase 1
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].isUsable) {
            modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase2 = modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase1;
        }  
    }
    modelRun->optimizedRMSEphase2 = modelRun->optimizedRMSEphase1;  // start with phase 1 RMSE; if we can't improve upon that stick with it

    // bail out if we're only doing the first phase
    if(fci->skipPhase2) {
        return True;
    }
      
    // For numDivisions = 5, increment1 = 20 : refinementBase = -10
    fci->refinementBase = -(fci->increment1);  // might be better off with -(refinementBase/2)
    fci->increment2 = (2 * fci->increment1/fci->numDivisions);  // 2*20/5 = 8, 2*14/7 = 4, 2*10/10 = 2
    // save weights in case phase 2 doesn't improve upon phase 1
    Start_t = time(NULL);
 
    fprintf(stderr, "======= numDivisions = %d\n", fci->numDivisions);
    fprintf(stderr, "======= refinement base = %d\n", fci->refinementBase);
    fprintf(stderr, "======= increment2 = %d\n\n", fci->increment2);
 
    for(i1=0; i1<=fci->numDivisions; i1++) {
        if(stats[1]->optimizedWeightPhase1 > 0) stats[1]->weight = stats[1]->optimizedWeightPhase1 + fci->refinementBase + (i1 * fci->increment2);
        else { stats[1]->weight = 0; i1 = fci->numDivisions+1; }  // short circuit this since the weight starts at zero
        //fprintf(stderr, "[%s] Model 0, i1 = %d origWeight = %.1f weight = %.3f\n", getElapsedTime(Start_t), i1, modelRun->hourlyModelStats[0].optimizedWeightPhase1, modelRun->hourlyModelStats[0].weight);
        for(i2=0; i2<=fci->numDivisions; i2++) {
            if(stats[2]->optimizedWeightPhase1 > 0) stats[2]->weight = stats[2]->optimizedWeightPhase1 + fci->refinementBase + (i2 * fci->increment2);
            else { stats[2]->weight = 0; i2 = fci->numDivisions+1; } 
            CheckWeights(2) 
            //fprintf(stderr, "[%s] Model 1, i2 = %d origWeight = %.1f weight = %.3f\n", getElapsedTime(Start_t), i2, modelRun->hourlyModelStats[1].optimizedWeightPhase1, modelRun->hourlyModelStats[1].weight);
            if(numActiveModels == 2) {
                RunRmse()
            } else {            
                for(i3=0; i3<=fci->numDivisions; i3++) {  
                    if(stats[3]->optimizedWeightPhase1 > 0) stats[3]->weight = stats[3]->optimizedWeightPhase1 + fci->refinementBase + (i3 * fci->increment2);            
                    else { stats[3]->weight = 0; i3 = fci->numDivisions+1; }
                    CheckWeights(3)
                    if(numActiveModels == 3) {
                        RunRmse()               
                    } else {
                        for(i4=0; i4<=fci->numDivisions; i4++) {           
                            if(stats[4]->optimizedWeightPhase1 > 0) stats[4]->weight = stats[4]->optimizedWeightPhase1 + fci->refinementBase + (i4 * fci->increment2);            
                            else { stats[4]->weight = 0; i4 = fci->numDivisions+1; }
                            CheckWeights(4)
                            if(numActiveModels == 4) {
                                RunRmse()               
                            } else {
                                for(i5=0; i5<=fci->numDivisions; i5++) {           
                                    if(stats[5]->optimizedWeightPhase1 > 0) stats[5]->weight =  stats[5]->optimizedWeightPhase1 + fci->refinementBase + (i5 * fci->increment2);            
                                    else { stats[5]->weight = 0; i5 = fci->numDivisions+1; }
                                    CheckWeights(5)
                                    if(numActiveModels == 5) {
                                        RunRmse()               
                                    } else {
                                        for(i6=0; i6<=fci->numDivisions; i6++) {           
                                            if(stats[6]->optimizedWeightPhase1 > 0) stats[6]->weight = stats[6]->optimizedWeightPhase1 + fci->refinementBase + (i6 * fci->increment2);            
                                            else { stats[6]->weight = 0; i6 = fci->numDivisions+1; }
                                            CheckWeights(6)
                                            if(numActiveModels == 6) {
                                                RunRmse()               
                                            } else {
                                                for(i7=0; i7<=fci->numDivisions; i7++) {
                                                    if(stats[7]->optimizedWeightPhase1 > 0) stats[7]->weight = stats[7]->optimizedWeightPhase1 + fci->refinementBase + (i7 * fci->increment2);
                                                    else { stats[7]->weight = 0; i7 = fci->numDivisions+1; }
                                                    CheckWeights(7)
                                                    if(numActiveModels == 7) {
                                                        RunRmse()               
                                                    } else {
                                                        for(i8=0; i8<=fci->numDivisions; i8++) {
                                                            if(stats[8]->optimizedWeightPhase1 > 0) stats[8]->weight = stats[8]->optimizedWeightPhase1 + fci->refinementBase + (i8 * fci->increment2);           
                                                            else { stats[8]->weight = 0; i8 = fci->numDivisions+1; }
                                                            CheckWeights(8)
                                                            if(numActiveModels == 8) {
                                                                RunRmse()               
                                                            } else {
                                                                for(i9=0; i9<=fci->numDivisions; i9++) {
                                                                    if(stats[9]->optimizedWeightPhase1 > 0) stats[9]->weight = stats[9]->optimizedWeightPhase1 + fci->refinementBase + (i9 * fci->increment2);
                                                                    else { stats[9]->weight = 0; i9 = fci->numDivisions+1; }
                                                                    CheckWeights(9)
                                                                    if(numActiveModels == 9) {
                                                                        RunRmse()               
                                                                    } else {
                                                                        for(i10=0; i10<=fci->numDivisions; i10++) {   
                                                                            if(stats[10]->optimizedWeightPhase1 > 0) stats[10]->weight = stats[10]->optimizedWeightPhase1 + fci->refinementBase + (i10 * fci->increment2);
                                                                            else { stats[10]->weight = 0; i10 = fci->numDivisions+1; }
                                                                            CheckWeights(10)
                                                                            if(numActiveModels == 10) {
                                                                                RunRmse()               
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

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    fprintf(stderr, "\n=== Phase %d weights for %s hours ahead %d", phase, genProxySiteName(fci), fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
    if(hoursAfterSunriseIndex >= 0)
        fprintf(stderr, ", hours after sunrise %d", hoursAfterSunriseIndex+1);
    fprintf(stderr, ":\n");

    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].isUsable) {           
            fprintf(stderr, "\t%-35s = %-8d\n", getGenericModelName(fci, modelIndex), phase < 2 ? modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase1 : modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase2);
        }
    }    
    fprintf(stderr, "\t\n[Phase %d RMSE = %.2f%%]\n\n", phase, (phase < 2 ? modelRun->optimizedRMSEphase1 : modelRun->optimizedRMSEphase2) * 100);   
}


void saveModelWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int modelIndex;
    modelRunType *modelRun;
    double minRmse;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];    

    minRmse = modelRun->weightedModelStats.rmsePct;

    if(fci->verbose) {
        if(fci->inPhase1)
            fprintf(stderr, "%ld sumWeight and %ld RMSE calls @ %s new low RMSE (phase 1) = %.3f%% wts=", modelRun->phase1SumWeightsCalls, modelRun->phase1RMSEcalls, getElapsedTime(Start_t), minRmse * 100);
        else
            fprintf(stderr, "%ld sumWeight and %ld RMSE calls  @ %s new low RMSE (phase 2) = %.3f%% wts=", modelRun->phase2SumWeightsCalls, modelRun->phase2RMSEcalls, getElapsedTime(Start_t), minRmse * 100);
    }
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].isUsable) {
            if(fci->inPhase1) {
                modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase1 = modelRun->hourlyModelStats[modelIndex].weight;
                if(fci->verbose) 
                    fprintf(stderr, "%d ", modelRun->hourlyModelStats[modelIndex].weight);
            }
            else {
                modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase2 = modelRun->hourlyModelStats[modelIndex].weight;
                if(fci->verbose) 
                    fprintf(stderr, "%d ", modelRun->hourlyModelStats[modelIndex].weight);
            }
        }
    }
    if(fci->verbose)
        fprintf(stderr, "\n");
    
    if(fci->inPhase1)
        modelRun->optimizedRMSEphase1 = minRmse;
    else
        modelRun->optimizedRMSEphase2 = minRmse;
}

//#define DUMP_ALL_WEIGHTS
int sumWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int modelIndex;
    static int weightSum;
    modelRunType *modelRun;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
    weightSum = 0;
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].isUsable) {
#ifdef DUMP_ALL_WEIGHTS
            fprintf(stderr, "%d-%d ", modelIndex, modelRun->hourlyModelStats[modelIndex].weight);
#endif
            weightSum += (modelRun->hourlyModelStats[modelIndex].weight < 0 ? 0 : modelRun->hourlyModelStats[modelIndex].weight);  // ignore negative weights
            if(weightSum > fci->weightSumHighCutoff)// no need to keep adding to a number that's breached the max weight sum allowed
                break; //return weightSum; 
        }
    }
#ifdef DUMP_ALL_WEIGHTS
    fprintf(stderr, " : sum = %d\n", weightSum);
#endif    
    return weightSum;    
}

int weightSumLimitExceeded(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int maxModelIndex)
{
    int modelIndex;
    static int weightSum;
    modelRunType *modelRun;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
    weightSum = 0;
    for(modelIndex=0; modelIndex < maxModelIndex-1; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].isUsable) {
#ifdef DUMP_ALL_WEIGHTS
            fprintf(stderr, "%d-%d ", modelIndex, modelRun->hourlyModelStats[modelIndex].weight);
#endif
            weightSum += (modelRun->hourlyModelStats[modelIndex].weight < 0 ? 0 : modelRun->hourlyModelStats[modelIndex].weight);  // ignore negative weights
            if(weightSum > fci->weightSumHighCutoff)// no need to keep adding to a number that's breached the max weight sum allowed
                return True; //return weightSum; 
        }
    }
#ifdef DUMP_ALL_WEIGHTS
    fprintf(stderr, " : sum = %d\n", weightSum);
#endif    
    return False;    
}