/* 
 Forecast Optimizer.
 */

/* todo
 * 1) numModelReporting should be just one file, not one for every perm
 * 2) *modelMix.HA_HAS.allPermutations.csv is stopping after perm 1
 * 3) *forecastSummary* is only generating a file for perm 1
 * 4) in v4 runs, first hour is often missing (west)
 */

#include "forecastOpt.h"
#include "forecastOptUtils.h"

#define VERSION "1.2"

//#define DUMP_ALL_DATA
//#define DEBUG_HAS

void help(void);
void version(void);
void processForecast(forecastInputType *fci);
void runErrorAnalysis(forecastInputType *fci, int permutationIndex);
void runErrorAnalysisBootstrap(forecastInputType *fci, int permutationIndex);

char *Progname;
char ErrStr[4096];
char MultipleSites;

int main(int argc, char **argv)
{
    forecastInputType fci;
    int i;
    //signal(SIGSEGV, segvhandler); // catch memory reference errors

    Progname = basename(argv[0]); // strip out path
    fprintf(stderr, "=== Running [%s ", Progname);
    for(i = 1; i < argc; i++) fprintf(stderr, "%s%s", argv[i], i == argc - 1 ? "]\n" : " ");

    // set default
    initForecastInfo(&fci);

    if(!parseArgs(&fci, argc, argv)) {
        exit(1);
    }

    processForecast(&fci);

    return (EXIT_SUCCESS);
}

