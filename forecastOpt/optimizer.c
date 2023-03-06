#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <omp.h>
#include "forecastOpt.h"
#include "forecastOptUtils.h"

void saveModelWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int runIndex);
void loadOptimizedModelWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex);
int sumWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int numModels);
void dumpWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunrise, int ktIndex, int phase);
int runErrorWithWeights(forecastInputType *fci, modelRunType *modelRun, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int runIndex);
int runCostWithWeights(forecastInputType *fci, modelRunType *modelRun, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int runIndex);
int weightsInRange(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex);
int weightsRangeExceeded(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex);
int runWeightSet(forecastInputType *fci, modelRunType *modelRun, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int runIndex);
void cleanUpTimeSeries(forecastInputType *fci, int hourAheadIndex);
int cmpcost(const void *p1, const void *p2);
void dumpCostData(forecastInputType *fci, modelRunType *modelRun);
void generateModelWeightPermutationsPhase1(forecastInputType *fci);
void generateModelWeightPermutationsPhase2(forecastInputType *fci);
void genPermsPhase1(forecastInputType *fci, int modelInd, int div, weightType *weights);
void genPermsPhase2(forecastInputType *fci, int modelInd, int div, weightType *weights);
void dumpWeightSets(forecastInputType *fci);
char *printWeightSet(forecastInputType *fci, weightType *weights);


time_t Start_t;
double MinRmse;

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

    if(fci->errorMetric == Cost) {
        retVal = runCostWithWeights(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, runIndex);
    }

    else {
        retVal = runErrorWithWeights(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, runIndex);
    }

    return retVal;
}

int runCostWithWeights(forecastInputType *fci, modelRunType *modelRun, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int runIndex)
{
    if(fci->inPhase1) {
        modelRun->phase1MetricCalls++;
    }
    else {
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
}

int runErrorWithWeights(forecastInputType *fci, modelRunType *modelRun, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int runIndex)
{
    double minMetric;

    if(fci->inPhase1) {
        minMetric = modelRun->optimizedMetricPhase1;
        modelRun->phase1SumWeightsCalls++;
        modelRun->phase1MetricCalls++;

    }
    else {
        minMetric = modelRun->optimizedMetricPhase2;
        modelRun->phase2SumWeightsCalls++;
        modelRun->phase2MetricCalls++;
    }

    computeHourlyErrorStatsWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, USE_GROUND_REF, runIndex);

    double errorStat = getErrorStat(fci, &modelRun->weightedModelStatsVsGround); // RMSEpct, MAEpct, MBEpct

    if(fabs(errorStat) < fabs(minMetric)) {
        if(fci->verbose)
            fprintf(stderr, " this %s = %.2f   current min %s = %.2f\n", fci->errorMetricName, errorStat * 100, fci->errorMetricName, minMetric * 100);

        if(fci->inPhase1) {
            modelRun->optimizedMetricPhase1 = errorStat;
        }
        else {
            //fprintf(stderr, "[HA%d/HAS%d] RMSE=%.0f, %%RMSE=%.4f\n", hoursAheadIndex+1, hoursAfterSunriseIndex+1, modelRun->weightedModelStatsVsGround.rmse, modelRun->weightedModelStatsVsGround.rmsePct);
            modelRun->optimizedMetricPhase2 = errorStat;
        }
        modelRun->mbePctOpt = modelRun->weightedModelStatsVsGround.mbePct;  // save all these stats for later
        modelRun->maePctOpt = modelRun->weightedModelStatsVsGround.maePct;
        modelRun->rmsePctOpt = modelRun->weightedModelStatsVsGround.rmsePct;
        
        saveModelWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, runIndex);
    }
    //}
    return True;
}

