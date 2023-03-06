#include <time.h>
#include <unistd.h>
#include <omp.h>
#include "forecastOpt.h"

void dumpCostData(costType *c);

// Fixed Parameters For Cost Calculation
//
// storage eficiency       
double Storage_Efficiency = 0.9;
// PV cost $ / kW [future]
double PV_Cost = 1000; // was 400 for current
// storage cost $ / kWh [future] 
double Storage_Cost = 200; // was 50 for current
// recharge cost $ / kWh     
double Recharge_Cost = 0.15;
double Flexibility = 0.025 * 1000; // 2.5% times 1000 (kW?)
double Remuneration = 0.08;
// life time   
double Life_Span = 20;

// Variable (looped) Parameters For Cost Calculation
//Oversize    (Variable Parameter)
//double Oversize = 1; // loop 1..2 by .1

//Max battery size (Wh) / kW    (Variable Parameter)
//double Max_Battery_Size = 500; // loop 0:5000

//maximum recharge rate W / kWh    (Variable Parameter)
//double Max_Rate = 50; // loop 0..300

/* from Sergey:
I looked through the calculations we did on perfect forecast, and here are the suggested changes in the boundaries 
for the variable look-up coefficients:

for (Max_Rate = 10; Max_Rate <= 500; Max_Rate=Max_Rate+10){
for (oversize = 0.95; oversize <= 1.30; oversize = oversize + 0.01){
for (Max_bat_size = 100; Max_bat_size <= 4000; Max_bat_size = Max_bat_size+10){

The Max_Rate has to stay intact;
The oversize gets close but has never reached the new boundaries;
The Max_bat_size also gets close but has never reached them.

It should speed up the algorithm run at least 2 times faster.
 */

// From Sergey's latest code (1/2/2020):
/*
        for (Max_Rate = 1; Max_Rate <= 400; Max_Rate = Max_Rate + 2){
                for (oversize = 0.9; oversize <= 1.3; oversize = oversize + 0.02){
                        for (Max_bat_size = 0; Max_bat_size <= 4000; Max_bat_size = Max_bat_size + 10){
 */

// increment sizes will have a more profound effect on runtime than start and stop numbers
int Max_Rate_Start = 1; // was 10
int Max_Rate_End = 40; // was 400
int Rate_Increment = 1; // was 10
//double Oversize_Start = 0.98; // according to Sergey's table
int Oversize_Start = 100; // 6/28/2020 -- Rich said to hold Oversize above 1.0
int Oversize_End = 110; // was 130
int Oversize_Increment = 1; // was 0.1
int Max_Battery_Size = 500;
int Battery_Size_Start = 60; // was 0
int Battery_Size_End = 3940; // Serge == 10000, was 5000
int Battery_Size_Increment = 10; // was 100

// This is where the perfect forecast cost is computed -- based on Sergey's code