int parseArgs(forecastInputType *fci, int argC, char **argV)
{
    int c;
    extern char *optarg;
    extern int optind;
    int parseDates(forecastInputType *fci, char *optarg);

    fci->doKtNWP = True;
    omp_set_num_threads(1);

    //static char tabDel[32];
    //sprintf(tabDel, "%c", 9);  // ascii 9 = TAB

    while((c = getopt(argC, argV, "b:kpfa:do:s:HhvSVr:w:i:BuK:Ct:gD:m:")) != EOF) {
        switch(c) {
            case 'd':
            {
                fci->doKtAndNonKt = True;
                break;
            }
            case 'a':
            {
                if(!parseDates(fci, optarg)) {
                    fprintf(stderr, "Failed to process archive dates\n");
                    return False;
                }
                break;
            }
            case 'i':
            {
                fci->inputDirectory = strdup(optarg);
                break;
            }
            case 'o':
            {
                fci->outputDirectory = strdup(optarg);
                break;
            }
            case 's':
            {
                fci->maxHoursAfterSunrise = atoi(optarg);
                if(fci->maxHoursAfterSunrise > MAX_HOURS_AFTER_SUNRISE || fci->maxHoursAfterSunrise < 1) {
                    fprintf(stderr, "max hours after surise out of range: %d\n", fci->maxHoursAfterSunrise);
                    return False;
                }
                fci->runHoursAfterSunrise = True;
                /*
                                fci->startHourHighIndex = fci->maxHoursAfterSunrise;
                                fci->startHourLowIndex = 0;
                 */
                break;
            }
            case 't':
            {
                fci->omp_num_threads = atoi(optarg);
                if(fci->omp_num_threads > 64 || fci->omp_num_threads < 1) {
                    fprintf(stderr, "Bad argument to -t : expecting number of CPUs/threads to use\n");
                    return False;
                }
                omp_set_num_threads(fci->omp_num_threads);
                break;
            }

            case 'v':
            {
                fci->verbose = True;
                break;
            }
            case 'r':
            {
                if(!parseHourIndexes(fci, optarg)) {
                    fprintf(stderr, "Failed to process hour indexes\n");
                    return False;
                }
                break;
            }
            case 'h': help();
                return (False);
            case 'V': version();
                return (False);
            case 'p':
            {
                fci->skipPhase2 = True;
                break;
            }
            case 'f':
            {
                fci->filterWithSatModel = False;
                break;
            }
            case 'b':
            {
                fci->numDivisions = atoi(optarg);
                break;
            }
            case 'k':
            {
                fci->numKtBins = 1; // override KTI mechanism
                fci->doKtNWP = False;
                break;
            }
            case 'w':
            {
                if(!parseWeightSumLimits(fci, optarg)) {
                    fprintf(stderr, "Failed to process -w flag args\n");
                    return False;
                }
                break;
            }
            case 'P':
            {
                fci->doModelPermutations = False;
                break;
            }
            case 'S':
            {
                fci->useSatelliteDataAsRef = True;
                fci->groundGHICol = 6;
                fci->satGHICol = 5;
                fprintf(stderr, "[NOTE: Using satGHI as reference]\n");
                break;
            }
            case 'B':
            {
                fci->doKtBootstrap = True; // this means run v4 and then bootstrap
                fci->doKtNWP = True;
                break;
            }
            case 'K':
            {
                fci->ktModelColumnName = strdup(optarg);
                break;
            }
            case 'u':
            {
                fci->dumpFilterData = True;
                break;
            }
            case 'C':
            {
                fci->errorMetric = Cost;
                break;
            }
            case 'g':
            {
                fci->useV4fromFile = True;
                fci->groundGHICol = 6;
                fci->satGHICol = 5;
                break;
            }
            case 'D':
            {
                fci->disabledModels = strdup(optarg);
                break;
            }
            case 'm':
                if(strcmp(optarg, "RMSE") == 0 || strcmp(optarg, "rmse") == 0) {
                    fci->errorMetric = RMSE;
                    fci->errorMetricName = "RMSEpct";
                }
                else if(strcmp(optarg, "MAE") == 0 || strcmp(optarg, "mae") == 0) {
                    fci->errorMetric = MAE;
                    fci->errorMetricName = "MAEpct";
                }
                else if(strcmp(optarg, "MBE") == 0 || strcmp(optarg, "mbe") == 0) {
                    fci->errorMetric = MBE;
                    fci->errorMetricName = "MBEpct";
                }
                else {
                    fprintf(stderr, "Got bad arg to -m flag : %s\n", optarg);
                    return False;
                }
                break;

            default: return False;
        }
    }

    if(fci->inputDirectory == NULL) {
        FatalError("parseArgs()", "no -i inputDirectory arg given");
    }
    if(fci->outputDirectory == NULL) {
        FatalError("parseArgs()", "no -o outputDirectory arg given");
    }

    if(fci->runHoursAfterSunrise) { // allocate modelRunType data structure space
        int i, j;
        fci->hoursAfterSunriseGroup = malloc(MAX_HOURS_AHEAD * sizeof (modelRunType **));
        for(i = 0; i < MAX_HOURS_AHEAD; i++) {
            fci->hoursAfterSunriseGroup[i] = malloc(MAX_HOURS_AFTER_SUNRISE * sizeof (modelRunType *));
            for(j = 0; j < MAX_HOURS_AFTER_SUNRISE; j++)
                fci->hoursAfterSunriseGroup[i][j] = malloc(MAX_KT_BINS * sizeof (modelRunType));
        }
        long memHASgroup = MAX_HOURS_AHEAD * sizeof (modelRunType **);
        long memHAShour = MAX_HOURS_AFTER_SUNRISE * sizeof (modelRunType *) * MAX_HOURS_AHEAD;
        long memHAShrHas = MAX_KT_BINS * sizeof (modelRunType) * MAX_HOURS_AHEAD * MAX_HOURS_AFTER_SUNRISE;
        long total = memHASgroup + memHAShour + memHAShrHas;
        fprintf(stderr, "Memory allocated for modelRunType = %ld\n", total);
    }
    /*
        For numDivisions =  5, increment1 = 100/5 = 20 => 0,20,40,60,80,100
        For numDivisions =  7, increment1 = 100/7 = 14 => 7,14,28,42,56,70,84,98
        For numDivisions = 10, increment1 = 100/10 = 10 => 10,20,30,40,50,60,70,80,90,100
     */
    fci->increment1 = 100 / fci->numDivisions; // numDivisions=10 => increment1=10, 5 => 20, 25 => 4
    fci->numDivisions2 = (fci->increment1) / 2; // might be better off with -(numDivisions2/2)
    fci->increment2 = MAX(1, (fci->increment1 / fci->numDivisions)); // 2*20/5 = 8, 2*14/7 = 4, 2*10/10 = 2, 2*4/25 = 1

    fprintf(stderr, "\n======== Weight Search Settings =========\n");
    fprintf(stderr, "===== increment1 = %d\n", fci->increment1);
    fprintf(stderr, "===== numDivisions = %d\n", fci->numDivisions);
    if(!fci->skipPhase2) {
        fprintf(stderr, "===== increment2 = %d\n", fci->increment2);
        fprintf(stderr, "===== numDivisions2 = %d\n", fci->numDivisions2);
    }
    fprintf(stderr, "===========================================\n");

    sprintf(fci->parameterStamp, "wtSum_%d-%d.step_%d", fci->weightSumLowCutoff, fci->weightSumHighCutoff, fci->increment1);

    return True;
}