// Macro that's a bit lengthy but which makes the nested loop more readable
//#define runRMSE() weightSum = sumWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex); modelRun->phase1SumWeightsCalls++; if(weightSum >= fci->weightSumLowCutoff && weightSum <= fci->weightSumHighCutoff) { modelRun->phase1MetricCalls++; computeHourlyErrorStatsWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex); if(modelRun->weightedModelStatsVsGround.rmsePct < MinRmse) { MinRmse = modelRun->weightedModelStatsVsGround.rmsePct; saveModelWeights(fci,hoursAheadIndex,hoursAfterSunriseIndex); } }
//#define runRMSENoSumCheck() modelRun->phase1MetricCalls++; computeHourlyErrorStatsWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex); if(modelRun->weightedModelStatsVsGround.rmsePct < MinRmse) { MinRmse = modelRun->weightedModelStatsVsGround.rmsePct; saveModelWeights(fci,hoursAheadIndex,hoursAfterSunriseIndex); }
//#define runRMSE_2() weightSum = sumWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex); modelRun->phase2SumWeightsCalls++; if(weightSum >= 97 && weightSum <= 103) { modelRun->phase2MetricCalls++; computeHourlyErrorStatsWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex); if(modelRun->weightedModelStatsVsGround.rmsePct < MinRmse) { MinRmse = modelRun->weightedModelStatsVsGround.rmsePct; saveModelWeights(fci,hoursAheadIndex,hoursAfterSunriseIndex); } }

//#define CheckWeights(n) { if(((numActiveModels - n) > 3) && weightsInRange(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, n)) break; }

// the CheckWeights() macro sums up weights and breaks out of the current loop if the sum is exceeded
#define CheckWeights(n) { sumWeights(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, n); if(fci->weightSum > fci->weightSumHighCutoff) { stats[n]->weight = 0; break;} }

//#define OptimizeWeights() { sumWeights(fci, n); if(fci->weightSum > fci->weightSumHighCutoff) break; if(fci->weightSum >= fci->weightSumLowCutoff) { /*runWeightSet(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);*/ } }
#define OptimizeWeights() { if(fci->weightSum >= fci->weightSumLowCutoff) { runWeightSet(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, 0);} }
//#define OptimizeWeights() { if(fci->weightSum >= fci->weightSumLowCutoff) { numRuns++;} }


#define NESTED_OPT_DEBUG

// In order to optimize nested loops, the 

int runOptimizerParallelCost(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex)
{
    int i;
    //    int i, i1, i2, i3, i4, i5, i6;
    modelRunType *modelRun;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
    modelRun->optimizedMetricPhase1 = 10000;
    modelRun->optimizedMetricPhase2 = 10000;
    modelRun->phase1MetricCalls = 0;
    modelRun->phase2MetricCalls = 0;
    modelRun->phase1SumWeightsCalls = 0;
    modelRun->phase2SumWeightsCalls = 0;

    if(modelRun->numValidSamples == 0)
        return True;

    fci->inPhase1 = True;

    fprintf(stderr, "Max num threads = %d\n", omp_get_max_threads());
    fprintf(stderr, "Num processors  = %d\n", omp_get_num_procs());

    Start_t = time(NULL);

    // In this case we want to use a v4 that's been computed externally with T/S data and a set of weights
    // The v4 will appear in the ground column
    if(fci->useV4fromFile) {
        fci->lowCostList = (costType *) malloc(sizeof (costType));
        fci->numGoodWeightSets = 1;
        runWeightSet(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, 0);
        dumpCostData(fci, modelRun);
        return (True);
    }

    //#define JUST_ONE_MODEL
#ifdef JUST_ONE_MODEL
    fci->lowCostList[numGoodWeightSets] = (costType *) malloc(sizeof (costType));
    fci->lowCostList[numGoodWeightSets]->weights[0] = 0 * inc;
    fci->lowCostList[numGoodWeightSets]->weights[1] = 0 * inc;
    fci->lowCostList[numGoodWeightSets]->weights[2] = 2 * inc; // 2 * 10 = 20% NDFD
    fci->lowCostList[numGoodWeightSets]->weights[3] = 8 * inc; // 8 * 10 = 80% CMM
    fci->lowCostList[numGoodWeightSets]->weights[4] = 0 * inc;
    fci->lowCostList[numGoodWeightSets]->weights[5] = 0 * inc;
    numGoodWeightSets++;
#else

    generateModelWeightPermutationsPhase1(fci); // fci->lowCostList gets allocated here
    dumpWeightSets(fci);

#endif
    fprintf(stderr, "Got %d good weight sets : %d allocated\n", fci->numGoodWeightSets, fci->numAllocatedWeightSets);

    cleanUpTimeSeries(fci, hoursAheadIndex);
    fci->lowCostList = (costType *) malloc(fci->numGoodWeightSets * sizeof (costType));

    ///////// Main Loop ////////////////

#pragma omp parallel for schedule(static)
    for(i = 0; i < fci->numGoodWeightSets; i++) {
        runWeightSet(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, i);
    }

    ////////////////////////////////////

    fprintf(stderr, "\n=== Total elapsed time in phase 1 : %ld seconds\n", time(NULL) - Start_t);

    // sort results -- lowest cost will be first
    qsort(fci->lowCostList, fci->numGoodWeightSets, sizeof (costType), cmpcost);
    fci->bestWeightsIndex1 = fci->lowCostList[0].weightIndexPhase1;
    modelRun->optimizedMetricPhase1 = fci->lowCostList[0].total_cost;
    memcpy(&modelRun->optimizedWeightsPhase1, &fci->weightSetPhase1[fci->bestWeightsIndex1], sizeof (weightType));
    dumpCostData(fci, modelRun);
    fci->inPhase1 = False;

    /////////////////////////////////////  phase 2

    generateModelWeightPermutationsPhase2(fci);
    fci->lowCostList = (costType *) realloc(fci->lowCostList, fci->numGoodWeightSets * sizeof (costType));

#ifdef DUMP_WEIGHTS
    for(i = 0; i < fci->numGoodWeightSets; i++) {
        int sum = 0;
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
                fprintf(stderr, "%s=%2d ", getModelName(fci, modelIndex), fci->lowCostList[i]->weights->modelWeights[modelIndex]);
                sum += fci->lowCostList[i]->weights->modelWeights[modelIndex];
            }
        }
        fprintf(stderr, "sum=%d\n", sum);
    }