void computeHourlyCostWeighted(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int useGroundReference, int runIndex)
{
    int sampleInd, modelIndex;
    //double diff;
    double compositeGHI;
    timeSeriesType *thisSample;
    cost_timeseries_type *ts_data;
    costType *lowestCostData, costData;
    weightType *weights;
    FILE *OUT;
    time_t beginTime;
    int threadNum = omp_get_thread_num();
    int Max_Rate;
    int Oversize_Int;
    double Oversize;
    beginTime = time(NULL);
    cost_timeseries_type *lowestCostTimeSeries;

    size_t tsSize = sizeof (cost_timeseries_type) * fci->numTotalSamples;

    // set the NWP weights for this run
    weights = fci->inPhase1 ? &fci->weightSetPhase1[runIndex] : &fci->weightSetPhase2[runIndex];

    //fprintf(stderr, "phase/runIndex = %d/%d\n", fci->inPhase1 ? 1 : 2, runIndex);
    lowestCostData = &fci->lowCostList[runIndex];
    // ts_data is the time-series data that get used over and over again
    if((ts_data = (cost_timeseries_type *) malloc(tsSize)) == NULL)
        FatalError("computeHourlyCostWeighted()", "Memory allocation error");

    // lowestCostTimeSeries is a snapshot copy of ts_data when the total_cost reaches a new low
    if(fci->saveLowCostTimeSeries) {
        if((lowestCostTimeSeries = (cost_timeseries_type *) malloc(tsSize)) == NULL)
            FatalError("computeHourlyCostWeighted()", "Memory allocation error");
    }

    /* shortcut settings */
/*
    Rate_Increment = 20;
    Oversize_Increment = .1;
    Battery_Size_Increment = 100;
*/

    memset(ts_data, 0, tsSize);

#ifdef DEBUG_2
    int count = 0, hoursAhead = modelRun->hoursAhead;
#endif
    if(!useGroundReference) {
        fprintf(stderr, "useGroundReference not set -- not working\n");
        exit(1);
    }

    // first we compute the v4 from NWPs and weights
    for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        memcpy(&ts_data[sampleInd].dateTime, &thisSample->dateTime, sizeof (dateTimeType));

        if(fci->useV4fromFile) {
            ts_data[sampleInd].v3 = thisSample->satGHI > 0 ? thisSample->satGHI : 0; // labeled v3 in input file
            ts_data[sampleInd].v4 = thisSample->groundGHI > 0 ? thisSample->groundGHI : 0; // latbled ground in input file
        }
        else {
#ifdef DEBUG_2
            if(hoursAhead == DEBUGHOUR)
                fprintf(stderr, "DEBUG:group %d\n", count++);
#endif
            // Form the composite GHI from the NWPs and current weight set
            compositeGHI = 0;
            for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
                compositeGHI += (thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] * ((float) weights->modelWeights[modelIndex]) / 100);

#ifdef DEBUG_2
                if(hoursAhead == DEBUGHOUR && weight > 0.01) {
                    fprintf(stderr, "DEBUG:%s,%s=%.1f,weight=%.2f,weightedGHI=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                            getModelName(fci, modelIndex), thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex], weight, thisSample->optimizedGHI1);
                }
#endif
                //}
            }

            ts_data[sampleInd].v3 = thisSample->groundGHI; // assumed that when only v3 is available, ground==v3
            ts_data[sampleInd].v4 = compositeGHI;
        }
    }

    // OK, so all the v3's and v4's are set.  Now on to the lowest cost calculation

#ifdef DEBUG_2
    fprintf(stderr, "weightedRMSE: rmsePct=%.1f sumModel_Ground_2=%.1f, N=%d, totalWeights=%.1f\n", weightedModelStats->rmsePct * 100, weightedModelStats->sumModel_Ground_2, N, 0);
