/* 
 Forecast Optimizer.
 */

/* todo
 */

#include "forecastOpt.h"
#include "forecastOptUtils.h"

#define VERSION "1.1"

//#define DUMP_ALL_DATA
//#define DEBUG_HAS

void help(void);
void version(void);
void processForecast(forecastInputType *fci);
void runErrorAnalysis(forecastInputType *fci);

char *Progname;
char ErrStr[4096];
char MultipleSites;

int main(int argc,char **argv)
{
    forecastInputType fci;
    int i;
    //signal(SIGSEGV, segvhandler); // catch memory reference errors

    Progname = basename(argv[0]);  // strip out path
    fprintf(stderr, "=== Running %s ", Progname);
    for(i=1;i<argc;i++) fprintf(stderr, "%s%c", argv[i], i==argc-1 ? '\n':' ');
        
    initForecastInfo(&fci);

    if (!parseArgs(&fci, argc, argv)) {
       exit(1);
    }
 
    processForecast(&fci);
           
    return(EXIT_SUCCESS);
}
  
int parseArgs(forecastInputType *fci, int argC, char **argV)
{
    int c;
    extern char *optarg;
    extern int optind;
    int parseDates(forecastInputType *fci, char *optarg);

    //static char tabDel[32];
    //sprintf(tabDel, "%c", 9);  // ascii 9 = TAB
    
    while ((c=getopt(argC, argV, "b:kpfa:c:dto:s:HhvVr:mw:")) != EOF) {
        switch (c) {
            case 'd': { fci->delimiter = ",";
                        break; }
            case 'a': { if(!parseDates(fci, optarg)) {
                        fprintf(stderr, "Failed to process archive dates\n");
                        return False;
                        }
                        break; }
            case 'o': { fci->outputDirectory = strdup(optarg);
                        break; }
            case 'c': { fci->descriptionFile.fileName = strdup(optarg);
                        break; }
            case 's': { fci->maxHoursAfterSunrise = atoi(optarg);
                        if(fci->maxHoursAfterSunrise > MAX_HOURS_AFTER_SUNRISE || fci->maxHoursAfterSunrise < 1) {
                            fprintf(stderr, "max hours after surise out of range: %d\n", fci->maxHoursAfterSunrise);
                            return False;
                        }
                        fci->runHoursAfterSunrise = True;
                        fci->startHourHighIndex = fci->maxHoursAfterSunrise;
                        fci->startHourLowIndex = 0;
                        break; }
            case 't': { fci->delimiter = "\t";
                        break; }

            case 'v': { fci->verbose = True;
                        break; }
            case 'r': { if(!parseHourIndexes(fci, optarg)) {
                        fprintf(stderr, "Failed to process archive dates\n");
                        return False;
                        }
                        break; }
            case 'h': help();
                      return(False);
            case 'V': version();
                      return(False);
            case 'm': { fci->multipleSites = True;
                        break; }
            case 'k': { fci->skipPhase2 = True;
                        break; }
            case 'f': { fci->filterWithSatModel = False;
                        break; }
            case 'b': { fci->numDivisions = atoi(optarg);
                        break; }
            case 'w': { fci->modelMixFileInput.fileName = strdup(optarg);
                        fci->runWeightedErrorAnalysis = True;
                        break; }
            case 'p': { fci->genModelMixPermutations = False; 
                        break; } 
            default:  return False;         
        }       
    }  

    if(optind < argC) {  // assume any left over args are fileNames
      fci->forecastTableFile.fileName = strdup(argV[optind]);
    }
    else {
        help();
        return False;
    }
    if(fci->descriptionFile.fileName == NULL) {
        FatalError("parseArgs()", "no -c configFile arg given");
    }
    
    return True;
} 

void help(void) 
{
    version();
    printf( "usage: %s [-dsmpkvh] [-r beginHourIndex,endHourIndex] [-a begin,end] [-o outputDir] [-b divisions] -c configFile forecastFile\n", Progname);
    printf( "where: -d = comma separated input [TAB]\n");
    printf( "       -s maxHours = set max hours after sunrise\n");
    printf( "       -m = input data file contains multiple sites (concatenated)\n");
    printf( "       -p = don't generate all model mix permutations -- just what's in conf file\n");
    printf( "       -k = skip phase 2 optimization\n");
    printf( "       -r beginHourIndex,endHourIndex = specify which hour ahead indexes to start and end with\n");
    printf( "       -a begin,end = specify begin and end dates in YYYYMMDD,YYYYMMDD format\n");
    printf( "       -o outputDir = specify where output files go\n");
    printf( "       -b divisions = specify how many intervals the 0..100 weight range is divided into [def=7]\n");    
    printf( "       -c configFile = specify config file\n");
    printf( "       -v = be versbose\n");
    printf( "       -h = help\n");
    printf( "       forecastFile = .csv forecast file\n");
}