#endif

    fprintf(stderr, "numGoodWeightSets = %d\n", fci->numGoodWeightSets);

#pragma omp parallel for schedule(static)
    for(i = 0; i < fci->numGoodWeightSets; i++) {
        runWeightSet(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, i);
    }

    // sort results -- lowest cost will be first
    qsort(fci->lowCostList, fci->numGoodWeightSets, sizeof (costType), cmpcost);
    fci->bestWeightsIndex2 = fci->lowCostList[0].weightIndexPhase2;
    modelRun->optimizedMetricPhase2 = fci->lowCostList[0].total_cost;
    memcpy(&modelRun->optimizedWeightsPhase2, &fci->weightSetPhase2[fci->bestWeightsIndex2], sizeof (weightType));
    dumpCostData(fci, modelRun);

    return True;
}

int runOptimizerParallel(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex)
{
    int i, modelIndex, numActiveModels = 0;
    //    int i, i1, i2, i3, i4, i5, i6;
    //modelStatsType * stats[MAX_MODELS + 1]; //*stats[1], *stats[2], ...
    modelRunType *modelRun;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
    modelRun->optimizedMetricPhase1 = 10000;
    modelRun->optimizedMetricPhase2 = 10000;
    modelRun->phase1MetricCalls = 0;
    modelRun->phase2MetricCalls = 0;
    modelRun->phase1SumWeightsCalls = 0;
    modelRun->phase2SumWeightsCalls = 0;

    if(modelRun->numValidSamples == 0)
        return True;

    fci->inPhase1 = True;

    //    fprintf(stderr, "Max num threads = %d\n", omp_get_max_threads());
    //    fprintf(stderr, "Num processors  = %d\n", omp_get_num_procs());

    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        //modelData->hourlyModelStats[modelIndex].isActive = (getMaxHoursAhead(fci, modelIndex) >= hoursAhead);
        if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
            // activeModel[numActiveModels] = modelIndex;   
            //stats[numActiveModels + 1] = &modelRun->hourlyModelStats[modelIndex];
            numActiveModels++;
        }
    }

    if(numActiveModels < 1) {
        fprintf(stderr, "\n!!! Warning: no models active for current hour; no data to work with\n");
        return False;
    }
    if(numActiveModels > MAX_MODELS) {
        fprintf(stderr, "\n!!! Warning: number of active models [%d] > current max of %d\n", numActiveModels, MAX_MODELS);
        return False;
    }
    modelRun->optimizedMetricPhase1 = 10000;
    modelRun->phase1MetricCalls = 0;
    modelRun->phase2MetricCalls = 0;
    modelRun->phase1SumWeightsCalls = 0;
    modelRun->phase2SumWeightsCalls = 0;

    if(modelRun->numValidSamples == 0)
        return True;

    Start_t = time(NULL);

    generateModelWeightPermutationsPhase1(fci);
    //dumpWeightSets(fci);
    fprintf(stderr, "\nGot %d good phase 1 weight sets\n", fci->numGoodWeightSets);

    ///////// Main Loop ////////////////

    for(i = 0; i < fci->numGoodWeightSets; i++) {
        runWeightSet(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, i);
    }

    fprintf(stderr, "\n=== Total elapsed time in phase 1 : %ld seconds\n", time(NULL) - Start_t);
    fprintf(stderr, "\nBest weight set for phase 1:\n");
    fprintf(stderr, "\trunIndex = %d\n", fci->bestWeightsIndex1);
    fprintf(stderr, "\tweights = %s\n", printWeightSet(fci, &modelRun->optimizedWeightsPhase1));
    fprintf(stderr, "\t%s = %.1f\n", fci->errorMetricName, modelRun->optimizedMetricPhase1 * 100);
    fci->inPhase1 = False;

    ////////////////////////////////////

    /////////////////////////////////////  phase 2

    // using the best NWP weight set from phase 1, generate list of weight sets within a range (say, +-5)
    // for all non-zero weights
    generateModelWeightPermutationsPhase2(fci);