#endif

    int i;
    double lowestCost = 1000000;
    int perm_index = 0;
    /*
        fprintf(stderr, "[%d] Max_Rate=[%d-%d-%d],Oversize=[%.2f-%.02f-%.02f],Battery=[%d-%d-%d],Num_Cost_Perms=%d\n",
                threadNum, Max_Rate_Start, Max_Rate_End, Rate_Increment,
                Oversize_Start, Oversize_End, Oversize_Increment,
                Battery_Size_Start, Battery_Size_End, Battery_Size_Increment,
                Num_Cost_Perms);
     */

    //fprintf(stderr, "[%d] finished allocating data -- entering cost loop : elapse time = %ld\n", threadNum, time(NULL) - beginTime);

    for(Max_Rate = Max_Rate_Start; Max_Rate <= Max_Rate_End; Max_Rate = Max_Rate + Rate_Increment) {
        for(Oversize_Int = Oversize_Start; Oversize_Int <= Oversize_End; Oversize_Int += Oversize_Increment) {
            Oversize = ((double) Oversize_Int)/100;
            if(0) {
                char dumpFile[512];
                sprintf(dumpFile, "/tmp/CostDataDump.Rate=%d.Oversize=%.2f.Bat=%d.csv", Max_Rate, Oversize, Max_Battery_Size);
                if((OUT = fopen(dumpFile, "w")) == NULL) {
                    perror("Couldn't open cost dump file");
                    exit(1);
                }
                fprintf(OUT, "#Max_Rate=%d,Oversize=%.2f,Max_Battery_Size=%d\n", Max_Rate, Oversize, Max_Battery_Size);
                //fprintf(OUT, "#year,month,day,hour,minute,v3,v4,v3oversize,v3_v4,recharge_v3_v4,c_v3_v4,peak_v4,trough_v4,peak_to_trough_v4,storage_size,total_recharge\n");
            }

            memset(&costData, 0, sizeof (costType)); // this should have the effect of setting everything to 0
            //costData.weights = weights; // this copies over the weightset pointer

            // initial values
            ts_data[0].v3_v4 = ts_data[0].v3 - ts_data[0].v4; //
            //double cumulative_v3_v4 = ts_data[0].c_v3_v4 = ts_data[0].v3_v4; // maybe some extra stuff before this (sergey's code)               
            ts_data[0].v3_v4_noOverSz = ts_data[0].v3_v4 = ts_data[0].v3 - ts_data[0].v4;
            ts_data[0].v3_v4 = 0;
            if(ts_data[0].v3 < ts_data[0].v4 && ts_data[0].v4 - ts_data[0].v3 > Flexibility) {
                ts_data[0].v3_v4 = ts_data[0].v3 - ts_data[0].v4 + Flexibility;
            }
            else if(ts_data[0].v3 > ts_data[0].v4 && ts_data[0].v3 - ts_data[0].v4 > Flexibility) {
                ts_data[0].v3_v4 = ts_data[0].v3 - ts_data[0].v4 - Flexibility;
            }

            ts_data[0].c_v3_v4 = ts_data[0].v3_v4;
            ts_data[0].peak_to_trough_v4 = 0;
            ts_data[0].peak_v4 = 0;
            ts_data[0].trough_v4 = 0;
            costData.storage_size = ts_data[0].peak_to_trough_v4;
            costData.total_recharge = 0;
            costData.total_energy_v3_over = 0;
            costData.total_energy_v4 = 0;
            costData.total_curtailment = 0;
            costData.life_span_adj = Life_Span * 8760 / fci->numTotalSamples;

            int prevGoodSampIndex = 0;

            for(i = 1; i < fci->numTotalSamples; i++) {
                double v3_over = ts_data[i].v3 * Oversize; // scale v3 up or down
                // v3_v4 is the difference between reality and prediction
                // if v4 over-predicts this will be negative, if v4 under predicts this will be positive
                // ...it zeros out at night

                // Flexibility clips the difference (v3 - v4) to within +- Flexibility
                if(v3_over < ts_data[i].v4 && (ts_data[i].v4 - v3_over) > Flexibility) {
                    ts_data[i].v3_v4 = v3_over - ts_data[i].v4 + Flexibility;
                }
                else if(v3_over > ts_data[i].v4 && (v3_over - ts_data[i].v4) > Flexibility) {
                    ts_data[i].v3_v4 = v3_over - ts_data[i].v4 - Flexibility;
                }
                else {
                    ts_data[i].v3_v4 = 0;
                }

                // Do the same with the non-oversized version, v3_v4_noOverSz
                if(ts_data[i].v3 < ts_data[i].v4 && ts_data[i].v4 - ts_data[i].v3 > Flexibility) {
                    ts_data[i].v3_v4_noOverSz = ts_data[i].v3 - ts_data[i].v4 + Flexibility;
                }
                else if(ts_data[i].v3 > ts_data[i].v4 && ts_data[i].v3 - ts_data[i].v4 > Flexibility) {
                    ts_data[i].v3_v4_noOverSz = ts_data[i].v3 - ts_data[i].v4 - Flexibility;
                }
                else {
                    ts_data[i].v3_v4_noOverSz = 0;
                }

                // recharge
                ts_data[i].recharge_v3_v4 = 0;
                if(v3_over < 1 && ts_data[prevGoodSampIndex].peak_to_trough_v4 < 0) {
                    ts_data[i].recharge_v3_v4 = (-ts_data[prevGoodSampIndex].peak_to_trough_v4 < Max_Rate) ? -ts_data[prevGoodSampIndex].peak_to_trough_v4 : Max_Rate;
                }

                ts_data[i].c_v3_v4 = ts_data[prevGoodSampIndex].c_v3_v4 + ts_data[i].v3_v4 + ts_data[i].recharge_v3_v4;
                ts_data[i].peak_v4 = MAX(ts_data[prevGoodSampIndex].peak_v4, ts_data[i].c_v3_v4);

                ts_data[i].trough_v4 = (ts_data[i].c_v3_v4 < ts_data[prevGoodSampIndex].peak_v4) ? ts_data[i].c_v3_v4 : ts_data[i].peak_v4;
                ts_data[i].peak_to_trough_v4 = ts_data[i].trough_v4 - ts_data[i].peak_v4;
                costData.storage_size = (costData.storage_size > ts_data[i].peak_to_trough_v4) ? ts_data[i].peak_to_trough_v4 : costData.storage_size;
                costData.total_recharge = costData.total_recharge + ts_data[i].recharge_v3_v4;
                costData.total_energy_v3_over += v3_over;
                costData.total_energy_v4 += ts_data[i].v4;

                // snapshot values
                ts_data[i].total_recharge = costData.total_recharge;
                ts_data[i].total_energy_v3_over = costData.total_energy_v3_over;
                ts_data[i].total_energy_v4 = costData.total_energy_v4;

                prevGoodSampIndex = i; // ignores night and non-OK data
                //}

            }

            if(0 && perm_index % 100000 == 0 && perm_index > 0) {
                fprintf(stderr, "[%d] %d permutations processed : [%d/%.1f/%d] : elapsed time = %ld\n", threadNum, perm_index, Max_Rate, Oversize, Max_Battery_Size, time(NULL) - beginTime);
            }

            costData.diff_en = (costData.total_energy_v4 - costData.total_energy_v3_over) / 1000; //kWh -- not used

            // Now compute state_of_charge and curtailment (check this against Rich's Excel code)
            for(i = 1; i < fci->numTotalSamples; i++) {
                ts_data[i].state_of_charge = 1 - (ts_data[i].peak_to_trough_v4 + (ts_data[i].c_v3_v4 - ts_data[i].trough_v4)) / costData.storage_size;

                if(ts_data[i].state_of_charge >= 1 && ts_data[i].v3_v4_noOverSz > 0) {
                    ts_data[i].curtailment_v4 =
                            (ts_data[i].v3_v4_noOverSz + ts_data[i - 1].peak_to_trough_v4 > 0) ?
                            (ts_data[i].v3_v4_noOverSz + ts_data[i - 1].peak_to_trough_v4) / 1000 : 0;
                }
                else {
                    ts_data[i].curtailment_v4 = 0;
                }

                costData.total_curtailment = costData.total_curtailment + ts_data[i].curtailment_v4;

                if(0) {
                    if(i == 1)
                        fprintf(OUT, "year,mon,day,hour,min,v3,v4,v3over,v3_v4,recharge_v3_v4,c_v3_v4,peak_v4,trough_v4,peak_to_trough_v4,storage_size,total_recharge,total_v3,total_v4,state_of_charge,curtailment\n");
                    fprintf(OUT, "%s,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.1f\n",
                            dtToStringCsvCompact(&ts_data[i].dateTime), ts_data[i].v3, ts_data[i].v4, ts_data[i].v3 * Oversize, ts_data[i].v3_v4, ts_data[i].recharge_v3_v4, ts_data[i].c_v3_v4, ts_data[i].peak_v4, ts_data[i].trough_v4,
                            ts_data[i].peak_to_trough_v4, ts_data[i].storage_size, ts_data[i].total_recharge, ts_data[i].total_energy_v3_over, ts_data[i].total_energy_v4, ts_data[i].state_of_charge, ts_data[i].curtailment_v4);
                } //ts_data[i].v3 = ts_data[i].v3 / Oversize; // scale back
            }


            costData.total_loss = costData.total_curtailment * Remuneration * costData.life_span_adj;

            costData.storage_size = costData.storage_size / Storage_Efficiency;
            costData.total_recharge = costData.total_recharge / 1000;
            costData.total_recharge_cost = costData.total_recharge * Recharge_Cost * costData.life_span_adj;
            costData.total_cost = PV_Cost * (Oversize - 1) - costData.storage_size * Storage_Cost / 1000 + costData.total_recharge_cost + costData.total_loss;

            if(costData.total_cost < lowestCost && costData.total_cost > 0) {
                costData.oversize = Oversize;
                costData.max_battery_size = Max_Battery_Size;
                costData.max_rate = Max_Rate;
                //fprintf(stderr, "=> New lowest cost = %.1f [index=%d]\n", costData.total_cost, perm_index);
                //dumpCostData(costData);
                //lowestCostIndex = perm_index;
                lowestCost = costData.total_cost;
                // save lowest cost parameters
                memcpy(lowestCostData, &costData, sizeof (costType));
                if(fci->saveLowCostTimeSeries) {
                    memcpy(lowestCostTimeSeries, ts_data, tsSize);
                }
                if(fci->inPhase1)
                    lowestCostData->weightIndexPhase1 = runIndex;
                else
                    lowestCostData->weightIndexPhase2 = runIndex;
            }

            perm_index++;
        }
    }

    if(fci->saveLowCostTimeSeries) {
        lowestCostData->lowestCostTimeSeries = lowestCostTimeSeries;
    }

    fprintf(stderr, "[%d] finished cost loop -- permutations = %d elapsed time = %ld\n", threadNum, perm_index, time(NULL) - beginTime);

