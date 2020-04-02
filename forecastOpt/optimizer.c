#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <omp.h>
#include "forecastOpt.h"
#include "forecastOptUtils.h"

void saveModelWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex);
void loadOptimizedModelWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex);
int sumWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int numModels);
void dumpWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunrise, int ktIndex, int phase);
int runRMSEwithWeights(forecastInputType *fci, modelRunType *modelRun, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex);
int runCostWithWeights(forecastInputType *fci, modelRunType *modelRun, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int runIndex);
int weightsInRange(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex);
int weightsRangeExceeded(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex);
int runWeightSet(forecastInputType *fci, modelRunType *modelRun, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int runIndex);
void cleanUpTimeSeries(forecastInputType *fci, int hourAheadIndex);
int cmpcost(const void *p1, const void *p2);
void dumpAndFree(forecastInputType *fci, modelRunType *modelRun, int numGoodWeightSets);

time_t Start_t;
double MinRmse;

/*
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
    
    if(!falterForecastData(fci, hoursAheadIndex, -1))
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

            }

        }
//                    fprintf(stderr, "\n");

        if(weightSum >= 0.9 && weightSum <= 1.1) {
            computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, -1, GROUND_REF);
            if(modelData->weightedModelStatsVsGround.rmsePct < minRmse) {
                minRmse = modelData->weightedModelStatsVsGround.rmsePct;

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
 */

char *getElapsedTime(time_t start_t)
{
    static char timeStr[1024];

    time_t elapsed_t = time(NULL) - start_t;

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

    return (timeStr);
}

int runWeightSet(forecastInputType *fci, modelRunType *modelRun, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int runIndex)
{
    int retVal;

    if(fci->errorMetric == RMSE) {
        retVal = runRMSEwithWeights(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);

    }
    else if(fci->errorMetric == Cost) {
        retVal = runCostWithWeights(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, runIndex);
    }

    else {
        fprintf(stderr, "No error metric defined\n");
        exit(1);
    }

    return retVal;
}

int runCostWithWeights(forecastInputType *fci, modelRunType *modelRun, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int runIndex)
{
    //    int weightSum;
    double minCost;

    //fprintf(stderr, "Running with runIndex %d run on thread %d\n", runIndex, omp_get_thread_num());

    if(fci->inPhase1) {
        minCost = modelRun->optimizedMetricPhase1;
        modelRun->phase1MetricCalls++;
    }
    else {
        minCost = modelRun->optimizedMetricPhase2;
        modelRun->phase2MetricCalls++;
    }

    //#define WEIGHT_TRIALS

    //    weightSum = sumWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);

    //    if(weightSum > fci->weightSumHighCutoff) {
#ifdef WEIGHT_TRIALS
    fprintf(stderr, " SUM_TOO_HIGH\n");
#endif
    //        return False;
    //    }
    //    else if(weightSum < fci->weightSumLowCutoff) {
#ifdef WEIGHT_TRIALS    
    fprintf(stderr, " SUM_TOO_LOW\n");
#endif    
    //        return False;
    //    }

    // else
#ifdef WEIGHT_TRIALS    
    fprintf(stderr, " RUNNING\n");
#endif        
    computeHourlyCostWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, USE_GROUND_REF, runIndex);

    return True; // skip all the saving of parameters if we're running in parallel






    //fprintf(stderr, "checking minCost...\n");
    double lowestCost = modelRun->weightedModelStatsVsGround.lowestCostParameters.total_cost;
    if(lowestCost < minCost) {
        if(fci->inPhase1) {
            // for the Cost algo, optimizedPctMetric and optimizedMetric are the same
            modelRun->optimizedPctMetricPhase1 = modelRun->optimizedMetricPhase1 = lowestCost;
        }
        else {
            //fprintf(stderr, "[HA%d/HAS%d] RMSE=%.0f, %%RMSE=%.4f\n", hoursAheadIndex+1, hoursAfterSunriseIndex+1, modelRun->weightedModelStatsVsGround.rmse, modelRun->weightedModelStatsVsGround.rmsePct);
            modelRun->optimizedPctMetricPhase2 = modelRun->optimizedMetricPhase2 = lowestCost;
        }
        saveModelWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);
    }
    else {
        //fprintf(stderr, "Not saving model weights\n");
    }
    //fprintf(stderr, "finished...\n");

    //}
    return True;
}

int runRMSEwithWeights(forecastInputType *fci, modelRunType *modelRun, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex)
{
    double minRmse;

    if(fci->inPhase1) {
        minRmse = modelRun->optimizedMetricPhase1;
        modelRun->phase1SumWeightsCalls++;
    }
    else {
        minRmse = modelRun->optimizedMetricPhase2;
        modelRun->phase2SumWeightsCalls++;
    }

    //#define WEIGHT_TRIALS

    if(fci->weightSum > fci->weightSumHighCutoff) {
#ifdef WEIGHT_TRIALS
        fprintf(stderr, " SUM_TOO_HIGH\n");
#endif
        return False;
    }
    else if(fci->weightSum < fci->weightSumLowCutoff) {
#ifdef WEIGHT_TRIALS    
        fprintf(stderr, " SUM_TOO_LOW\n");
#endif    
        return False;
    }

    // else
#ifdef WEIGHT_TRIALS    
    fprintf(stderr, " RUNNING\n");
#endif        
    //if(fci->weightSum >= fci->weightSumLowCutoff && weightSum <= fci->weightSumHighCutoff) {
    if(fci->inPhase1)
        modelRun->phase1MetricCalls++;
    else
        modelRun->phase2MetricCalls++;

    computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, USE_GROUND_REF);

    //fprintf(stderr, "previous minRmse = %.2f   current minRmse = %.2f\n", modelRun->weightedModelStatsVsGround.rmsePct, minRmse);

    if(modelRun->weightedModelStatsVsGround.rmsePct < minRmse) {
        if(fci->inPhase1) {
            modelRun->optimizedPctMetricPhase1 = modelRun->weightedModelStatsVsGround.rmsePct;
            modelRun->optimizedMetricPhase1 = modelRun->weightedModelStatsVsGround.rmsePct;
        }
        else {
            //fprintf(stderr, "[HA%d/HAS%d] RMSE=%.0f, %%RMSE=%.4f\n", hoursAheadIndex+1, hoursAfterSunriseIndex+1, modelRun->weightedModelStatsVsGround.rmse, modelRun->weightedModelStatsVsGround.rmsePct);
            modelRun->optimizedPctMetricPhase2 = modelRun->weightedModelStatsVsGround.rmsePct;
            modelRun->optimizedMetricPhase2 = modelRun->weightedModelStatsVsGround.rmsePct;
        }
        saveModelWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);
    }
    //}
    return True;
}