#ifdef DUMP_WIGHTS
    for(i = 0; i < fci->numGoodWeightSets; i++) {
        int sum = 0;
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
                fprintf(stderr, "%s=%2d ", getModelName(fci, modelIndex), fci->weightSetPhase2[i].modelWeights[modelIndex]);
                sum += fci->weightSetPhase2[i].modelWeights[modelIndex];
            }
        }
        fprintf(stderr, "sum=%d\n", sum);
    }
}
#endif

fprintf(stderr, "\nGot %d good phase 2 weight sets\n", fci->numGoodWeightSets);

Start_t = time(NULL);

#pragma omp parallel for schedule(static)
for(i = 0; i < fci->numGoodWeightSets; i++) {
    runWeightSet(fci, modelRun, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, i);
}

fprintf(stderr, "\n=== Total elapsed time in phase 2 : %ld seconds\n", time(NULL) - Start_t);
fprintf(stderr, "\nBest weight set for phase 2:\n");
fprintf(stderr, "\trunIndex = %d\n", fci->bestWeightsIndex2);
fprintf(stderr, "\tweights = %s\n", printWeightSet(fci, &fci->weightSetPhase2[fci->bestWeightsIndex2]));
fprintf(stderr, "\t%s = %.1f\n", fci->errorMetricName, modelRun->optimizedMetricPhase2 * 100);

return True;
}

char *printWeightSet(forecastInputType *fci, weightType *weights)
{
    int modelIndex, offset = 0;
    static char wtStr[4096];
    memset(wtStr, 0, 4096);

    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        sprintf(wtStr + offset, "%s-%d ", getModelName(fci, modelIndex), weights->modelWeights[modelIndex]);
        offset = strlen(wtStr);
    }
    return (wtStr);
}

void dumpWeightSets(forecastInputType *fci)
{
    int s, modelIndex;

    for(s = 0; s < fci->numGoodWeightSets; s++) {
        fprintf(stderr, "[%d]", s);
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {

            fprintf(stderr, " %d", fci->inPhase1 ? fci->weightSetPhase1[s].modelWeights[modelIndex] : fci->weightSetPhase2[s].modelWeights[modelIndex]);
        }
        fprintf(stderr, "\n");
    }
}