void help(void)
{
    version();
    printf("usage: %s [-dsmpkSBfvh] [-r beginHourIndex,endHourIndex] [-m errorMetric] [-a begin,end] [-w low,high] [-o outputDir] [-b divisions] [-t opm_num_threads] forecastFile\n", Progname);
    printf("where: -d = comma separated input [TAB]\n");
    printf("       -s maxHours = set max hours after sunrise\n");
    printf("       -p = skip phase 2 optimization\n");
    printf("       -k = don't do kt binning\n");
    printf("       -S = use satellite model data as reference\n");
    printf("       -r beginHourIndex,endHourIndex = specify which hour ahead indexes to start and end with\n");
    printf("       -m errorMetric = one of RMSE, MAE, MBE [RMSE]\n");
    printf("       -a begin,end = specify begin and end dates in YYYYMMDD,YYYYMMDD format\n");
    printf("       -w low,high = specify low and high weight sum cutoffs [92,108]\n");
    printf("       -o outputDir = specify where output files go\n");
    printf("       -b divisions = specify how many intervals the 0..100 weight range is divided into [def=7]\n");
    printf("       -B = run in kt bootstrap mode\n");
    printf("       -f = don't filter with satModel data\n");
    printf("       -t opm_num_threads = set number of CPUs to use\n");
    printf("       -v = be verbose\n");
    printf("       -h = help\n");
    printf("       forecastFile = .csv forecast file\n");
}

void version(void)
{
    fprintf(stderr, "%s %s built [%s %s]\n", Progname, VERSION, __TIME__, __DATE__);
    return;
}