// Macro that's a bit lengthy but which makes the nested loop more readable
//#define runRMSE() weightSum = sumWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex); modelRun->phase1SumWeightsCalls++; if(weightSum >= fci->weightSumLowCutoff && weightSum <= fci->weightSumHighCutoff) { modelRun->phase1MetricCalls++; computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex); if(modelRun->weightedModelStatsVsGround.rmsePct < MinRmse) { MinRmse = modelRun->weightedModelStatsVsGround.rmsePct; saveModelWeights(fci,hoursAheadIndex,hoursAfterSunriseIndex); } }
//#define runRMSENoSumCheck() modelRun->phase1MetricCalls++; computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex); if(modelRun->weightedModelStatsVsGround.rmsePct < MinRmse) { MinRmse = modelRun->weightedModelStatsVsGround.rmsePct; saveModelWeights(fci,hoursAheadIndex,hoursAfterSunriseIndex); }
//#define runRMSE_2() weightSum = sumWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex); modelRun->phase2SumWeightsCalls++; if(weightSum >= 97 && weightSum <= 103) { modelRun->phase2MetricCalls++; computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex); if(modelRun->weightedModelStatsVsGround.rmsePct < MinRmse) { MinRmse = modelRun->weightedModelStatsVsGround.rmsePct; saveModelWeights(fci,hoursAheadIndex,hoursAfterSunriseIndex); } }

//#define CheckWeights(n) { if(((numActiveModels - n) > 3) && weightsInRange(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, n)) break; }

// the CheckWeights() macro sums up weights and breaks out of the current loop if the sum is exceeded
#define CheckWeights(n) { sumWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, n); if(fci->weightSum > fci->weightSumHighCutoff) { stats[n]->weight = 0; break;} }

//#define OptimizeWeights() { sumWeights(fci, n); if(fci->weightSum > fci->weightSumHighCutoff) break; if(fci->weightSum >= fci->weightSumLowCutoff) { /*runWeightSet(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);*/ } }
#define OptimizeWeights() { if(fci->weightSum >= fci->weightSumLowCutoff) { runWeightSet(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, 0);} }
//#define OptimizeWeights() { if(fci->weightSum >= fci->weightSumLowCutoff) { numRuns++;} }


#define NESTED_OPT_DEBUG

// In order to optimize nested loops, the 

int runOptimizerParallel(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex)
{
    int modelIndex, numActiveModels = 0;
    int i, i1, i2, i3, i4, i5, i6, j;
    modelStatsType * stats[MAX_MODELS + 1]; //*stats[1], *stats[2], ...
    modelRunType *modelRun;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    fci->inPhase1 = True;

    fprintf(stderr, "Max num threads = %d\n", omp_get_max_threads());
    fprintf(stderr, "Num processors  = %d\n", omp_get_num_procs());

    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        //modelData->hourlyModelStats[modelIndex].isActive = (getMaxHoursAhead(fci, modelIndex) >= hoursAhead);
        if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
            // activeModel[numActiveModels] = modelIndex;   
            stats[numActiveModels + 1] = &modelRun->hourlyModelStats[modelIndex];
            numActiveModels++;
        }
    }

    if(numActiveModels < 1) {
        fprintf(stderr, "\n!!! Warning: no models active for current hour; no data to work with\n");
        return False;
    }
    if(numActiveModels > 11) {
        fprintf(stderr, "\n!!! Warning: number of active models [%d] > current max of 11\n", numActiveModels);
        return False;
    }
    modelRun->optimizedPctMetricPhase1 = 10000;
    modelRun->optimizedMetricPhase1 = 10000;
    modelRun->phase1MetricCalls = 0;
    modelRun->phase2MetricCalls = 0;
    modelRun->phase1SumWeightsCalls = 0;
    modelRun->phase2SumWeightsCalls = 0;

    if(modelRun->numValidSamples == 0)
        return True;

    Start_t = time(NULL);

    // allocate list for lowCost list that all threads report their results to
    int numAllocatedWeightSets = 2000;
    fci->lowCostList = (cost_type **) malloc(numAllocatedWeightSets * sizeof (cost_type *));

    // this is all pretty hard-coded right now
    int inc = fci->increment1;
    int numGoodWeightSets = 0;
    for(i1 = 0; i1 <= fci->numDivisions; i1++) {
        for(i2 = 0; i2 <= fci->numDivisions; i2++) {
            for(i3 = 0; i3 <= fci->numDivisions; i3++) {
                for(i4 = 0; i4 <= fci->numDivisions; i4++) {
                    for(i5 = 0; i5 <= fci->numDivisions; i5++) {
                        for(i6 = 0; i6 <= fci->numDivisions; i6++) {
                            int sum = i1 * inc + i2 * inc + i3 * inc + i4 * inc + i5 * inc + i6*inc;
                            if(sum >= fci->weightSumLowCutoff && sum <= fci->weightSumHighCutoff) {
                                fci->lowCostList[numGoodWeightSets] = (cost_type *) malloc(sizeof (cost_type));
                                fci->lowCostList[numGoodWeightSets]->weights[0] = i1*inc;
                                fci->lowCostList[numGoodWeightSets]->weights[1] = i2*inc;
                                fci->lowCostList[numGoodWeightSets]->weights[2] = i3*inc;
                                fci->lowCostList[numGoodWeightSets]->weights[3] = i4*inc;
                                fci->lowCostList[numGoodWeightSets]->weights[4] = i5*inc;
                                fci->lowCostList[numGoodWeightSets]->weights[5] = i6*inc;
                                numGoodWeightSets++;
                                if(numGoodWeightSets == numAllocatedWeightSets) {
                                    numAllocatedWeightSets *= 2;
                                    fprintf(stderr, "=== Scaling up weightset memory to %d sets ===\n", numAllocatedWeightSets);
                                    fci->lowCostList = (cost_type **) realloc(fci->lowCostList, numAllocatedWeightSets * sizeof (cost_type *));
                                }
                            }
                        }
                    }
                }
            } // that's 
        } // a
    } // lot

    fprintf(stderr, "Got %d good weight sets : %d allocated\n", numGoodWeightSets, numAllocatedWeightSets);

    cleanUpTimeSeries(fci, hoursAheadIndex);

    ///////// Main Loop ////////////////