void generateModelWeightPermutationsPhase1(forecastInputType *fci)
{
    // allocate list for weights

    weightType weights;

    fci->numAllocatedWeightSets = 2000;
    if(fci->weightSetPhase1 != NULL)
        free(fci->weightSetPhase1);
    fci->weightSetPhase1 = (weightType *) malloc(fci->numAllocatedWeightSets * sizeof (weightType));
    memset(fci->weightSetPhase1, 0, fci->numAllocatedWeightSets * sizeof (weightType));
    memset(&weights, 0, sizeof (weightType)); // start with all zero weights
    fci->numGoodWeightSets = 0;
    genPermsPhase1(fci, -1, 0, &weights);
}

void generateModelWeightPermutationsPhase2(forecastInputType *fci)
{
    // 8 -> 4 -> [-4, -3, -2, -1, 0 , 1, 2, 3, 4]
    weightType phase1Weights;
    int modelIndex;

    fci->increment2 = 1;
    fci->numGoodWeightSets = 0;
    //fci->numAllocatedWeightSets = 2000;
    // copy over the best weight set -- this should be in the first slot of the sorted 
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++)
        phase1Weights.modelWeights[modelIndex] = fci->weightSetPhase1[fci->bestWeightsIndex1].modelWeights[modelIndex];
    if(fci->weightSetPhase2 != NULL)
        free(fci->weightSetPhase2);
    fci->weightSetPhase2 = (weightType *) malloc(fci->numAllocatedWeightSets * sizeof (weightType));

    // for the 2nd (refinement) phase we ignore zero weighted models and tweak the others
    // but we need to prime the pump, so to speak
    genPermsPhase2(fci, -1, 0, &phase1Weights);

    if(fci->numGoodWeightSets > 200000) {

        fprintf(stderr, "\nIn generateModelWeightPermutationsPhase2() got %d weight permutations.  That's too many.  Scaling back weights increment\n", fci->numGoodWeightSets);
        fprintf(stderr, "\told increment2 = %d\n", fci->increment2);
        fci->increment2 = 2;
        fprintf(stderr, "\tnew increment2 = %d\n", fci->increment2);
        fci->numGoodWeightSets = 0;
        memcpy(&phase1Weights, &fci->weightSetPhase1[fci->bestWeightsIndex1], sizeof (weightType));
        genPermsPhase2(fci, -1, 0, &phase1Weights); // try again
        fprintf(stderr, "\tnow get %d weight permutations\n", fci->numGoodWeightSets);
    }
}

void genPermsPhase1(forecastInputType *fci, int modelIndex, int div, weightType *weights)
{
    int i;

    //#define DEBUG_PERM
#ifdef DEBUG_PERM
    fprintf(stderr, "modelIndex=%d div=%d wts=[", modelIndex, div);
    for(i = 0; i <= modelIndex; i++) {
        fprintf(stderr, "%d ", weights->modelWeights[i]);
    }
    fprintf(stderr, "]\n");
#endif

    if(div > fci->numDivisions)
        return;

    if(modelIndex >= 0)
        weights->modelWeights[modelIndex] = div * fci->increment1;

    int sum = 0;
    for(i = 0; i < fci->numModels; i++) {
        sum += weights->modelWeights[i];
    }

    if(modelIndex == (fci->numModels - 1)) {
        if(sum >= fci->weightSumLowCutoff && sum <= fci->weightSumHighCutoff) {
            for(i = 0; i < fci->numModels; i++) {
                fci->weightSetPhase1[fci->numGoodWeightSets].modelWeights[i] = weights->modelWeights[i];
#ifdef DEBUG_PERM
                fprintf(stderr, "%d ", weights->modelWeights[i]);
#endif
            }
#ifdef DEBUG_PERM
            fprintf(stderr, "setIndex = %d sum = %d\n", fci->numGoodWeightSets, sum);
#endif
            fci->numGoodWeightSets++;
            if(fci->numGoodWeightSets == fci->numAllocatedWeightSets) {
                fci->numAllocatedWeightSets *= 2;
                if(fci->verbose) fprintf(stderr, "=== Scaling up weightset memory to %d sets ===\n", fci->numAllocatedWeightSets);
                fci->weightSetPhase1 = (weightType *) realloc(fci->weightSetPhase1, fci->numAllocatedWeightSets * sizeof (weightType));
            }
        }
    }

    else {
        int div;
        for(div = 0; div <= (fci->numDivisions + 1); div++) {

            genPermsPhase1(fci, modelIndex + 1, div, weights);
        }
    }
}