void processForecast(forecastInputType *fci)
{
    time_t start;
    int permutationIndex;

    start = time(NULL);
    fprintf(stderr, "=== Starting processing at %s\n", timeOfDayStr());
    fprintf(stderr, "=== Using date range: %s to ", dtToStringDateTime(&fci->startDate));
    fprintf(stderr, "%s\n", dtToStringDateTime(&fci->endDate));
    fprintf(stderr, "=== Using metric %s\n", fci->errorMetricName);
    fprintf(stderr, "=== Weight sum range: %d to %d\n", fci->weightSumLowCutoff, fci->weightSumHighCutoff);

    if(fci->errorMetric != Cost) {
        if(!readForecastDataNoNight(fci)) // this reads in all the forecast data (either single site or composite) for all forecast horizons
            return;
    }
    else
        readForecastData(fci);

    if(fci->runHoursAfterSunrise)
        copyHoursAfterData(fci);

    fprintf(stderr, "=== Hours Ahead: %d to %d\n", fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead);
    fprintf(stderr, "=== Number of    input records: %d\n", fci->numInputRecords);
    fprintf(stderr, "=== Number of daylight records: %d\n", fci->numDaylightRecords);

    initPermutationSwitches(fci);

    // default mode : run NWP-based kt opt
    // -k mode : v4 equivalent
    // -B bootstrap mode : run with v4 kt and then recompute kt based on that and re-run

    //for(permutationIndex = 1; permutationIndex < fci->modelPermutations.numPermutations; permutationIndex++) {
    //for(permutationIndex = 31; permutationIndex < fci->modelPermutations.numPermutations; permutationIndex++) {
    for(permutationIndex = fci->modelPermutations.numPermutations - 1; permutationIndex < fci->modelPermutations.numPermutations; permutationIndex++) {
        //    for(permutationIndex = fci->modelPermutations.numPermutations-1; permutationIndex < fci->modelPermutations.numPermutations; permutationIndex++) {
        if(fci->doKtBootstrap) {
            fci->numKtBins = 1;
            runErrorAnalysis(fci, permutationIndex); // this produces v4
            fci->numKtBins = 6;
            runErrorAnalysisBootstrap(fci, permutationIndex);
        }
        else {
            if(fci->doKtAndNonKt) {
                fci->numKtBins = 1;
                runErrorAnalysis(fci, permutationIndex); // this produces v4
                fci->numKtBins = 6;
            }
            runErrorAnalysis(fci, permutationIndex); // this produces v4x
        }
    }

    fprintf(stderr, "=== Ending at %s\n", timeOfDayStr());
    fprintf(stderr, "=== Elapsed time: %s\n", getElapsedTime(start));
    return;
}

void runErrorAnalysis(forecastInputType *fci, int permutationIndex)
{
    int hoursAheadIndex, hoursAfterSunriseIndex, ktIndex;

    fci->inKtBootstrap = False;

    if(fci->runHoursAfterSunrise) {
        for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
            int numHASwithData = 0;
            for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
                //ktIndex = 0;
                for(ktIndex = 0; ktIndex < fci->numKtBins; ktIndex++) {
                    setModelSwitches(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, permutationIndex);
                    fprintf(stderr, "\n############ Running for HA/HAS/KTI/permIndex = %d/%d/%d/%d\n\n",
                            fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex].hoursAhead,
                            fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex].hoursAfterSunrise,
                            ktIndex,
                            permutationIndex);
                    filterDataAndComputeErrors(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);
                    //dumpNumModelsReportingTable(fci);
                    printHourlyErrorTable(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);
                    printHourlySummary(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);

                    // This is the actual optimizer call
                    numHASwithData += runOptimizerParallel(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);
                    //fprintf(stderr, "\n############ End hour ahead %d\n", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
                }
            }
            if(numHASwithData) {
                computeOptimizedGHI(fci, hoursAheadIndex);
                dumpHourlyOptimizedTS(fci, hoursAheadIndex);
                dumpModelMix(fci, hoursAheadIndex);
            }
        }
        dumpHoursAfterSunrise(fci);
        //dumpModelMix_EachModel_HAxHAS(fci);
        //dumpModelMix_EachHAS_HAxModel(fci);
        /*
                for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex < fci->maxHoursAfterSunrise; hoursAheadIndex++) {
                    dumpModelMix(fci, hoursAheadIndex);
                    //dumpWeightedTimeSeries(fci, hoursAheadIndex, -1);
                }
         */
    }

    else {
        for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
            fprintf(stderr, "\n############ Running for hour ahead %d\n\n", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
            setModelSwitches(fci, hoursAheadIndex, -1, -1, permutationIndex);
            filterDataAndComputeErrors(fci, hoursAheadIndex, -1, -1);
            //dumpNumModelsReportingTable(fci);
            printHourlyErrorTable(fci, hoursAheadIndex, -1, -1);
            printHourlySummary(fci, hoursAheadIndex, -1, -1);
            if(runOptimizerParallelCost(fci, hoursAheadIndex, -1, -1)) {
                computeOptimizedGHI(fci, hoursAheadIndex);
                dumpHourlyOptimizedTS(fci, hoursAheadIndex);
                //fprintf(stderr, "\n############ End hour ahead %d\n", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
                dumpModelMix(fci, hoursAheadIndex);
            }
        }
        //printHoursAheadSummaryCsv(fci);
    }


    //    printByHour(fci);
    //    printByModel(fci);
    //printByAnalysisType(fci);

    if(fci->multipleSites) {
        // Now go through all the individual site data with the weights derived from the composite run
        // 1) re-read the site T/S data
        // 2) run the RMSE
        // 3) write summary somewhere
    }
    fclose(fci->modelMixFileOutput.fp);
    fci->modelMixFileOutput.fp = NULL;
}