#pragma omp parallel for schedule(static)
    for(i = 0; i < numGoodWeightSets; i++) {
        runWeightSet(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, i);
    }

    fprintf(stderr, "\n=== Total elapsed time in phase 1 : %ld seconds\n", time(NULL) - Start_t);

    ////////////////////////////////////

    // sort results -- lowest cost will be first
    qsort(fci->lowCostList, numGoodWeightSets, sizeof (cost_type *), cmpcost);

    // copy out the top 10 so we can reuse the fci->lowCostList
    cost_type lowCostPhase1[10];
    for(i = 0; i < 10; i++) {
        for(j = 0; j < fci->numModels; j++)
            lowCostPhase1[i].weights[j] = fci->lowCostList[i]->weights[j];
    }
    
    dumpAndFree(fci, modelRun, numGoodWeightSets);

    /////////////////////////////////////  phase 2
    fci->inPhase1 = False;

    int numLowMetrics = 1; // how many of the lowest cost weight sets should we optimize over?
    // 8 -> 4 -> [-4, -3, -2, -1, 0 , 1, 2, 3, 4]
    fci->increment2 = inc = 1;
    int step = fci->increment1 / 2;
    numGoodWeightSets = 0;
    numAllocatedWeightSets = 2000;
    fci->lowCostList = (cost_type **) malloc(numAllocatedWeightSets * sizeof (cost_type *));

    char skipZeroWeights = 1;
    int wt1, wt2, wt3, wt4, wt5, wt6;

    for(i = 0; i < numLowMetrics; i++) {
        int lowWt1 = lowCostPhase1[i].weights[0];
        int lowWt2 = lowCostPhase1[i].weights[1];
        int lowWt3 = lowCostPhase1[i].weights[2];
        int lowWt4 = lowCostPhase1[i].weights[3];
        int lowWt5 = lowCostPhase1[i].weights[4];
        int lowWt6 = lowCostPhase1[i].weights[5];
        for(i1 = -step; i1 <= step; i1++) {
            if(lowWt1 == 0 && skipZeroWeights) {
                wt1 = 0;
                i1 = step;
            }
            else wt1 = lowWt1 + i1;
            if(wt1 < 0) continue;
            for(i2 = -step; i2 <= step; i2++) {
                if(lowWt2 == 0 && skipZeroWeights) {
                    wt2 = 0;
                    i2 = step;
                }
                else wt2 = lowWt2 + i2;
                if(wt2 < 0) continue;
                for(i3 = -step; i3 <= step; i3++) {
                    if(lowWt3 == 0 && skipZeroWeights) {
                        wt3 = 0;
                        i3 = step;
                    }
                    else wt3 = lowWt3 + i3;
                    if(wt3 < 0) continue;
                    for(i4 = -step; i4 <= step; i4++) {
                        if(lowWt4 == 0 && skipZeroWeights) {
                            wt4 = 0;
                            i4 = step;
                        }
                        else wt4 = lowWt4 + i4;
                        if(wt4 < 0) continue;
                        for(i5 = -step; i5 <= step; i5++) {
                            if(lowWt5 == 0 && skipZeroWeights) {
                                wt5 = 0;
                                i5 = step;
                            }
                            else wt5 = lowWt5 + i5;
                            if(wt5 < 0) continue;
                            for(i6 = -step; i6 <= step; i6++) {
                                if(lowWt6 == 0 && skipZeroWeights) {
                                    wt6 = 0;
                                    i6 = step;
                                }
                                else wt6 = lowWt6 + i6;
                                if(wt6 < 0) continue;
                                int sum = wt1 + wt2 + wt3 + wt4 + wt5 + wt6;
                                if(sum >= fci->weightSumLowCutoff && sum <= fci->weightSumHighCutoff) {
                                    //fprintf(stderr, "%d|%d %d|%d %d|%d %d|%d %d|%d %d|%d sum = %d\n", lowWt1, wt1, lowWt2, wt2, lowWt3, wt3, lowWt4, wt4, lowWt5, wt5, lowWt6, wt6, sum);
                                    fci->lowCostList[numGoodWeightSets] = (cost_type *) malloc(sizeof (cost_type));
                                    fci->lowCostList[numGoodWeightSets]->weights[0] = wt1;
                                    fci->lowCostList[numGoodWeightSets]->weights[1] = wt2;
                                    fci->lowCostList[numGoodWeightSets]->weights[2] = wt3;
                                    fci->lowCostList[numGoodWeightSets]->weights[3] = wt4;
                                    fci->lowCostList[numGoodWeightSets]->weights[4] = wt5;
                                    fci->lowCostList[numGoodWeightSets]->weights[5] = wt6;
                                    numGoodWeightSets++;
                                    if(numGoodWeightSets == numAllocatedWeightSets) {
                                        numAllocatedWeightSets *= 2;
                                        fprintf(stderr, "=== Scaling up weightset memory to %d sets ===\n", numAllocatedWeightSets);
                                        fci->lowCostList = (cost_type **) realloc(fci->lowCostList, numAllocatedWeightSets * sizeof (cost_type *));
                                    }
                                }
                            }
                        }
                    }
                } // that's 
            } // a
        } // lot
    }

    for(i = 0; i < numGoodWeightSets; i++) {
        int sum = 0;
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
                fprintf(stderr, "%s=%2d ", getModelName(fci, modelIndex), fci->lowCostList[i]->weights[modelIndex]);
                sum += fci->lowCostList[i]->weights[modelIndex];
            }
        }
        fprintf(stderr, "sum=%d\n", sum);
    }

    fprintf(stderr, "numGoodWeightSets = %d\n", numGoodWeightSets);