void genPermsPhase2(forecastInputType *fci, int modelIndex, int div, weightType *weights)
{
    int i;
    //#define DEBUG_PERM2
#ifdef DEBUG_PERM
    fprintf(stderr, "modelIndex=%d div=%d wts=[", modelIndex, div);
    for(i = 0; i < fci->numModels; i++) {
        fprintf(stderr, "%d ", weights->modelWeights[i]);
    }
    fprintf(stderr, "]\n");
#endif
    // terminate condition -- we've gotten to the last weight addition -- say 100
    if(div > fci->numDivisions2)
        return;

    int weightPhase1 = fci->weightSetPhase1[fci->bestWeightsIndex1].modelWeights[modelIndex];
    // adjust model weights from -increment to +increment for non-zero weights
    if(modelIndex >= 0 && weightPhase1 > 0)
        weights->modelWeights[modelIndex] = weightPhase1 + (div * 1); // forcing increment2 to 1

    // all model weights have been set -- now check the sum to see if this set is a candidate
    if(modelIndex == (fci->numModels - 1)) {
        int sum = 0;
        for(i = 0; i < fci->numModels; i++) {
            sum += weights->modelWeights[i];
        }
        if(sum >= fci->weightSumLowCutoff && sum <= fci->weightSumHighCutoff) {
            for(i = 0; i < fci->numModels; i++) {
                fci->weightSetPhase2[fci->numGoodWeightSets].modelWeights[i] = weights->modelWeights[i];
#ifdef DEBUG_PERM2
                fprintf(stderr, "%d ", weights->modelWeights[i]);
#endif
            }
#ifdef DEBUG_PERM2
            fprintf(stderr, " sum = %d\n", sum);
#endif
            fci->numGoodWeightSets++;
            if(fci->numGoodWeightSets == fci->numAllocatedWeightSets) {
                fci->numAllocatedWeightSets *= 2;
                fprintf(stderr, "=== Scaling up weightset memory to %d sets ===\n", fci->numAllocatedWeightSets);
                fci->weightSetPhase2 = (weightType *) realloc(fci->weightSetPhase2, fci->numAllocatedWeightSets * sizeof (weightType));
            }
        }
    }

    else {
        if(weights->modelWeights[modelIndex + 1] == 0) // next weight is zero -- just run it once to get past it
            genPermsPhase2(fci, modelIndex + 1, fci->numDivisions2, weights);

        else { // otherwise run the next weight through all increment refinements
            int div;
            for(div = -fci->numDivisions2; div <= (fci->numDivisions2 + 1); div = div + fci->increment2) {

                genPermsPhase2(fci, modelIndex + 1, div, weights);
            }
        }
    }
}