#ifdef DEBUG_2
    static char firstTime = 1;
    if(firstTime) {
        fprintf(stderr, "CST:#modelWeights,storage_size,recharge,recharge_cost,max_rate,oversize,max_battery,cost,runtime,index\n");
        firstTime = 0;
    }

    fprintf(stderr, "CST:");
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {

            fprintf(stderr, "%s-%d ", getModelName(fci, modelIndex), modelRun->hourlyModelStats[modelIndex].weight);
        }

    }

    fprintf(stderr, ",%.1f", costData.storage_size);
    fprintf(stderr, ",%.1f", costData.total_recharge);
    fprintf(stderr, ",%.1f", costData.total_recharge_cost);
    fprintf(stderr, ",%.2f", costData.max_rate);
    fprintf(stderr, ",%.1f", costData.oversize);
    fprintf(stderr, ",%.1f", costData.max_battery_size);
    fprintf(stderr, ",%.1f", costData.total_cost);
    fprintf(stderr, ",%s", getElapsedTime(Start_t));
    fprintf(stderr, ",%d\n", lowestCostIndex);
#endif

    free(ts_data);
    //    free(cost_data);

    /*
        if(runIndex == 0)
           fprintf(stderr, "Number of cost algo permutations = %d\n", perm_index);
     */
    fprintf(stderr, "TIMER:%d,%ld\n", threadNum, time(NULL) - beginTime);
}

