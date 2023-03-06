/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

void computeHourlyCostWeightedOld(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int useGroundReference, int runIndex)
{
    int sampleInd, modelIndex;
    //double diff;
    double compositeGHI;
    timeSeriesType *thisSample;
    cost_timeseries_type *ts_data;
    costType *lowestCostData, costData;
    FILE *OUT;
    time_t beginTime;
    int threadNum = omp_get_thread_num();
    int Max_Rate, Max_Battery_Size;
    double Oversize;
    beginTime = time(NULL);
    cost_timeseries_type *lowestCostTimeSeries;
    modelRunType *modelRun;
    int *modelWeights = fci->lowCostList[runIndex]->weights;
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    size_t tsSize = sizeof (cost_timeseries_type) * fci->numTotalSamples;

    lowestCostData = fci->lowCostList[runIndex];

    //    if(ts_data == NULL) { // this number shouldn't change over the course of the run but we double it for now just to be safe
    if((ts_data = (cost_timeseries_type *) malloc(tsSize)) == NULL)
        FatalError("computeHourlyCostWeighted()", "Memory allocation error");
    if(fci->saveLowCostTimeSeries) {
        if((lowestCostTimeSeries = (cost_timeseries_type *) malloc(tsSize)) == NULL)
            FatalError("computeHourlyCostWeighted()", "Memory allocation error");
    }

    if((ts_data = (cost_timeseries_type *) malloc(tsSize)) == NULL)
        FatalError("computeHourlyCostWeighted()", "Memory allocation error");
    /*
            if((fci->lowestCostTimeSeries = (cost_timeseries_type *) malloc(tsSize)) == NULL)
                FatalError("computeHourlyCostWeighted()", "Memory allocation error");
     */

    // testing/overrides -- this speeds things up

    /*
            Rate_Increment = 8;
            Oversize_Increment = .05;
            Battery_Size_Increment = 50;
     */

    // testing/overrides -- super fast

    Rate_Increment = 20;
    Oversize_Increment = .1;
    Battery_Size_Increment = 100;


    /*
        Max_Rate_Start = Max_Rate_End = 13;
        Oversize_Start = Oversize_End = 1.0;
        Battery_Size_Start = Battery_Size_End = 260;
     */
    //    }

    //    if(cost_data == NULL) { // this size is frozen
    /*
        if((cost_data = (costType *) malloc(sizeof (costType) * Num_Cost_Perms)) == NULL)
            FatalError("computeHourlyCostWeighted()", "Memory allocation error");
     */

    /*
            fprintf(stderr, "CST:#Max_Rate=[%d-%d-%d],Oversize=[%.2f-%.02f-%.02f],Battery=[%d-%d-%d],Num_Cost_Perms=%d\n",
                    Max_Rate_Start, Max_Rate_End, Rate_Increment,
                    Oversize_Start, Oversize_End, Oversize_Increment,
                    Battery_Size_Start, Battery_Size_End, Battery_Size_Increment,
                    Num_Cost_Perms);
        }
     */

    memset(ts_data, 0, tsSize);
    //memset(cost_data, 0, costSize);

#ifdef DEBUG_2
    int count = 0, hoursAhead = modelRun->hoursAhead;
#endif
    if(!useGroundReference) {
        fprintf(stderr, "useGroundReference not set -- not working\n");
        exit(1);
    }

    /*
        fprintf(stderr, "[%d] Running weight set [", threadNum);
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            fprintf(stderr, "%d%c", modelWeights[modelIndex], modelIndex < (fci->numModels - 1) ? ',' : ']');
        }
        fprintf(stderr, "\n");
     */

    // now run through the T/S ts_data
    for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        memcpy(&ts_data[sampleInd].dateTime, &thisSample->dateTime, sizeof (dateTimeType));

#ifdef DUMP_NWP
        static int printHeader = 1;
        if(printHeader) {
            fprintf(stderr, "DEBUG:group %d\n", count++);
            printHeader = 0;
        }
#endif
        // Form the composite GHI from the NWPs and current weight set
        compositeGHI = 0;
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            compositeGHI += (thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] * ((float) modelWeights[modelIndex]) / 100);
            /*
                            thisModelStats = &modelRun->hourlyModelStats[modelIndex];
                            if(thisModelStats->maskSwitchOn && thisModelStats->weight > 0) {
                                weight = ((double) thisModelStats->weight) / 100.0;
                                compositeGHI += (thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] * weight);
             */
#ifdef DUMP_NWP
            if(hoursAhead == DEBUGHOUR && weight > 0.01) {
                fprintf(stderr, "DEBUG:%s,%s=%.1f,weight=%.2f,weightedGHI=%.1f\n", dtToStringCsv2(&thisSample->dateTime),
                        getModelName(fci, modelIndex), thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex], weight, thisSample->optimizedGHI1);
            }