void dumpCostData(forecastInputType *fci, modelRunType * modelRun)
{
    int i, modelIndex;
    char outputFile[2024];
    FILE *outputFP;
    int phase = fci->inPhase1 ? 1 : 2;

    // dump cost data to file
    sprintf(outputFile, "%s/lowestCostData.%dcores.HA%d.%s.phase%d.csv", fci->outputDirectory,
            fci->omp_num_threads, modelRun->hoursAhead, fci->parameterStamp, phase);
    if((outputFP = fopen(outputFile, "w")) == NULL) {
        fprintf(stderr, "Couldn't open weights file %s\n", outputFile);
        exit(1);
    }

    fprintf(outputFP, "#modelWeights,total_cost,max_rate,oversize,storage_size,total_recharge,total_recharge_cost,min_peak_to_trough_v4,total_curtailment,life_span,total_loss,total_energy_v3_over,total_energy_v4\n");
    for(i = 0; i < fci->numGoodWeightSets; i++) {
        costType *c = &fci->lowCostList[i];
        int sum = 0;
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
                int wt = fci->inPhase1 ? fci->weightSetPhase1[c->weightIndexPhase1].modelWeights[modelIndex] : fci->weightSetPhase2[c->weightIndexPhase2].modelWeights[modelIndex];
                fprintf(outputFP, "%s=%2d ", getModelName(fci, modelIndex), wt);
                sum += wt;
            }
        }
        fprintf(outputFP, "sum=%d,", sum);

        fprintf(outputFP, "%.1f,%.0f,%.02f,%.1f,%.1f,%.1f,%.1f,%.4f,%.1f,%.1f,%.1f,%.1f\n",
                c->total_cost, c->max_rate, c->oversize, c->storage_size, c->total_recharge, c->total_recharge_cost, c->min_peak_to_trough_v4, c->total_curtailment, c->life_span_adj, c->total_loss, c->total_energy_v3_over, c->total_energy_v4);
    }
    fclose(outputFP);

    // optionally dump lowest cost time-series
    if(fci->saveLowCostTimeSeries) {
        costType *lowestCostData = &fci->lowCostList[0];
        cost_timeseries_type *ts_data = lowestCostData->lowestCostTimeSeries;

        sprintf(outputFile, "%s/lowestCostTS.Rate_%.1f.Ovrsz_%.2f.%dcores.HA%d.%s.phase%d.csv", fci->outputDirectory,
                lowestCostData->max_rate, lowestCostData->oversize, fci->omp_num_threads, modelRun->hoursAhead, fci->parameterStamp, phase);
        if((outputFP = fopen(outputFile, "w")) == NULL) {
            fprintf(stderr, "Couldn't open T/S file %s\n", outputFile);
            exit(1);
        }
        fprintf(outputFP, "#Max_Rate=%.1f,Oversize=%.2f\n", lowestCostData->max_rate, lowestCostData->oversize);
        fprintf(outputFP, "year,mon,day,hour,min,v3,v4,v3over,v3_v4,v3_v4_noOverSz,recharge_v3_v4,c_v3_v4,peak_v4,trough_v4,peak_to_trough_v4,storage_size,total_recharge,total_v3,total_v4,state_of_charge,curtailment\n");
        for(i = 1; i < fci->numTotalSamples; i++) {
            fprintf(outputFP, "%s,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.4f\n",
                    dtToStringCsvCompact(&ts_data[i].dateTime), ts_data[i].v3, ts_data[i].v4, ts_data[i].v3 * lowestCostData->oversize, ts_data[i].v3_v4, ts_data[i].v3_v4_noOverSz, ts_data[i].recharge_v3_v4, ts_data[i].c_v3_v4, ts_data[i].peak_v4, ts_data[i].trough_v4,
                    ts_data[i].peak_to_trough_v4, ts_data[i].storage_size, ts_data[i].total_recharge, ts_data[i].total_energy_v3_over, ts_data[i].total_energy_v4, ts_data[i].state_of_charge, ts_data[i].curtailment_v4);
        } //ts_data[i].v3 = ts_data[i].v3 / Oversize; // scale back
        fclose(outputFP);
    }
}

int cmpcost(const void *p1, const void *p2)
{
    costType *pp1 = (costType *) p1;
    costType *pp2 = (costType *) p2;

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
        else {
            fprintf(stderr, "filtered: sampleIndex = %d, validCode = %s\n", sampleInd, validString(thisSample->forecastData[hoursAheadIndex].groupIsValid));
        }
    }

    fprintf(stderr, "cleanUpTimeSeries(): filtered out %d samples (old=%d, new=%d)\n", fci->numTotalSamples - newNumSamples, fci->numTotalSamples, newNumSamples);

    free(fci->timeSeries);
    fci->timeSeries = newTimeSeries;
    fci->numTotalSamples = newNumSamples;
}

#ifdef BLORFO

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
#endif

#ifdef DEPRECATED

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
#endif

void saveModelWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int runIndex)
{
    int modelIndex;
    modelRunType *modelRun;
    double minMetric;
    char *metric;
    static FILE *costFile = NULL;
    char dumpCostInfo = 0;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    switch(fci->errorMetric) {
        case RMSE:
            metric = "RMSE";
            minMetric = modelRun->weightedModelStatsVsGround.rmsePct;
            break;
        case MBE:
            metric = "MBE";
            minMetric = modelRun->weightedModelStatsVsGround.mbePct;
            break;
        case MAE:
            metric = "MAE";
            minMetric = modelRun->weightedModelStatsVsGround.maePct;
            break;
        case Cost:
            //
            // get rid of all the cost stuff!!
            //
            metric = "Cost";
            minMetric = modelRun->weightedModelStatsVsGround.lowestCostParameters.total_cost;
            if(dumpCostInfo) {
                if(costFile == NULL) {
                    if((costFile = fopen("/tmp/forecastOpt.costInfo.csv", "w")) == NULL) {
                        fprintf(stderr, "blorf\n");
                        exit(0);
                    }
                    fprintf(costFile, "#modelWeights,max_rate,oversize,storage_size,total_recharge,total_recharge_cost,min_peak_to_trough_v4,total_cost,total_curtailment,life_span,total_loss,total_energy_v3_over,total_energy_v4\n");
                }
            }
            break;
    }

    if(fci->verbose) {
        if(fci->inPhase1)
            fprintf(stderr, "phase 1 : %ld/%ld sum/%s calls @ %s : [ %s ] ", modelRun->phase1SumWeightsCalls, modelRun->phase1MetricCalls, metric, getElapsedTime(Start_t), printWeightSet(fci, &fci->weightSetPhase1[runIndex]));
        else
            fprintf(stderr, "phase 2 : %ld/%ld sum/%s calls @ %s : [ %s ] ", modelRun->phase2SumWeightsCalls, modelRun->phase2MetricCalls, metric, getElapsedTime(Start_t), printWeightSet(fci, &fci->weightSetPhase2[runIndex]));
        //fprintf(stderr, "%ld sumWeight and %ld %s calls  @ %s new low %s (phase 2) = %.3f%% wts=", modelRun->phase2SumWeightsCalls, modelRun->phase2MetricCalls, metric, getElapsedTime(Start_t), metric, minMetric);
    }

    for(modelIndex = 100000; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
            if(fci->inPhase1) {
                //                modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase1 = fci->weightSetPhase1[runIndex].modelWeights[modelIndex];
                if(fci->verbose)
                    fprintf(stderr, "%s-%d ", getModelName(fci, modelIndex), fci->weightSetPhase1[runIndex].modelWeights[modelIndex]);
                if(fci->errorMetric == Cost) {
                    fprintf(costFile, "%s-%d ", getModelName(fci, modelIndex), fci->weightSetPhase1[runIndex].modelWeights[modelIndex]);
                }
            }
            else {
                //                modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase2 = fci->weightSetPhase2[runIndex].modelWeights[modelIndex];
                if(fci->verbose)
                    fprintf(stderr, "%s-%d ", getModelName(fci, modelIndex), fci->weightSetPhase2[runIndex].modelWeights[modelIndex]);
            }
        }
    }
    if(fci->verbose) {
        if(fci->errorMetric == Cost) {
            costType c = modelRun->weightedModelStatsVsGround.lowestCostParameters;
            fprintf(stderr, "] %s=$%.0f sum=%d\n", metric, minMetric, fci->weightSum);
            // write cost summaries to costFile
            fprintf(costFile, " sum=%d,", fci->weightSum);
            fprintf(costFile, "%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n", c.max_rate, c.oversize, c.storage_size, c.total_recharge, c.total_recharge_cost, c.min_peak_to_trough_v4, c.total_cost, c.total_curtailment, c.life_span_adj, c.total_loss, c.total_energy_v3_over, c.total_energy_v4);
            fflush(costFile);
        }
    }

    if(fci->inPhase1) {
        //modelRun->optimizedMetricPhase1 = minMetric;
        fci->bestWeightsIndex1 = runIndex;
        memcpy(&modelRun->optimizedWeightsPhase1, &fci->weightSetPhase1[runIndex], sizeof (weightType));
    }
    else {
        //modelRun->optimizedMetricPhase2 = minMetric;
        fci->bestWeightsIndex2 = runIndex;
        memcpy(&modelRun->optimizedWeightsPhase2, &fci->weightSetPhase2[runIndex], sizeof (weightType));
    }
}

#ifdef BLORFO

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
#endif

//#define DUMP_ALL_WEIGHTS

int sumWeights(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int numModels)
{
#ifdef BLORFO 

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
#endif

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

#ifdef BLORFO

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
#endif