void runErrorAnalysisBootstrap(forecastInputType *fci, int permutationIndex)
{
    int hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, sampleInd;
    timeSeriesType *thisSample;

    fci->inKtBootstrap = True;

    for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        int numHASwithData = 0;
        /*
        // set numKtBins temporarily to 1
        // run optimizer and save kt's based on that run
        // rerun optimizer with numKtBins restored and using previous kt's
        for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
            setModelSwitches(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, permutationIndex);
            fprintf(stderr, "############ Running bootstrap phase for HA/HAS/permIndex = %d/%d/%d\n\n",
                    fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex].hoursAhead,
                    fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex].hoursAfterSunrise,
                    permutationIndex);
            filterDataAndComputeErrors(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);
            //dumpNumModelsReportingTable(fci);
            printHourlyErrorTable(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);
            printHourlySummary(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);

            // This is the actual optimizer call
            numHASwithData += runOptimizerNested(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);

            //fprintf(stderr, "\n############ End hour ahead %d\n", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
        }
        if(numHASwithData) {
            dumpHourlyOptimizedTS(fci, hoursAheadIndex); // this is where the kt get computed
            dumpModelMix(fci, hoursAheadIndex);
        }

         */

        //////////////
        // Now run again with ktV4 as extra dimension
        //////////////

        // set the proper kt index for this HA using ktV4
        for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
            thisSample = &fci->timeSeries[sampleInd];
            setKtIndex(fci, thisSample, hoursAheadIndex); // set the ktIndex for bootstrapping
        }

        for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
            for(ktIndex = 0; ktIndex < fci->numKtBins; ktIndex++) {
                setModelSwitches(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, permutationIndex);
                fprintf(stderr, "############ Running Bootstrap KTI phase for HA/HAS/permIndex = %d/%d/%d/%d\n\n",
                        fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex].hoursAhead,
                        fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex].hoursAfterSunrise,
                        ktIndex,
                        permutationIndex);
                filterDataAndComputeErrors(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);
                //dumpNumModelsReportingTable(fci);
                printHourlyErrorTable(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);
                printHourlySummary(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);

                // This is the actual optimizer call
                numHASwithData += runOptimizerParallel(fci, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex);

                //fprintf(stderr, "\n############ End hour ahead %d\n", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
            }
        }
        if(numHASwithData) {
            computeOptimizedGHI(fci, hoursAheadIndex);
            dumpHourlyOptimizedTS(fci, hoursAheadIndex); // this is where the kt get computed
            dumpModelMix(fci, hoursAheadIndex);
        }
    }
    dumpHoursAfterSunrise(fci);
    //dumpModelMix_EachModel_HAxHAS(fci);
    //dumpModelMix_EachHAS_HAxModel(fci);
    /*
            for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex < fci->maxHoursAfterSunrise; hoursAheadIndex++) {
                dumpModelMix(fci, hoursAheadIndex);
                //dumpWeightedTimeSeries(fci, hoursAheadIndex, -1);
            }
     */

    //    printByHour(fci);
    //    printByModel(fci);
    //printByAnalysisType(fci);

    if(fci->multipleSites) {
        // Now go through all the individual site data with the weights derived from the composite run
        // 1) re-read the site T/S data
        // 2) run the RMSE
        // 3) write summary somewhere
    }
    fclose(fci->modelMixFileOutput.fp);
    fci->modelMixFileOutput.fp = NULL;
}