#pragma omp parallel for schedule(static)
    for(i = 0; i < numGoodWeightSets; i++) {
        runWeightSet(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, i);
    }

    // sort results -- lowest cost will be first
    qsort(fci->lowCostList, numGoodWeightSets, sizeof (cost_type *), cmpcost);
    dumpAndFree(fci, modelRun, numGoodWeightSets);

    return True;
}

void dumpAndFree(forecastInputType *fci, modelRunType *modelRun, int numGoodWeightSets)
{
    int i, modelIndex;
    char outputFile[2024];
    FILE *outputFP;
    static int phase = 1;

    // dump cost data to file
    sprintf(outputFile, "%s/lowestCostData.%dcores.HA%d.percentInc_%d.phase%d.csv", fci->outputDirectory, 
            fci->omp_num_threads, modelRun->hoursAhead, fci->increment1, phase);
    if((outputFP = fopen(outputFile, "w")) == NULL) {
        fprintf(stderr, "Couldn't open weights file %s\n", outputFile);
        exit(1);
    }

    fprintf(outputFP, "#modelWeights,total_cost,max_rate,oversize,max_battery_size,storage_size,total_recharge,total_recharge_cost,min_peak_to_trough_v4,total_curtailment,total_loss,total_energy_v3_over,total_energy_v4\n");
    for(i = 0; i < numGoodWeightSets; i++) {
        int sum = 0;
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {

                fprintf(outputFP, "%s=%2d ", getModelName(fci, modelIndex), fci->lowCostList[i]->weights[modelIndex]);
                sum += fci->lowCostList[i]->weights[modelIndex];
            }
        }
        fprintf(outputFP, "sum=%d,", sum);

        cost_type *c = fci->lowCostList[i];
        fprintf(outputFP, "%.1f,%.02f,%.02f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n",
                c->total_cost, c->max_rate, c->oversize, c->max_battery_size, c->storage_size, c->total_recharge, c->total_recharge_cost, c->min_peak_to_trough_v4, c->total_curtailment, c->total_loss, c->total_energy_v3_over, c->total_energy_v4);
    }
    fclose(outputFP);

    // optionally dump lowest cost time-series
    if(fci->saveLowCostTimeSeries) {
        cost_type *lowestCostData = fci->lowCostList[0];
        cost_timeseries_type *ts_data = lowestCostData->lowestCostTimeSeries;

        sprintf(outputFile, "%s/lowestCostTS.Rate_%.1f.Ovrsz_%.2f.Bat_%.0f.%dcores.HA%d.percentInc_%d.phase%d.csv", fci->outputDirectory,
                lowestCostData->max_rate, lowestCostData->oversize, lowestCostData->max_battery_size, fci->omp_num_threads, modelRun->hoursAhead, fci->increment1, phase);
        if((outputFP = fopen(outputFile, "w")) == NULL) {
            fprintf(stderr, "Couldn't open T/S file %s\n", outputFile);
            exit(1);
        }
        fprintf(outputFP, "#Max_Rate=%.1f,Oversize=%.2f,Max_Battery_Size=%.1f\n", lowestCostData->max_rate, lowestCostData->oversize, lowestCostData->max_battery_size);
        fprintf(outputFP, "year,mon,day,hour,min,v3,v4,v3over,v3_v4,recharge_v3_v4,c_v3_v4,peak_v4,trough_v4,peak_to_trough_v4,storage_size,total_recharge,total_v3,total_v4,state_of_charge,curtailment\n");
        for(i = 1; i < fci->numTotalSamples; i++) {
            fprintf(outputFP, "%s,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.1f\n",
                    dtToStringCsvCompact(&ts_data[i].dateTime), ts_data[i].v3, ts_data[i].v4, ts_data[i].v3 * lowestCostData->oversize, ts_data[i].v3_v4, ts_data[i].recharge_v3_v4, ts_data[i].c_v3_v4, ts_data[i].peak_v4, ts_data[i].trough_v4,
                    ts_data[i].peak_to_trough_v4, ts_data[i].storage_size, ts_data[i].total_recharge, ts_data[i].total_energy_v3_over, ts_data[i].total_energy_v4, ts_data[i].state_of_charge, ts_data[i].curtailment_v4);
        } //ts_data[i].v3 = ts_data[i].v3 / Oversize; // scale back
    }
    fclose(outputFP);

    for(i = 0; i < numGoodWeightSets; i++)
        free(fci->lowCostList[i]);

    free(fci->lowCostList);
    phase++;
}

int cmpcost(const void *p1, const void *p2)
{
    cost_type *pp1 = *(cost_type **) p1;
    cost_type *pp2 = *(cost_type **) p2;

    if(pp1->total_cost < pp2->total_cost) return -1;
    if(pp1->total_cost > pp2->total_cost) return 1;
    return 0;
}

// this just gets rid of everything except 'OK' and night data -- in prep for the cost algorithm