void getMinMaxParameters(/*minMaxType *minMax*/)
{

    /*
    MIN	
    oversize=	Max_bat_capacity_(Wh/kW)=	Max_recharge_rate_(W/kW)=
    v4cpr           0.96	40	1	
    persistence	0.96	80	1	
    GFS             1.08	170	1	
    NDFD_east	0.96	170	1	
    CMM_east	0.96	60	1	
    ECMWF           0.98	90	3	
    HRRR            1.06	510	1	

    MAX	
    oversize=	Max_bat_capacity_(Wh/kW)=	Max_recharge_rate_(W/kW)=
    v4cpr           1.16	1790	123	
    persistence	1.28	3960	399	
    GFS             1.28	3100	303	
    NDFD_east	1.18	3940	391	
    CMM_east	1.14	1270	251	
    ECMWF           1.28	1780	305	
    HRRR            1.28	2700	399	
     */

}

void dumpTheCostData(costType * c)
{
    static int firstTime = 1;

    if(firstTime) {
        fprintf(stderr, "COST:max_rate,oversize,storage_size,total_recharge,total_recharge_cost,min_peak_to_trough_v4,total_cost,total_curtailment,total_loss,total_energy_v3_over,total_energy_v4\n");
        firstTime = 0;
    }
    fprintf(stderr, "COST:%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n",
            c->max_rate, c->oversize, c->storage_size, c->total_recharge, c->total_recharge_cost, c->min_peak_to_trough_v4, c->total_cost, c->total_curtailment, c->total_loss, c->total_energy_v3_over, c->total_energy_v4);
}