void version(void)
{
  fprintf(stderr,"%s %s built [%s %s]\n", Progname, VERSION, __TIME__, __DATE__);
  return;
} 

void processForecast(forecastInputType *fci)
{        
    time_t start;
    
    start = time(NULL);
    fprintf(stderr, "=== Starting processing at %s\n", timeOfDayStr());
    fprintf(stderr, "=== Using date range: %s to ", dtToStringDateTime(&fci->startDate));
    fprintf(stderr, "%s\n", dtToStringDateTime(&fci->endDate));
    fprintf(stderr, "=== Weight sum range: %d to %d\n", fci->weightSumLowCutoff, fci->weightSumHighCutoff);

    readForecastFile(fci);  // this reads in all the forecast data (either single or composite) for all forecast horizons
    
    if(fci->runHoursAfterSunrise) 
        copyHoursAfterData(fci);
    fprintf(stderr, "=== Hours Ahead: %d to %d\n", fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead);
    fprintf(stderr, "=== Number of    input records: %d\n", fci->numInputRecords);
    fprintf(stderr, "=== Number of daylight records: %d\n", fci->numDaylightRecords);
               
    if(fci->genModelMixPermutations) {
        int i;
        genPermutationMatrix(fci);
        for(i=0; i<=fci->perm.numPermutations; i++) {
            setPermutation(fci, i);  // set the models on/off switches according to perm
            runErrorAnalysis(fci);
        }
    }
    else {
        runErrorAnalysis(fci);
    }
     
    fprintf(stderr, "=== Ending at %s\n", timeOfDayStr());
    fprintf(stderr, "=== Elapsed time: %s\n", getElapsedTime(start));
    return;
}

void runErrorAnalysis(forecastInputType *fci) 
{
    int hoursAheadIndex, hoursAfterSunriseIndex;
    
    if(fci->runHoursAfterSunrise) {
        for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex < fci->maxHoursAfterSunrise; hoursAheadIndex++) {
            int numHASwithData = 0;
            for(hoursAfterSunriseIndex=0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
                fprintf(stderr, "\n############ Running for hour ahead %d, hour after sunrise %d\n\n", 
                        fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex].hoursAhead, fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex].hoursAfterSunrise);
                computeModelRMSE(fci, hoursAheadIndex, hoursAfterSunriseIndex);
                dumpNumModelsReportingTable(fci);
                printRmseTableHour(fci, hoursAheadIndex, hoursAfterSunriseIndex);
                printHourlySummary(fci, hoursAheadIndex, hoursAfterSunriseIndex);
                
                numHASwithData += runOptimizerNested(fci, hoursAheadIndex, hoursAfterSunriseIndex);
                
                fprintf(stderr, "\n############ End hour ahead %d\n", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
            }
            if(numHASwithData) {
                dumpHourlyOptimizedTS(fci, hoursAheadIndex);
                dumpModelMixRMSE(fci, hoursAheadIndex);
            }
        }
        dumpHoursAfterSunrise(fci);
        dumpModelMix_EachModel_HAxHAS(fci);
        dumpModelMix_EachHAS_HAxModel(fci);
/*
        for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex < fci->maxHoursAfterSunrise; hoursAheadIndex++) {
            dumpModelMix(fci, hoursAheadIndex);
            //dumpWeightedTimeSeries(fci, hoursAheadIndex, -1);
        }
*/
    }

    else {
        for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
            fprintf(stderr, "\n############ Running for hour ahead %d\n\n", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
            computeModelRMSE(fci, hoursAheadIndex, -1);
            dumpNumModelsReportingTable(fci);
            printRmseTableHour(fci, hoursAheadIndex, -1);
            printHourlySummary(fci, hoursAheadIndex, -1);
            if(runOptimizerNested(fci, hoursAheadIndex, -1)) {
                dumpHourlyOptimizedTS(fci, hoursAheadIndex);
                fprintf(stderr, "\n############ End hour ahead %d\n", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
                dumpModelMixRMSE(fci, hoursAheadIndex);
            }
        }
        printHoursAheadSummaryCsv(fci);
    }
    

//    printByHour(fci);
//    printByModel(fci);
    printByAnalysisType(fci);
    
    
    if(fci->multipleSites) {
        // Now go through all the individual site data with the weights derived from the composite run
        // 1) re-read the site T/S data
        // 2) run the RMSE
        // 3) write summary somewhere
    }
}