#endif
            //}
        }

        ts_data[sampleInd].v3 = thisSample->groundGHI;
        ts_data[sampleInd].v4 = compositeGHI;
    }

    // OK, so all the v3's and v4's are set.  Now on to the lowest cost calculation
    /*
        fprintf(stderr, "[%d] totalSamples =%d : numGoodSamples = %d\n", threadNum, fci->numTotalSamples, numGoodSamples);
     */

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
        for(Oversize = Oversize_Start; Oversize <= Oversize_End; Oversize = Oversize + Oversize_Increment) {
            for(Max_Battery_Size = Battery_Size_Start; Max_Battery_Size <= Battery_Size_End; Max_Battery_Size = Max_Battery_Size + Battery_Size_Increment) {
                //int dumpTSdata = (modelRun->phase1MetricCalls == 50 && fabs(Max_Rate - 100) < 0.1 && fabs(Oversize - 1.2) < 0.01 && fabs(Max_Battery_Size - 2000) < 0.1);
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

                memset(&costData, 0, sizeof (costType));
                memcpy(&costData.weights, lowestCostData->weights, sizeof (int) * fci->numModels); // this copies over the weightset

                // presets
                ts_data[0].v3_v4 = ts_data[0].v3 - ts_data[0].v4; //
                double cumulative_v3_v4 = ts_data[0].c_v3_v4 = ts_data[0].v3_v4; // maybe some extra stuff before this (sergey's code)               
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

                int prevGoodSampIndex = 0;

                for(i = 1; i < fci->numTotalSamples; i++) {
                    double v3_over = ts_data[i].v3 * Oversize; // scale v3 up or down

                    // v3_v4 is the difference between reality and prediction
                    // if v4 over-predicts this will be negative, if v4 under predicts this will be positive
                    // ...it zeros out at night

                    ts_data[i].v3_v4 = v3_over - ts_data[i].v4;
                    if(v3_over < ts_data[i].v4) { // over-prediction
                        double v4_v3 = ts_data[i].v4 - v3_over;

                        if(v4_v3 > Flexibility) { // currently 25
                            ts_data[i].v3_v4 += Flexibility; // if we overpredict by more than 25, add 25
                        }
                        else {
                            ts_data[i].v3_v4 = 0; // we say v3 and v4 are basically the same
                        }
                    }

                    // if it's night and the last peak_trough was greater in magnitude than Max_Battery_Size use Max_Rate 
                    // energy, otherwise we good
                    if(v3_over < 1 && ts_data[prevGoodSampIndex].peak_to_trough_v4 < -Max_Battery_Size) {
                        ts_data[i].recharge_v3_v4 = Max_Rate;
                    }
                    else {
                        ts_data[i].recharge_v3_v4 = 0;
                    }

                    cumulative_v3_v4 += (ts_data[i].v3_v4 + ts_data[i].recharge_v3_v4);
                    ts_data[i].c_v3_v4 = cumulative_v3_v4;

                    // compute peak and trough values
                    // prevGoodSampIndex is Sergey's [i-1]
                    ts_data[i].peak_v4 = MAX(ts_data[prevGoodSampIndex].peak_v4, ts_data[i].c_v3_v4);
                    if(ts_data[i].c_v3_v4 < ts_data[prevGoodSampIndex].peak_v4) {
                        ts_data[i].trough_v4 = MIN(ts_data[prevGoodSampIndex].trough_v4, ts_data[i].c_v3_v4);
                    }
                    else {
                        ts_data[i].trough_v4 = ts_data[i].peak_v4;
                    }

                    ts_data[i].peak_to_trough_v4 = ts_data[i].trough_v4 - ts_data[i].peak_v4; // signed value

                    // min_peak_to_trough_v4 is used in state_of_charge calculation, which is done later
                    // this is the min value over the entire time-series

                    // snapshot values are for T/S output -- not needed for actual cost algo
                    if(ts_data[i].peak_to_trough_v4 < costData.min_peak_to_trough_v4) {
                        costData.min_peak_to_trough_v4 = ts_data[i].peak_to_trough_v4;
                        ts_data[i].min_peak_to_trough_v4 = costData.min_peak_to_trough_v4; // snapshot value
                    }

                    if(costData.storage_size > ts_data[i].peak_to_trough_v4) { // both of these values run negative
                        costData.storage_size = ts_data[i].peak_to_trough_v4;
                        ts_data[i].storage_size = costData.storage_size; // snapshot value
                    }

                    costData.total_recharge += ts_data[i].recharge_v3_v4;
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


                // Now compute state_of_charge and curtailment (check this against Rich's Excel code)
                for(i = 1; i < fci->numTotalSamples; i++) {
                    /*
                                                 if(costData.min_peak_to_trough_v4 < 0.1) {
                                                     ts_data[i].state_of_charge = 0;
                                                 }
                                                 else {
                                                     ts_data[i].state_of_charge =
                                                             1 - ((ts_data[i].peak_to_trough_v4 + ts_data[i].c_v3_v4 - ts_data[i].trough_v4) / costData.storage_size);
                                                     if(ts_data[i].state_of_charge >= 1 && ts_data[i].v3_v4 > 0)
                                                         ts_data[i].curtailment_v4 = ts_data[i].v3_v4 / 1000;
                                                     else
                                                         ts_data[i].curtailment_v4 = 0;
                                                 }
                     */
                    ts_data[i].state_of_charge =
                            1 - ((ts_data[i].peak_to_trough_v4 + ts_data[i].c_v3_v4 - ts_data[i].trough_v4) / costData.storage_size);
                    if(ts_data[i].state_of_charge >= 1 && ts_data[i].v3_v4 > 0)
                        ts_data[i].curtailment_v4 = ts_data[i].v3_v4 / 1000;
                    else
                        ts_data[i].curtailment_v4 = 0;
                    costData.total_curtailment += ts_data[i].curtailment_v4;

                    if(0) {
                        if(i == 1)
                            fprintf(OUT, "year,mon,day,hour,min,v3,v4,v3over,v3_v4,recharge_v3_v4,c_v3_v4,peak_v4,trough_v4,peak_to_trough_v4,storage_size,total_recharge,total_v3,total_v4,state_of_charge,curtailment\n");
                        fprintf(OUT, "%s,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.1f\n",
                                dtToStringCsvCompact(&ts_data[i].dateTime), ts_data[i].v3, ts_data[i].v4, ts_data[i].v3 * Oversize, ts_data[i].v3_v4, ts_data[i].recharge_v3_v4, ts_data[i].c_v3_v4, ts_data[i].peak_v4, ts_data[i].trough_v4,
                                ts_data[i].peak_to_trough_v4, ts_data[i].storage_size, ts_data[i].total_recharge, ts_data[i].total_energy_v3_over, ts_data[i].total_energy_v4, ts_data[i].state_of_charge, ts_data[i].curtailment_v4);
                    } //ts_data[i].v3 = ts_data[i].v3 / Oversize; // scale back


                    //                    }
                }

                // if total_cost < min_cost set all the parameters to current
                costData.total_loss = (costData.total_curtailment - (costData.total_energy_v3_over - costData.total_energy_v4) / 1000) * Remuneration;
                costData.storage_size = costData.storage_size / Storage_Efficiency;
                costData.total_recharge = costData.total_recharge / 1000;
                costData.total_recharge_cost = costData.total_recharge * Recharge_Cost * Life_Span;
                costData.total_cost = PV_Cost * (Oversize - 1.0) - costData.storage_size * Storage_Cost / 1000 + costData.total_recharge_cost + costData.total_loss * Life_Span;
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
                }

                /*
                                if(doDump) {
                                    fprintf(OUT, "\ttotal_loss = %.1f\n", costData.total_loss);
                                    fprintf(OUT, "\tstorage_size = %.1f\n", costData.storage_size);
                                    fprintf(OUT, "\ttotal_recharge = %.1f\n", costData.total_recharge);
                                    fprintf(OUT, "\ttotal_recharge_cost = %.1f\n", costData.total_recharge_cost);
                                    fprintf(OUT, "\ttotal_cost = %.1f\n", costData.total_cost);
                                    fprintf(OUT, "\ttotal_energy_v3over = %.1f\n", costData.total_energy_v3_over);
                                    fprintf(OUT, "\ttotal_energy_v4 = %.1f\n", costData.total_energy_v4);
                                    fprintf(OUT, "\ttotal_curtailment = %.1f\n", costData.total_curtailment);
                                    fprintf(OUT, "\toversize = %.1f\n", costData.oversize);
                                    fprintf(OUT, "\tmax_battery_size = %.1f\n", costData.max_battery_size);
                                    fprintf(OUT, "\tmax_rate = %.02f\n", costData.max_rate);
                                    fclose(OUT);
                                }
                                if(0) {
                                    fprintf(stderr, "[%d] Rate=%.1f ", perm_index, costData.max_rate);
                                    fprintf(stderr, "Ovr=%.1f ", costData.oversize);
                                    fprintf(stderr, "Batt=%.1f ", costData.max_battery_size);
                                    fprintf(stderr, "stSz=%.1f ", costData.storage_size);
                                    fprintf(stderr, "totRe=%.1f ", costData.total_recharge);
                                    fprintf(stderr, "totReCst=%.1f ", costData.total_recharge_cost);
                                    fprintf(stderr, "totCst=%.1f\n", costData.total_cost);
                                }
                 */
                perm_index++;
            }
        }
    }

    if(fci->saveLowCostTimeSeries) {
        if(lowestCostData->lowestCostTimeSeries != NULL) // left over from phase1
            free(lowestCostData->lowestCostTimeSeries);
        lowestCostData->lowestCostTimeSeries = lowestCostTimeSeries;
    }

    //fprintf(stderr, "[%d] finished cost loop -- permutations = %d elapsed time = %ld\n", threadNum, perm_index, time(NULL) - beginTime);

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

    fprintf(stderr, "TIMER:%d,%ld\n", threadNum, time(NULL) - beginTime);
}