void cleanUpTimeSeries(forecastInputType *fci, int hoursAheadIndex)
{
    int sampleInd, newNumSamples = 0;
    timeSeriesType *thisSample, *newTimeSeries;

    if((newTimeSeries = (timeSeriesType *) malloc(sizeof (timeSeriesType) * fci->numTotalSamples)) == NULL) {
        fprintf(stderr, "Memory alloc failure in cleanUpTimeSeries()\n");
        exit(1);
    }

    for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->forecastData[hoursAheadIndex].groupIsValid == OK || thisSample->forecastData[hoursAheadIndex].groupIsValid == zenith) {
            memcpy(&newTimeSeries[newNumSamples], &fci->timeSeries[sampleInd], sizeof (timeSeriesType));
            newNumSamples++;
        }
    }

    fci->timeSeries = newTimeSeries;
    fci->numTotalSamples = newNumSamples;
}

int runOptimizerNested(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex)
{
    int modelIndex, numActiveModels = 0;
    int i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11;
    modelStatsType * stats[MAX_MODELS + 1]; //*stats[1], *stats[2], ...
    modelRunType *modelRun;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    fci->inPhase1 = True;

    // intialize things
    // clearHourlyErrorFields(fci, hoursAheadIndex);   no,no

    //    if(!filterForecastData(fci, hoursAheadIndex, hoursAfterSunriseIndex))
    //        return False;

    //int hoursAhead = fci->hoursAheadGroup[hoursAheadIndex].hoursAhead;
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        //modelData->hourlyModelStats[modelIndex].isActive = (getMaxHoursAhead(fci, modelIndex) >= hoursAhead);
        if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
            // activeModel[numActiveModels] = modelIndex;   
            stats[numActiveModels + 1] = &modelRun->hourlyModelStats[modelIndex];
            numActiveModels++;
        }
    }

    if(numActiveModels < 1) {
        fprintf(stderr, "\n!!! Warning: no models active for current hour; no data to work with\n");
        return False;
    }
    if(numActiveModels > 11) {
        fprintf(stderr, "\n!!! Warning: number of active models [%d] > current max of 11\n", numActiveModels);
        return False;
    }
    modelRun->optimizedPctMetricPhase1 = 10000;
    modelRun->optimizedMetricPhase1 = 10000;
    modelRun->phase1MetricCalls = 0;
    modelRun->phase2MetricCalls = 0;
    modelRun->phase1SumWeightsCalls = 0;
    modelRun->phase2SumWeightsCalls = 0;

    if(modelRun->numValidSamples == 0)
        return True;

    Start_t = time(NULL);
    //    int numRuns = 0;

    /*
        int tid, nthreads;
    #pragma omp parallel shared(nthreads) private(i1,i2,i3,i4,i5,i6,i7,i8,i9,i10,tid)
        tid = omp_get_thread_num();
        if(tid == 0) {
            nthreads = omp_get_num_threads();
            printf("Number of threads = %d\n", nthreads);
        }

        printf("Thread %d starting...\n", tid);
     */

    //#pragma omp parallel for collapse(10)
    for(i1 = 0; i1 <= fci->numDivisions; i1++) {
        stats[1]->weight = i1 * fci->increment1;
        //CheckWeights(1) // if the sum of weights exceeds fci->weightSumHighCutoff, bail
        if(numActiveModels == 1) { // bail out
            stats[1]->weight = 100;
            CheckWeights(1)
            OptimizeWeights()
                    // break;
        }
        else {
            for(i2 = 0; i2 <= fci->numDivisions; i2++) {
                stats[2]->weight = i2 * fci->increment1;
                CheckWeights(2) // if the sum of weights exceeds fci->weightSumHighCutoff, bail
                if(numActiveModels == 2)
                    OptimizeWeights()
                else {
                    for(i3 = 0; i3 <= fci->numDivisions; i3++) {
                        stats[3]->weight = i3 * fci->increment1;
                        CheckWeights(3) // this involves a "break" to short circuit current for() loop)
                        if(numActiveModels == 3)
                            OptimizeWeights() // this involves a "continue" to short circuit the loops below
                        else {
                            for(i4 = 0; i4 <= fci->numDivisions; i4++) {
                                stats[4]->weight = i4 * fci->increment1;
                                CheckWeights(4)
                                if(numActiveModels == 4)
                                    OptimizeWeights()
                                else {
                                    for(i5 = 0; i5 <= fci->numDivisions; i5++) {
                                        stats[5]->weight = i5 * fci->increment1;
                                        CheckWeights(5)
                                        if(numActiveModels == 5)
                                            OptimizeWeights()
                                        else {
                                            for(i6 = 0; i6 <= fci->numDivisions; i6++) {
                                                stats[6]->weight = i6 * fci->increment1;
                                                CheckWeights(6)
                                                if(numActiveModels == 6) {
                                                    OptimizeWeights()
                                                            //sumWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, 6);
                                                            /*
                                                                                                                if(fci->weightSum > fci->weightSumHighCutoff) {
                                                                                                                    //fprintf(stderr, "Too high : %d\n", fci->weightSum);
                                                                                                                    stats[6]->weight = 0;  // need to reset weight to 0
                                                                                                                    break;
                                                                                                                }

                                                                                                                if(fci->weightSum >= fci->weightSumLowCutoff) {
                                                                                                                    //fprintf(stderr, "Yes\n");
                                                                                                                    runWeightSet(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);
                                                                                                                }
                                                             */
                                                }
                                                else {
                                                    for(i7 = 0; i7 <= fci->numDivisions; i7++) {
                                                        stats[7]->weight = i7 * fci->increment1;
                                                        CheckWeights(7)
                                                        if(numActiveModels == 7)
                                                            OptimizeWeights()
                                                        else {
                                                            for(i8 = 0; i8 <= fci->numDivisions; i8++) {
                                                                stats[8]->weight = i8 * fci->increment1;
                                                                CheckWeights(8)
                                                                if(numActiveModels == 8)
                                                                    OptimizeWeights()
                                                                else {
                                                                    for(i9 = 0; i9 <= fci->numDivisions; i9++) {
                                                                        stats[9]->weight = i9 * fci->increment1;
                                                                        CheckWeights(9)
                                                                        if(numActiveModels == 9)
                                                                            OptimizeWeights()
                                                                        else {
                                                                            for(i10 = 0; i10 <= fci->numDivisions; i10++) {
                                                                                stats[10]->weight = i10 * fci->increment1;
                                                                                CheckWeights(10)
                                                                                if(numActiveModels == 10)
                                                                                    OptimizeWeights()
                                                                                else {
                                                                                    for(i11 = 0; i11 <= fci->numDivisions; i11++) {
                                                                                        stats[11]->weight = i11 * fci->increment1;
                                                                                        CheckWeights(11)
                                                                                        if(numActiveModels == 11)
                                                                                            OptimizeWeights()
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
                                                            }
                                                        }
                                                    } // that's 
                                                } // a
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
    }


    fci->inPhase1 = False;
    //   fprintf(stderr, "Number of RMSE/cost calls = %d\n", numRuns);

#ifdef NESTED_OPT_DEBUG
    char *metric = fci->errorMetric == RMSE ? "RMSE" : "Cost";
    double minMetric = fci->errorMetric == RMSE ? modelRun->optimizedPctMetricPhase1 * 100 : modelRun->optimizedPctMetricPhase1;
    fprintf(stderr, "\n=== Elapsed time for phase 1: %s [%s calls = %ld] [%s = %.2f%%]\n", getElapsedTime(Start_t), metric, modelRun->phase1MetricCalls, metric, minMetric);
#endif    
    dumpWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, 1);

    /*
        if(fci->errorMetric == Cost) {
            loadOptimizedModelWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);
            runWeightSet(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, 1);
        }
     */

    // copy over optimized weights from phase 1 to phase 2 in case phase 2 doesn't improve on phase 1
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
            modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase2 = modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase1;
        }
    }
    modelRun->optimizedPctMetricPhase2 = modelRun->optimizedPctMetricPhase1; // start with phase 1 RMSE; if we can't improve upon that stick with it
    modelRun->optimizedMetricPhase2 = modelRun->optimizedMetricPhase1;

    // bail out if we're only doing the first phase
    if(fci->skipPhase2 || numActiveModels == 1) { // if we're running only one model, don't need to refine
        return True;
    }


    // save weights in case phase 2 doesn't improve upon phase 1
    Start_t = time(NULL);

    for(i1 = 0; i1 <= fci->numDivisions; i1++) {
        if(stats[1]->optimizedWeightPhase1 > 0) stats[1]->weight = stats[1]->optimizedWeightPhase1 + fci->numDivisions2 + (i1 * fci->increment2);
        else {
            stats[1]->weight = 0;
            i1 = fci->numDivisions + 1;
        } // short circuit this since the weight starts at zero
        //fprintf(stderr, "[%s] Model 0, i1 = %d origWeight = %.1f weight = %.3f\n", getElapsedTime(Start_t), i1, modelRun->hourlyModelStats[0].optimizedWeightPhase1, modelRun->hourlyModelStats[0].weight);
        for(i2 = 0; i2 <= fci->numDivisions; i2++) {
            if(stats[2]->optimizedWeightPhase1 > 0) stats[2]->weight = stats[2]->optimizedWeightPhase1 + fci->numDivisions2 + (i2 * fci->increment2);
            else {
                stats[2]->weight = 0;
                i2 = fci->numDivisions + 1;
            }
            CheckWeights(2) // have we gone over the weight total limit?
                    //fprintf(stderr, "[%s] Model 1, i2 = %d origWeight = %.1f weight = %.3f\n", getElapsedTime(Start_t), i2, modelRun->hourlyModelStats[1].optimizedWeightPhase1, modelRun->hourlyModelStats[1].weight);
            if(numActiveModels == 2) {
                OptimizeWeights()
            }
            else {
                for(i3 = 0; i3 <= fci->numDivisions; i3++) {
                    if(stats[3]->optimizedWeightPhase1 > 0) stats[3]->weight = stats[3]->optimizedWeightPhase1 + fci->numDivisions2 + (i3 * fci->increment2);
                    else {
                        stats[3]->weight = 0;
                        i3 = fci->numDivisions + 1;
                    }
                    CheckWeights(3)
                    if(numActiveModels == 3) {
                        OptimizeWeights()
                    }
                    else {
                        for(i4 = 0; i4 <= fci->numDivisions; i4++) {
                            if(stats[4]->optimizedWeightPhase1 > 0) stats[4]->weight = stats[4]->optimizedWeightPhase1 + fci->numDivisions2 + (i4 * fci->increment2);
                            else {
                                stats[4]->weight = 0;
                                i4 = fci->numDivisions + 1;
                            }
                            CheckWeights(4)
                            if(numActiveModels == 4) {
                                OptimizeWeights()
                            }
                            else {
                                for(i5 = 0; i5 <= fci->numDivisions; i5++) {
                                    if(stats[5]->optimizedWeightPhase1 > 0) stats[5]->weight = stats[5]->optimizedWeightPhase1 + fci->numDivisions2 + (i5 * fci->increment2);
                                    else {
                                        stats[5]->weight = 0;
                                        i5 = fci->numDivisions + 1;
                                    }
                                    CheckWeights(5)
                                    if(numActiveModels == 5) {
                                        OptimizeWeights()
                                    }
                                    else {
                                        for(i6 = 0; i6 <= fci->numDivisions; i6++) {
                                            if(stats[6]->optimizedWeightPhase1 > 0) stats[6]->weight = stats[6]->optimizedWeightPhase1 + fci->numDivisions2 + (i6 * fci->increment2);
                                            else {
                                                stats[6]->weight = 0;
                                                i6 = fci->numDivisions + 1;
                                            }
                                            CheckWeights(6)
                                            if(numActiveModels == 6) {
                                                OptimizeWeights()
                                            }
                                            else {
                                                for(i7 = 0; i7 <= fci->numDivisions; i7++) {
                                                    if(stats[7]->optimizedWeightPhase1 > 0) stats[7]->weight = stats[7]->optimizedWeightPhase1 + fci->numDivisions2 + (i7 * fci->increment2);
                                                    else {
                                                        stats[7]->weight = 0;
                                                        i7 = fci->numDivisions + 1;
                                                    }
                                                    CheckWeights(7)
                                                    if(numActiveModels == 7) {
                                                        OptimizeWeights()
                                                    }
                                                    else {
                                                        for(i8 = 0; i8 <= fci->numDivisions; i8++) {
                                                            if(stats[8]->optimizedWeightPhase1 > 0) stats[8]->weight = stats[8]->optimizedWeightPhase1 + fci->numDivisions2 + (i8 * fci->increment2);
                                                            else {
                                                                stats[8]->weight = 0;
                                                                i8 = fci->numDivisions + 1;
                                                            }
                                                            CheckWeights(8)
                                                            if(numActiveModels == 8) {
                                                                OptimizeWeights()
                                                            }
                                                            else {
                                                                for(i9 = 0; i9 <= fci->numDivisions; i9++) {
                                                                    if(stats[9]->optimizedWeightPhase1 > 0) stats[9]->weight = stats[9]->optimizedWeightPhase1 + fci->numDivisions2 + (i9 * fci->increment2);
                                                                    else {
                                                                        stats[9]->weight = 0;
                                                                        i9 = fci->numDivisions + 1;
                                                                    }
                                                                    CheckWeights(9)
                                                                    if(numActiveModels == 9) {
                                                                        OptimizeWeights()
                                                                    }
                                                                    else {
                                                                        for(i10 = 0; i10 <= fci->numDivisions; i10++) {
                                                                            if(stats[10]->optimizedWeightPhase1 > 0) stats[10]->weight = stats[10]->optimizedWeightPhase1 + fci->numDivisions2 + (i10 * fci->increment2);
                                                                            else {
                                                                                stats[10]->weight = 0;
                                                                                i10 = fci->numDivisions + 1;
                                                                            }
                                                                            CheckWeights(10)
                                                                            if(numActiveModels == 10) {
                                                                                OptimizeWeights()
                                                                            }
                                                                            else {
                                                                                for(i11 = 0; i11 <= fci->numDivisions; i11++) {
                                                                                    if(stats[11]->optimizedWeightPhase1 > 0) stats[11]->weight = stats[11]->optimizedWeightPhase1 + fci->numDivisions2 + (i11 * fci->increment2);
                                                                                    else {
                                                                                        stats[11]->weight = 0;
                                                                                        i11 = fci->numDivisions + 1;
                                                                                    }
                                                                                    CheckWeights(11)
                                                                                    if(numActiveModels == 11) {
                                                                                        OptimizeWeights()
                                                                                    }
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

#ifdef NESTED_OPT_DEBUG
    metric = fci->errorMetric == RMSE ? "RMSE" : "Cost";
    minMetric = fci->errorMetric == RMSE ? modelRun->optimizedPctMetricPhase2 * 100 : modelRun->optimizedPctMetricPhase2;
    fprintf(stderr, "\n=== Elapsed time for phase 2: %s [%s calls = %ld] [%s = %.2f%%]\n", getElapsedTime(Start_t), metric, modelRun->phase2MetricCalls, metric, minMetric);
#endif    
    dumpWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, 2);

    return True;
}

void dumpWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int phase)
{
    int modelIndex;
    modelRunType *modelRun;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    fprintf(stderr, "=== Phase %d weights for %s hours ahead %d", phase, genProxySiteName(fci), fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
    if(hoursAfterSunriseIndex >= 0)
        fprintf(stderr, ", hours after sunrise %d, ktIndex %d", hoursAfterSunriseIndex + 1, ktIndex);
    fprintf(stderr, ":\n");

    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
            fprintf(stderr, "\t%-35s = %-8d\n", getModelName(fci, modelIndex), phase < 2 ? modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase1 : modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase2);
        }
    }
}

void saveModelWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex)
{
    int modelIndex;
    modelRunType *modelRun;
    double minMetric;
    char *metric;
    static FILE *costFile = NULL;
    char dumpCostInfo = 0;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    if(fci->errorMetric == RMSE) {
        metric = "RMSE";
        minMetric = modelRun->weightedModelStatsVsGround.rmsePct;
    }
    else {
        metric = "Cost";
        minMetric = modelRun->weightedModelStatsVsGround.lowestCostParameters.total_cost;

        if(dumpCostInfo) {
            if(costFile == NULL) {
                if((costFile = fopen("/tmp/forecastOpt.costInfo.csv", "w")) == NULL) {
                    fprintf(stderr, "blorf\n");
                    exit(0);
                }
                fprintf(costFile, "#modelWeights,max_rate,oversize,max_battery_size,storage_size,total_recharge,total_recharge_cost,min_peak_to_trough_v4,total_cost,total_curtailment,total_loss,total_energy_v3_over,total_energy_v4\n");
            }
        }
    }

    if(fci->verbose) {
        if(fci->inPhase1)
            fprintf(stderr, "%ld/%ld sum/%s calls @ %s : [", modelRun->phase1SumWeightsCalls, modelRun->phase1MetricCalls, metric, getElapsedTime(Start_t));
        else
            fprintf(stderr, "%ld sumWeight and %ld %s calls  @ %s new low %s (phase 2) = %.3f%% wts=", modelRun->phase2SumWeightsCalls, modelRun->phase2MetricCalls, metric, getElapsedTime(Start_t), metric, minMetric);
    }

    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
            if(fci->inPhase1) {
                modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase1 = modelRun->hourlyModelStats[modelIndex].weight;
                if(fci->verbose)
                    fprintf(stderr, "%s-%d ", getModelName(fci, modelIndex), modelRun->hourlyModelStats[modelIndex].weight);
                if(fci->errorMetric == Cost) {
                    fprintf(costFile, "%s-%d ", getModelName(fci, modelIndex), modelRun->hourlyModelStats[modelIndex].weight);
                }
            }
            else {
                modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase2 = modelRun->hourlyModelStats[modelIndex].weight;
                if(fci->verbose)
                    fprintf(stderr, "%s-%d ", getModelName(fci, modelIndex), modelRun->hourlyModelStats[modelIndex].weight);
            }
        }
    }
    if(fci->verbose) {
        if(fci->errorMetric == RMSE)
            fprintf(stderr, "] %s=%.1f%% sum=%d\n", metric, minMetric * 100, fci->weightSum);
        else {
            cost_type c = modelRun->weightedModelStatsVsGround.lowestCostParameters;
            fprintf(stderr, "] %s=$%.0f sum=%d\n", metric, minMetric, fci->weightSum);
            // write cost summaries to costFile
            fprintf(costFile, " sum=%d,", fci->weightSum);
            fprintf(costFile, "%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n", c.max_rate, c.oversize, c.max_battery_size, c.storage_size, c.total_recharge, c.total_recharge_cost, c.min_peak_to_trough_v4, c.total_cost, c.total_curtailment, c.total_loss, c.total_energy_v3_over, c.total_energy_v4);
            fflush(costFile);
        }
    }

    if(fci->inPhase1) {
        modelRun->optimizedPctMetricPhase1 = minMetric;
        modelRun->optimizedMetricPhase1 = minMetric;
    }
    else {
        modelRun->optimizedPctMetricPhase2 = minMetric;
        modelRun->optimizedMetricPhase2 = minMetric;

    }
}

void loadOptimizedModelWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex)
{
    int modelIndex;
    modelRunType *modelRun;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    if(fci->verbose) {
        fprintf(stderr, "Loading optimized model weights for cost T/S generation\n");
    }

    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
            if(fci->inPhase1) {
                modelRun->hourlyModelStats[modelIndex].weight = modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase1;
                if(fci->verbose)
                    fprintf(stderr, "\t%s %d\n", getModelName(fci, modelIndex), modelRun->hourlyModelStats[modelIndex].weight);
            }
            else {
                modelRun->hourlyModelStats[modelIndex].weight = modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase2;
                if(fci->verbose)
                    fprintf(stderr, "\t%s %d\n", getModelName(fci, modelIndex), modelRun->hourlyModelStats[modelIndex].weight);
            }
        }
    }
    if(fci->verbose)
        fprintf(stderr, "\n");

}

//#define DUMP_ALL_WEIGHTS

int sumWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int numModels)
{
    int modelIndex, activeIndex;
    modelRunType *modelRun;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
    fci->weightSum = 0;
#ifdef DUMP_ALL_WEIGHTS
    char wtsStr[1024];
    sprintf(wtsStr, "[%d] ", numModels);
#endif

    for(modelIndex = 0, activeIndex = 0; activeIndex < numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
#ifdef DUMP_ALL_WEIGHTS
            sprintf(wtsStr, "%s %s-%d ", wtsStr, getModelName(fci, modelIndex), modelRun->hourlyModelStats[modelIndex].weight);
#endif
            fci->weightSum += (modelRun->hourlyModelStats[modelIndex].weight < 0 ? 0 : modelRun->hourlyModelStats[modelIndex].weight); // ignore negative weights
            if(fci->weightSum > fci->weightSumHighCutoff)// no need to keep adding to a number that's breached the max weight sum allowed
                break; //return weightSum; 
            activeIndex++;
        }
#ifdef DUMP_ALL_WEIGHTS
        else {
            sprintf(wtsStr, "%s %s-OFF ", wtsStr, getModelName(fci, modelIndex));
        }
#endif        
    }
#ifdef DUMP_ALL_WEIGHTS
    if(fci->weightSum <= fci->weightSumHighCutoff && fci->weightSum >= fci->weightSumLowCutoff) {
        fprintf(stderr, "%s [sum=%d] [running %s]\n", wtsStr, fci->weightSum, fci->errorMetric == RMSE ? "RMSE" : "Cost");
    }
    else if(0) {
        fprintf(stderr, "%s [sum=%d]\n", wtsStr, fci->weightSum);
    }
#endif    

    if(fci->inPhase1) {
        modelRun->phase1SumWeightsCalls++;
    }
    else {
        modelRun->phase2SumWeightsCalls++;
    }

    return fci->weightSum;
}

int weightsInRange(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex)
{
    //int weightSum = sumWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);

    if(fci->weightSum < fci->weightSumHighCutoff && fci->weightSum > fci->weightSumLowCutoff)
        return True;
    else
        return False;
}

int weightsRangeExceeded(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex)
{
    //int weightSum = sumWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);

    return (fci->weightSum > fci->weightSumHighCutoff);
}

int weightsInRangeOld(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex)
{
    int modelIndex;
    static int weightSum;
    modelRunType *modelRun;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
    weightSum = 0;
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
#ifdef DUMP_ALL_WEIGHTS
            if(!fci->inPhase1) fprintf(stderr, "%d-%d ", modelIndex, modelRun->hourlyModelStats[modelIndex].weight);
#endif
            weightSum += (modelRun->hourlyModelStats[modelIndex].weight < 0 ? 0 : modelRun->hourlyModelStats[modelIndex].weight); // ignore negative weights
            if(weightSum > fci->weightSumHighCutoff)// no need to keep adding to a number that's breached the max weight sum allowed
                return False; //return weightSum; 
        }
    }
#ifdef DUMP_ALL_WEIGHTS
    if(!fci->inPhase1) fprintf(stderr, " : sum = %d\n", weightSum);
#endif    
    return (weightSum > fci->weightSumLowCutoff);
}