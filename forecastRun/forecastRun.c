/* 
 Forecast Optimizer Time Series Generator
 * 
 * This program takes a forecast weight set (previously generated) and a stream of
 * forecast data and applies the former to the latter.  
 * 
 * It needs to be adaptable to the actual forecast model input data availability.
 * That is, the weights used need to be from forecastOpt runs that used only the 
 * models that are available in this run.  Otherwise, we can end up with the situation
 * where only, say, 3 out of 4 of the models are present and the weights of the 3 present
 * models only add up to .52, leading to a low and probably quite erroneous GHI 
 * estimation.
 * 
 *   config/command line options to accamodate lat/lon window
 *   sql code and config for grid file identification
 *   lat/lon wrapper to cycle over all points
 *   output change format from csv to grid files
 *   debugging output changes
 * 
 */

#include "forecastOpt.h"
#include "forecastOptUtils.h"

#define VERSION "1.0"

#define MIN_IRR -25
#define MAX_IRR 1500
//#define DUMP_ALL_DATA
//#define DEBUG_HAS

#define IsReference 1
#define IsNotReference 0
#define IsForecast 1
#define IsNotForecast 0

void help(void);
void version(void);
int  parseArgs(forecastInputType *fci, int argC, char **argV);
int  runWeightedTimeSeriesAnalysis(forecastInputType *fci);

char *Progname;
char ErrStr[4096];

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
 
    runWeightedTimeSeriesAnalysis(&fci);
           
    return(EXIT_SUCCESS);
}
  
int parseArgs(forecastInputType *fci, int argC, char **argV)
{
    int c;
    extern char *optarg;
    extern int optind;
    int parseDates(forecastInputType *fci, char *optarg);

    fci->runOptimizer = False;    

    //static char tabDel[32];
    //sprintf(tabDel, "%c", 9);  // ascii 9 = TAB
    
    while ((c=getopt(argC, argV, "fa:c:dto:s:HhvVr:mw:")) != EOF) {
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
            case 'f': { fci->filterWithSatModel = False;
                        break; }
            case 'w': { fci->modelMixFileInput.fileName = strdup(optarg);
                        fci->runWeightedErrorAnalysis = True;
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
    printf( "usage: %s [-dsmhvV] [-r beginHourIndex,endHourIndex] [-a begin,end] [-o outputDir] [-b divisions] -c configFile forecastFile\n", Progname);
    printf( "where: -d = comma separated input [TAB]\n");
    printf( "       -s max hours after sunrise\n");
    printf( "       -r beginHourIndex,endHourIndex = specify which hour ahead indexes to start and end with\n");
    printf( "       -m input data file contains multiple sites (concatenated)\n");
    printf( "       -c configFile = specify config file\n");
    printf( "       -o outputDir = specify where output files go\n");
    printf( "       -v = be versbose\n");
    printf( "       -h = help\n");
    printf( "       forecastFile = .csv forecast file\n");
}

void version(void)
{
  fprintf(stderr,"%s %s built [%s %s]\n", Progname, VERSION, __TIME__, __DATE__);
  return;
} 


// This is where we read in the model weights that were generated in a previous run
// The weights will be applied to the T/S forecast file of a particular site
// 
// Notes: 
//   this file should be parsed after the config and forecast T/S files.
//   we have to turn on isActive/isContributingModel flags
//   we need to check that all referenced models are legal model names

int readModelMixFile(forecastInputType *fci)
{
    char line[LINE_LENGTH], saveLine[LINE_LENGTH];
    char *fields[MAX_FIELDS];
    int numFields, expectedNumFields, i;
    char *mixModelNames[MAX_MODELS];
    int numMixModelNames=0;
 
    if(!fci->gotConfigFile) 
        FatalError("readModelMixFile()", "Configuration (model description) file not read in.");
    if(!fci->gotForecastFile) 
        FatalError("readModelMixFile()", "Forecast file not read in.");
    
    if((fci->modelMixFileInput.fp = fopen(fci->modelMixFileInput.fileName, "r")) == NULL) {
        fprintf(stderr, "Couldn't open model mix file %s\n", fci->modelMixFileInput.fileName);
        return False;
    }
    
/*  
    modelMix files can look like:
    #siteName=Penn_State_PA,lat=40.72,lon=-77.93
    HA,ncep_GFS_sfc_DSWRF_surface_inst,NDFD_global,ecmwf_ghi,N
    42,0,34,20,0,42,2274
    48,0,30,16,0,50,2300

    or 

    #siteName=Penn_State_PA,lat=40.72,lon=-77.93
    HA,HAS,ncep_GFS_sfc_DSWRF_surface_inst,NDFD_global,cm,ecmwf_ghi,N
    1,1,76,22,0,0,81
    1,2,26,34,36,0,180
*/

    fgets(line, LINE_LENGTH, fci->modelMixFileInput.fp);
    strcpy(saveLine, line);
    // split by space then by =
    numFields = split(line, fields, MAX_FIELDS, " ");  /* split line */
    if(numFields < 3) {
        fprintf(stderr, "Error parsing %s: header line 1: expecting at least 3 fields but got %d: %s\n", fci->modelMixFileInput.fileName, numFields, saveLine);
        exit(1);
    }
    
    // not sure this is strictly necessary
//    setSiteInfo(fci, line);
//    fci->lat = atof(fields[1]);
//    fci->lon = atof(fields[2]);
    
    // now we have to parse the column names line. ex:
    // HA,HAS,ncep_GFS_sfc_DSWRF_surface_inst,NDFD_global,cm,ecmwf_ghi,N

    char *linePtr;
    fgets(line, LINE_LENGTH, fci->modelMixFileInput.fp);  
    linePtr = (*line == '#') ? line+1 : line;  // skip #, if present
    numFields = split(linePtr, fields, MAX_FIELDS, ",");  /* split line */
    expectedNumFields = numFields;  
    
    if(strcmp(fields[0], "HA") != 0) {
        sprintf(ErrStr, "Got header line without HA as first column: got %s instead", line);
        FatalError("readModelMixFile()", ErrStr);
    }
    // now, if the next column is HAS we need to run with hour-ahead
    fci->runHoursAfterSunrise = (strcmp(fields[1], "HAS") == 0);
    
    // read model names...
    int startCol = fci->runHoursAfterSunrise ? 2 : 1;
    for(i=startCol; i<numFields; i++) {
        mixModelNames[i] = strdup(fields[i]);
        numMixModelNames++;
    }
    
    // read in weights 
    int weight, lineNumber = 3;
    int fieldCol, hoursAhead, hoursAfterSunrise;
    int hoursAheadIndex, hoursAfterSunriseIndex, modelIndex;
    int lowIndex, highIndex;
    char *modelName;
    modelRunType *modelRun;
    
    lowIndex = -1;
    highIndex = -1;
    
    while(fgets(line, LINE_LENGTH, fci->modelMixFileInput.fp)) {
        strcpy(saveLine, line);
        numFields = split(line, fields, MAX_FIELDS, ",");  /* split line */
        if(numFields != expectedNumFields) { // just make sure that all lines have the same number of fields     
            sprintf(ErrStr, "in file %s expecting %d fields but got %d at line %d\n", fci->modelMixFileInput.fileName, numFields, expectedNumFields, lineNumber);
            FatalError("readModelMixFile()", ErrStr);
        } 
        
        // need to establish correct modelIndex and hourIndex (and hourAfterSunriseIndex)
        fieldCol = 0;
        hoursAhead = atoi(fields[fieldCol++]);
        if(hoursAhead < 1 || hoursAhead > 500) {
            sprintf(ErrStr, "in file %s HA=%d is out of range at line %d", fci->modelMixFileInput.fileName, hoursAhead, lineNumber);
            FatalError("readModelMixFile()", ErrStr);
        }
        hoursAheadIndex = getHoursAheadIndex(fci, hoursAhead);
        if(lowIndex < 0) 
            lowIndex = hoursAheadIndex;
        if(hoursAheadIndex > highIndex)
            highIndex = hoursAheadIndex;
        
        if(fci->runHoursAfterSunrise) {
            hoursAfterSunrise = atoi(fields[fieldCol++]);
            if(hoursAfterSunrise < 1 || hoursAfterSunrise > 20) {
                sprintf(ErrStr, "in file %s HAS=%d is out of range at line %d", fci->modelMixFileInput.fileName, hoursAfterSunrise, lineNumber);
                FatalError("readModelMixFile()", ErrStr);
            }
            hoursAfterSunriseIndex = hoursAfterSunrise - 1;
            if(hoursAfterSunriseIndex > fci->maxHoursAfterSunrise)
                fci->maxHoursAfterSunrise = hoursAfterSunriseIndex;
        }    
        
        for(; fieldCol < numFields-1; fieldCol++) {  // HA,[HAS],<model1>,Mmodel2>,...,<modeli>,N] => only want <models>
            modelName = mixModelNames[fieldCol];
            if((modelIndex = getModelIndex(fci, modelName)) < 0)
                continue;
            weight = atoi(fields[fieldCol]);
            if(weight != -999 && (weight < 0 || weight > 100)) {
                sprintf(ErrStr, "in file %s weight=%d is out of range at line %d", fci->modelMixFileInput.fileName, weight, lineNumber);
                FatalError("readModelMixFile()", ErrStr);                
            }
            modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];              
            modelRun->hourlyModelStats[modelIndex].isReference = False;
            modelRun->hourlyModelStats[modelIndex].isActive = modelRun->hourlyModelStats[modelIndex].isOn = (weight != -999);
            // also set the HA-only data structure fields for isActive and isOn
            fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isActive = fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isOn = (weight != -999);
            modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase1 = weight;  // set all the weights to this one weight so we can't go wrong
            modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase2 = weight;
            modelRun->hourlyModelStats[modelIndex].weight = weight;
        }
        lineNumber++;
    }
    
    fci->startHourLowIndex = lowIndex;
    fci->startHourHighIndex = highIndex;
    
    fprintf(stderr, "=== Finished reading weights file %s\n", fci->modelMixFileInput.fileName);
    fprintf(stderr, "=== Start hour index = %d (%d hours ahead)\n", fci->startHourLowIndex, fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead);
    fprintf(stderr, "=== End   hour index = %d (%d hours ahead)\n", fci->startHourHighIndex, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead);

    return True;
}

//#define DEBUG_RUN_WEIGHTED
int runWeightedTimeSeriesAnalysis(forecastInputType *fci)
{
    int hoursAheadIndex, hoursAfterSunriseIndex;

    readForecastFile(fci);

    if(!readModelMixFile(fci)) 
        return False;
    
    // OK, so we've got the forecast T/S data file and the weights file
    // now we just want to run the RMSE using the two
    if(fci->runHoursAfterSunrise) {
        for(hoursAheadIndex=fci->startHourLowIndex;hoursAheadIndex<=fci->startHourHighIndex;hoursAheadIndex++) {
            for(hoursAfterSunriseIndex=0;hoursAfterSunriseIndex<=fci->maxHoursAfterSunrise-1;hoursAfterSunriseIndex++) { 
                computeModelRMSE(fci, hoursAheadIndex, hoursAfterSunriseIndex);  // this filters the data and does the model RMSE
                computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, hoursAfterSunriseIndex);
                fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex].optimizedRMSEphase2 = 
                        fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex].weightedModelStats.rmsePct;
#ifdef DEBUG_RUN_WEIGHTED
                fprintf(stderr, "\n=== Hours ahead = %d HAS = %d ===\n", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead, hoursAfterSunriseIndex+1);
                fprintf(stderr, "\nRMSE = %0.1f, N=%d\n", fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex].weightedModelStats.rmsePct * 100, fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex].weightedModelStats.N);
#endif                
            }
            dumpHourlyOptimizedTS(fci, hoursAheadIndex);
            dumpModelMixRMSE(fci, hoursAheadIndex);
        }
        //printHoursAheadSummaryCsv(fci);
    }
    else {
        for(hoursAheadIndex=fci->startHourLowIndex;hoursAheadIndex<=fci->startHourHighIndex;hoursAheadIndex++) {
            computeModelRMSE(fci, hoursAheadIndex, -1);  // this filters the data and does the model RMSE
            computeHourlyRmseErrorWeighted(fci, hoursAheadIndex, -1); 
            fci->hoursAheadGroup[hoursAheadIndex].optimizedRMSEphase2 = fci->hoursAheadGroup[hoursAheadIndex].weightedModelStats.rmsePct;
            dumpHourlyOptimizedTS(fci, hoursAheadIndex);
        }
        printHoursAheadSummaryCsv(fci);
    }
    
    return True;
}

/* 
 Here's the hoursAheadIndex for each hoursAhead
      1	ncep_GFS_sfc_DSWRF_surface_avg_1
     2	ncep_GFS_sfc_DSWRF_surface_avg_2
     3	ncep_GFS_sfc_DSWRF_surface_avg_3
     4	ncep_GFS_sfc_DSWRF_surface_avg_4
     5	ncep_GFS_sfc_DSWRF_surface_avg_5
     6	ncep_GFS_sfc_DSWRF_surface_avg_6
     7	ncep_GFS_sfc_DSWRF_surface_avg_7
     8	ncep_GFS_sfc_DSWRF_surface_avg_8
     9	ncep_GFS_sfc_DSWRF_surface_avg_9
    10	ncep_GFS_sfc_DSWRF_surface_avg_12
    11	ncep_GFS_sfc_DSWRF_surface_avg_15
    12	ncep_GFS_sfc_DSWRF_surface_avg_18
    13	ncep_GFS_sfc_DSWRF_surface_avg_21
    14	ncep_GFS_sfc_DSWRF_surface_avg_24
    15	ncep_GFS_sfc_DSWRF_surface_avg_30
    16	ncep_GFS_sfc_DSWRF_surface_avg_36
    17	ncep_GFS_sfc_DSWRF_surface_avg_42
    18	ncep_GFS_sfc_DSWRF_surface_avg_48
    19	ncep_GFS_sfc_DSWRF_surface_avg_54
    20	ncep_GFS_sfc_DSWRF_surface_avg_60
    21	ncep_GFS_sfc_DSWRF_surface_avg_66
    22	ncep_GFS_sfc_DSWRF_surface_avg_72
    23	ncep_GFS_sfc_DSWRF_surface_avg_78
    24	ncep_GFS_sfc_DSWRF_surface_avg_84
    25	ncep_GFS_sfc_DSWRF_surface_avg_90
    26	ncep_GFS_sfc_DSWRF_surface_avg_96
    27	ncep_GFS_sfc_DSWRF_surface_avg_102
    28	ncep_GFS_sfc_DSWRF_surface_avg_108
    29	ncep_GFS_sfc_DSWRF_surface_avg_114
    30	ncep_GFS_sfc_DSWRF_surface_avg_120
    31	ncep_GFS_sfc_DSWRF_surface_avg_126
    32	ncep_GFS_sfc_DSWRF_surface_avg_132
    33	ncep_GFS_sfc_DSWRF_surface_avg_138
    34	ncep_GFS_sfc_DSWRF_surface_avg_144
    35	ncep_GFS_sfc_DSWRF_surface_avg_150
    36	ncep_GFS_sfc_DSWRF_surface_avg_156
    37	ncep_GFS_sfc_DSWRF_surface_avg_162
    38	ncep_GFS_sfc_DSWRF_surface_avg_168
    39	ncep_GFS_sfc_DSWRF_surface_avg_174
    40	ncep_GFS_sfc_DSWRF_surface_avg_180
    41	ncep_GFS_sfc_DSWRF_surface_avg_186
    42	ncep_GFS_sfc_DSWRF_surface_avg_192
    43	ncep_GFS_sfc_DSWRF_surface_avg_204
    44	ncep_GFS_sfc_DSWRF_surface_avg_216
    45	ncep_GFS_sfc_DSWRF_surface_avg_228
    46	ncep_GFS_sfc_DSWRF_surface_avg_240
    47	ncep_GFS_sfc_DSWRF_surface_avg_252
    48	ncep_GFS_sfc_DSWRF_surface_avg_264
    49	ncep_GFS_sfc_DSWRF_surface_avg_276
    50	ncep_GFS_sfc_DSWRF_surface_avg_288
    51	ncep_GFS_sfc_DSWRF_surface_avg_300
    52	ncep_GFS_sfc_DSWRF_surface_avg_312
    53	ncep_GFS_sfc_DSWRF_surface_avg_324
    54	ncep_GFS_sfc_DSWRF_surface_avg_336
    55	ncep_GFS_sfc_DSWRF_surface_avg_348
    56	ncep_GFS_sfc_DSWRF_surface_avg_360
    57	ncep_GFS_sfc_DSWRF_surface_avg_372

 For reference purposes, here's an exhaustive list of the column headers as of 2014-01-14:
 
siteGroup
siteName
lat
lon
valid_time
sr_zen
sr_global
sr_direct
sr_diffuse
sr_temp
sr_wspd
sr_rh
lat
lon
validTime
sat_ghi
clear_ghi
CosSolarZenithAngle
ncep_RAP_DSWRF_1
ncep_HRRR_DSWRF_1
ncep_HRRR_LCDC_1
ncep_HRRR_HCDC_1
ncep_HRRR_TCDC_1
ncep_HRRR_MCDC_1
persistence_1
ncep_NAM_hires_DSWRF_inst_1
ncep_NAM_hires_CSDSF_1
ncep_NAM_hires_LCDC_1
ncep_NAM_hires_MCDC_1
ncep_NAM_hires_HCDC_1
ncep_NAM_hires_TCDC_1
ncep_NAM_DSWRF_1
ncep_NAM_TCDC_1
ncep_NAM_flag_1
ncep_GFS_sfc_DSWRF_surface_avg_1
ncep_GFS_sfc_DSWRF_surface_inst_1
ncep_GFS_sfc_TCDC_high_1
ncep_GFS_sfc_TCDC_mid_1
ncep_GFS_sfc_TCDC_low_1
ncep_GFS_sfc_TCDC_total_1
ncep_GFS_DSWRF_1
ncep_GFS_TCDC_total_1
ncep_GFS_flag_1
NDFD_sky_1
NDFD_global_1
NDFD_flag_1
cm_1
ecmwf_ghi_1
ecmwf_flag_1
ecmwf_cloud_1
ncep_RAP_DSWRF_2
ncep_HRRR_DSWRF_2
ncep_HRRR_LCDC_2
ncep_HRRR_HCDC_2
ncep_HRRR_TCDC_2
ncep_HRRR_MCDC_2
persistence_2
ncep_NAM_hires_DSWRF_inst_2
ncep_NAM_hires_CSDSF_2
ncep_NAM_hires_LCDC_2
ncep_NAM_hires_MCDC_2
ncep_NAM_hires_HCDC_2
ncep_NAM_hires_TCDC_2
ncep_NAM_DSWRF_2
ncep_NAM_TCDC_2
ncep_NAM_flag_2
ncep_GFS_sfc_DSWRF_surface_avg_2
ncep_GFS_sfc_DSWRF_surface_inst_2
ncep_GFS_sfc_TCDC_high_2
ncep_GFS_sfc_TCDC_mid_2
ncep_GFS_sfc_TCDC_low_2
ncep_GFS_sfc_TCDC_total_2
ncep_GFS_DSWRF_2
ncep_GFS_TCDC_total_2
ncep_GFS_flag_2
NDFD_sky_2
NDFD_global_2
NDFD_flag_2
cm_2
ecmwf_ghi_2
ecmwf_flag_2
ecmwf_cloud_2
ncep_RAP_DSWRF_3
ncep_HRRR_DSWRF_3
ncep_HRRR_LCDC_3
ncep_HRRR_HCDC_3
ncep_HRRR_TCDC_3
ncep_HRRR_MCDC_3
persistence_3
ncep_NAM_hires_DSWRF_inst_3
ncep_NAM_hires_CSDSF_3
ncep_NAM_hires_LCDC_3
ncep_NAM_hires_MCDC_3
ncep_NAM_hires_HCDC_3
ncep_NAM_hires_TCDC_3
ncep_NAM_DSWRF_3
ncep_NAM_TCDC_3
ncep_NAM_flag_3
ncep_GFS_sfc_DSWRF_surface_avg_3
ncep_GFS_sfc_DSWRF_surface_inst_3
ncep_GFS_sfc_TCDC_high_3
ncep_GFS_sfc_TCDC_mid_3
ncep_GFS_sfc_TCDC_low_3
ncep_GFS_sfc_TCDC_total_3
ncep_GFS_DSWRF_3
ncep_GFS_TCDC_total_3
ncep_GFS_flag_3
NDFD_sky_3
NDFD_global_3
NDFD_flag_3
cm_3
ecmwf_ghi_3
ecmwf_flag_3
ecmwf_cloud_3
ncep_RAP_DSWRF_4
ncep_HRRR_DSWRF_4
ncep_HRRR_LCDC_4
ncep_HRRR_HCDC_4
ncep_HRRR_TCDC_4
ncep_HRRR_MCDC_4
persistence_4
ncep_NAM_hires_DSWRF_inst_4
ncep_NAM_hires_CSDSF_4
ncep_NAM_hires_LCDC_4
ncep_NAM_hires_MCDC_4
ncep_NAM_hires_HCDC_4
ncep_NAM_hires_TCDC_4
ncep_NAM_DSWRF_4
ncep_NAM_TCDC_4
ncep_NAM_flag_4
ncep_GFS_sfc_DSWRF_surface_avg_4
ncep_GFS_sfc_DSWRF_surface_inst_4
ncep_GFS_sfc_TCDC_high_4
ncep_GFS_sfc_TCDC_mid_4
ncep_GFS_sfc_TCDC_low_4
ncep_GFS_sfc_TCDC_total_4
ncep_GFS_DSWRF_4
ncep_GFS_TCDC_total_4
ncep_GFS_flag_4
NDFD_sky_4
NDFD_global_4
NDFD_flag_4
cm_4
ecmwf_ghi_4
ecmwf_flag_4
ecmwf_cloud_4
ncep_RAP_DSWRF_5
ncep_HRRR_DSWRF_5
ncep_HRRR_LCDC_5
ncep_HRRR_HCDC_5
ncep_HRRR_TCDC_5
ncep_HRRR_MCDC_5
persistence_5
ncep_NAM_hires_DSWRF_inst_5
ncep_NAM_hires_CSDSF_5
ncep_NAM_hires_LCDC_5
ncep_NAM_hires_MCDC_5
ncep_NAM_hires_HCDC_5
ncep_NAM_hires_TCDC_5
ncep_NAM_DSWRF_5
ncep_NAM_TCDC_5
ncep_NAM_flag_5
ncep_GFS_sfc_DSWRF_surface_avg_5
ncep_GFS_sfc_DSWRF_surface_inst_5
ncep_GFS_sfc_TCDC_high_5
ncep_GFS_sfc_TCDC_mid_5
ncep_GFS_sfc_TCDC_low_5
ncep_GFS_sfc_TCDC_total_5
ncep_GFS_DSWRF_5
ncep_GFS_TCDC_total_5
ncep_GFS_flag_5
NDFD_sky_5
NDFD_global_5
NDFD_flag_5
cm_5
ecmwf_ghi_5
ecmwf_flag_5
ecmwf_cloud_5
ncep_RAP_DSWRF_6
ncep_HRRR_DSWRF_6
ncep_HRRR_LCDC_6
ncep_HRRR_HCDC_6
ncep_HRRR_TCDC_6
ncep_HRRR_MCDC_6
persistence_6
ncep_NAM_hires_DSWRF_inst_6
ncep_NAM_hires_CSDSF_6
ncep_NAM_hires_LCDC_6
ncep_NAM_hires_MCDC_6
ncep_NAM_hires_HCDC_6
ncep_NAM_hires_TCDC_6
ncep_NAM_DSWRF_6
ncep_NAM_TCDC_6
ncep_NAM_flag_6
ncep_GFS_sfc_DSWRF_surface_avg_6
ncep_GFS_sfc_DSWRF_surface_inst_6
ncep_GFS_sfc_TCDC_high_6
ncep_GFS_sfc_TCDC_mid_6
ncep_GFS_sfc_TCDC_low_6
ncep_GFS_sfc_TCDC_total_6
ncep_GFS_DSWRF_6
ncep_GFS_TCDC_total_6
ncep_GFS_flag_6
NDFD_sky_6
NDFD_global_6
NDFD_flag_6
cm_6
ecmwf_ghi_6
ecmwf_flag_6
ecmwf_cloud_6
ncep_RAP_DSWRF_7
ncep_HRRR_DSWRF_7
ncep_HRRR_LCDC_7
ncep_HRRR_HCDC_7
ncep_HRRR_TCDC_7
ncep_HRRR_MCDC_7
persistence_7
ncep_NAM_hires_DSWRF_inst_7
ncep_NAM_hires_CSDSF_7
ncep_NAM_hires_LCDC_7
ncep_NAM_hires_MCDC_7
ncep_NAM_hires_HCDC_7
ncep_NAM_hires_TCDC_7
ncep_NAM_DSWRF_7
ncep_NAM_TCDC_7
ncep_NAM_flag_7
ncep_GFS_sfc_DSWRF_surface_avg_7
ncep_GFS_sfc_DSWRF_surface_inst_7
ncep_GFS_sfc_TCDC_high_7
ncep_GFS_sfc_TCDC_mid_7
ncep_GFS_sfc_TCDC_low_7
ncep_GFS_sfc_TCDC_total_7
ncep_GFS_DSWRF_7
ncep_GFS_TCDC_total_7
ncep_GFS_flag_7
NDFD_sky_7
NDFD_global_7
NDFD_flag_7
cm_7
ecmwf_ghi_7
ecmwf_flag_7
ecmwf_cloud_7
ncep_RAP_DSWRF_8
ncep_HRRR_DSWRF_8
ncep_HRRR_LCDC_8
ncep_HRRR_HCDC_8
ncep_HRRR_TCDC_8
ncep_HRRR_MCDC_8
persistence_8
ncep_NAM_hires_DSWRF_inst_8
ncep_NAM_hires_CSDSF_8
ncep_NAM_hires_LCDC_8
ncep_NAM_hires_MCDC_8
ncep_NAM_hires_HCDC_8
ncep_NAM_hires_TCDC_8
ncep_NAM_DSWRF_8
ncep_NAM_TCDC_8
ncep_NAM_flag_8
ncep_GFS_sfc_DSWRF_surface_avg_8
ncep_GFS_sfc_DSWRF_surface_inst_8
ncep_GFS_sfc_TCDC_high_8
ncep_GFS_sfc_TCDC_mid_8
ncep_GFS_sfc_TCDC_low_8
ncep_GFS_sfc_TCDC_total_8
ncep_GFS_DSWRF_8
ncep_GFS_TCDC_total_8
ncep_GFS_flag_8
NDFD_sky_8
NDFD_global_8
NDFD_flag_8
cm_8
ecmwf_ghi_8
ecmwf_flag_8
ecmwf_cloud_8
ncep_RAP_DSWRF_9
ncep_HRRR_DSWRF_9
ncep_HRRR_LCDC_9
ncep_HRRR_HCDC_9
ncep_HRRR_TCDC_9
ncep_HRRR_MCDC_9
persistence_9
ncep_NAM_hires_DSWRF_inst_9
ncep_NAM_hires_CSDSF_9
ncep_NAM_hires_LCDC_9
ncep_NAM_hires_MCDC_9
ncep_NAM_hires_HCDC_9
ncep_NAM_hires_TCDC_9
ncep_NAM_DSWRF_9
ncep_NAM_TCDC_9
ncep_NAM_flag_9
ncep_GFS_sfc_DSWRF_surface_avg_9
ncep_GFS_sfc_DSWRF_surface_inst_9
ncep_GFS_sfc_TCDC_high_9
ncep_GFS_sfc_TCDC_mid_9
ncep_GFS_sfc_TCDC_low_9
ncep_GFS_sfc_TCDC_total_9
ncep_GFS_DSWRF_9
ncep_GFS_TCDC_total_9
ncep_GFS_flag_9
NDFD_sky_9
NDFD_global_9
NDFD_flag_9
cm_9
ecmwf_ghi_9
ecmwf_flag_9
ecmwf_cloud_9
ncep_RAP_DSWRF_12
ncep_HRRR_DSWRF_12
ncep_HRRR_LCDC_12
ncep_HRRR_HCDC_12
ncep_HRRR_TCDC_12
ncep_HRRR_MCDC_12
persistence_12
ncep_NAM_hires_DSWRF_inst_12
ncep_NAM_hires_CSDSF_12
ncep_NAM_hires_LCDC_12
ncep_NAM_hires_MCDC_12
ncep_NAM_hires_HCDC_12
ncep_NAM_hires_TCDC_12
ncep_NAM_DSWRF_12
ncep_NAM_TCDC_12
ncep_NAM_flag_12
ncep_GFS_sfc_DSWRF_surface_avg_12
ncep_GFS_sfc_DSWRF_surface_inst_12
ncep_GFS_sfc_TCDC_high_12
ncep_GFS_sfc_TCDC_mid_12
ncep_GFS_sfc_TCDC_low_12
ncep_GFS_sfc_TCDC_total_12
ncep_GFS_DSWRF_12
ncep_GFS_TCDC_total_12
ncep_GFS_flag_12
NDFD_sky_12
NDFD_global_12
NDFD_flag_12
ecmwf_ghi_12
ecmwf_flag_12
ecmwf_cloud_12
ncep_RAP_DSWRF_15
ncep_HRRR_DSWRF_15
ncep_HRRR_LCDC_15
ncep_HRRR_HCDC_15
ncep_HRRR_TCDC_15
ncep_HRRR_MCDC_15
persistence_15
ncep_NAM_hires_DSWRF_inst_15
ncep_NAM_hires_CSDSF_15
ncep_NAM_hires_LCDC_15
ncep_NAM_hires_MCDC_15
ncep_NAM_hires_HCDC_15
ncep_NAM_hires_TCDC_15
ncep_NAM_DSWRF_15
ncep_NAM_TCDC_15
ncep_NAM_flag_15
ncep_GFS_sfc_DSWRF_surface_avg_15
ncep_GFS_sfc_DSWRF_surface_inst_15
ncep_GFS_sfc_TCDC_high_15
ncep_GFS_sfc_TCDC_mid_15
ncep_GFS_sfc_TCDC_low_15
ncep_GFS_sfc_TCDC_total_15
ncep_GFS_DSWRF_15
ncep_GFS_TCDC_total_15
ncep_GFS_flag_15
NDFD_sky_15
NDFD_global_15
NDFD_flag_15
ecmwf_ghi_15
ecmwf_flag_15
ecmwf_cloud_15
ncep_RAP_DSWRF_18
persistence_18
ncep_NAM_hires_DSWRF_inst_18
ncep_NAM_hires_CSDSF_18
ncep_NAM_hires_LCDC_18
ncep_NAM_hires_MCDC_18
ncep_NAM_hires_HCDC_18
ncep_NAM_hires_TCDC_18
ncep_NAM_DSWRF_18
ncep_NAM_TCDC_18
ncep_NAM_flag_18
ncep_GFS_sfc_DSWRF_surface_avg_18
ncep_GFS_sfc_DSWRF_surface_inst_18
ncep_GFS_sfc_TCDC_high_18
ncep_GFS_sfc_TCDC_mid_18
ncep_GFS_sfc_TCDC_low_18
ncep_GFS_sfc_TCDC_total_18
ncep_GFS_DSWRF_18
ncep_GFS_TCDC_total_18
ncep_GFS_flag_18
NDFD_sky_18
NDFD_global_18
NDFD_flag_18
ecmwf_ghi_18
ecmwf_flag_18
ecmwf_cloud_18
persistence_21
ncep_NAM_hires_DSWRF_inst_21
ncep_NAM_hires_CSDSF_21
ncep_NAM_hires_LCDC_21
ncep_NAM_hires_MCDC_21
ncep_NAM_hires_HCDC_21
ncep_NAM_hires_TCDC_21
ncep_NAM_DSWRF_21
ncep_NAM_TCDC_21
ncep_NAM_flag_21
ncep_GFS_sfc_DSWRF_surface_avg_21
ncep_GFS_sfc_DSWRF_surface_inst_21
ncep_GFS_sfc_TCDC_high_21
ncep_GFS_sfc_TCDC_mid_21
ncep_GFS_sfc_TCDC_low_21
ncep_GFS_sfc_TCDC_total_21
ncep_GFS_DSWRF_21
ncep_GFS_TCDC_total_21
ncep_GFS_flag_21
NDFD_sky_21
NDFD_global_21
NDFD_flag_21
ecmwf_ghi_21
ecmwf_flag_21
ecmwf_cloud_21
persistence_24
ncep_NAM_hires_DSWRF_inst_24
ncep_NAM_hires_CSDSF_24
ncep_NAM_hires_LCDC_24
ncep_NAM_hires_MCDC_24
ncep_NAM_hires_HCDC_24
ncep_NAM_hires_TCDC_24
ncep_NAM_DSWRF_24
ncep_NAM_TCDC_24
ncep_NAM_flag_24
ncep_GFS_sfc_DSWRF_surface_avg_24
ncep_GFS_sfc_DSWRF_surface_inst_24
ncep_GFS_sfc_TCDC_high_24
ncep_GFS_sfc_TCDC_mid_24
ncep_GFS_sfc_TCDC_low_24
ncep_GFS_sfc_TCDC_total_24
ncep_GFS_DSWRF_24
ncep_GFS_TCDC_total_24
ncep_GFS_flag_24
NDFD_sky_24
NDFD_global_24
NDFD_flag_24
ecmwf_ghi_24
ecmwf_flag_24
ecmwf_cloud_24
persistence_30
ncep_NAM_hires_DSWRF_inst_30
ncep_NAM_hires_CSDSF_30
ncep_NAM_hires_LCDC_30
ncep_NAM_hires_MCDC_30
ncep_NAM_hires_HCDC_30
ncep_NAM_hires_TCDC_30
ncep_NAM_DSWRF_30
ncep_NAM_TCDC_30
ncep_NAM_flag_30
ncep_GFS_sfc_DSWRF_surface_avg_30
ncep_GFS_sfc_DSWRF_surface_inst_30
ncep_GFS_sfc_TCDC_high_30
ncep_GFS_sfc_TCDC_mid_30
ncep_GFS_sfc_TCDC_low_30
ncep_GFS_sfc_TCDC_total_30
ncep_GFS_DSWRF_30
ncep_GFS_TCDC_total_30
ncep_GFS_flag_30
NDFD_sky_30
NDFD_global_30
NDFD_flag_30
ecmwf_ghi_30
ecmwf_flag_30
ecmwf_cloud_30
persistence_36
ncep_NAM_hires_DSWRF_inst_36
ncep_NAM_hires_CSDSF_36
ncep_NAM_hires_LCDC_36
ncep_NAM_hires_MCDC_36
ncep_NAM_hires_HCDC_36
ncep_NAM_hires_TCDC_36
ncep_NAM_DSWRF_36
ncep_NAM_TCDC_36
ncep_NAM_flag_36
ncep_GFS_sfc_DSWRF_surface_avg_36
ncep_GFS_sfc_DSWRF_surface_inst_36
ncep_GFS_sfc_TCDC_high_36
ncep_GFS_sfc_TCDC_mid_36
ncep_GFS_sfc_TCDC_low_36
ncep_GFS_sfc_TCDC_total_36
ncep_GFS_DSWRF_36
ncep_GFS_TCDC_total_36
ncep_GFS_flag_36
NDFD_sky_36
NDFD_global_36
NDFD_flag_36
ecmwf_ghi_36
ecmwf_flag_36
ecmwf_cloud_36
persistence_42
ncep_NAM_hires_DSWRF_inst_42
ncep_NAM_hires_CSDSF_42
ncep_NAM_hires_LCDC_42
ncep_NAM_hires_MCDC_42
ncep_NAM_hires_HCDC_42
ncep_NAM_hires_TCDC_42
ncep_NAM_DSWRF_42
ncep_NAM_TCDC_42
ncep_NAM_flag_42
ncep_GFS_sfc_DSWRF_surface_avg_42
ncep_GFS_sfc_DSWRF_surface_inst_42
ncep_GFS_sfc_TCDC_high_42
ncep_GFS_sfc_TCDC_mid_42
ncep_GFS_sfc_TCDC_low_42
ncep_GFS_sfc_TCDC_total_42
ncep_GFS_DSWRF_42
ncep_GFS_TCDC_total_42
ncep_GFS_flag_42
NDFD_sky_42
NDFD_global_42
NDFD_flag_42
ecmwf_ghi_42
ecmwf_flag_42
ecmwf_cloud_42
persistence_48
ncep_NAM_hires_DSWRF_inst_48
ncep_NAM_hires_CSDSF_48
ncep_NAM_hires_LCDC_48
ncep_NAM_hires_MCDC_48
ncep_NAM_hires_HCDC_48
ncep_NAM_hires_TCDC_48
ncep_NAM_DSWRF_48
ncep_NAM_TCDC_48
ncep_NAM_flag_48
ncep_GFS_sfc_DSWRF_surface_avg_48
ncep_GFS_sfc_DSWRF_surface_inst_48
ncep_GFS_sfc_TCDC_high_48
ncep_GFS_sfc_TCDC_mid_48
ncep_GFS_sfc_TCDC_low_48
ncep_GFS_sfc_TCDC_total_48
ncep_GFS_DSWRF_48
ncep_GFS_TCDC_total_48
ncep_GFS_flag_48
NDFD_sky_48
NDFD_global_48
NDFD_flag_48
ecmwf_ghi_48
ecmwf_flag_48
ecmwf_cloud_48
persistence_54
ncep_NAM_hires_DSWRF_inst_54
ncep_NAM_hires_CSDSF_54
ncep_NAM_hires_LCDC_54
ncep_NAM_hires_MCDC_54
ncep_NAM_hires_HCDC_54
ncep_NAM_hires_TCDC_54
ncep_NAM_DSWRF_54
ncep_NAM_TCDC_54
ncep_NAM_flag_54
ncep_GFS_sfc_DSWRF_surface_avg_54
ncep_GFS_sfc_DSWRF_surface_inst_54
ncep_GFS_sfc_TCDC_high_54
ncep_GFS_sfc_TCDC_mid_54
ncep_GFS_sfc_TCDC_low_54
ncep_GFS_sfc_TCDC_total_54
ncep_GFS_DSWRF_54
ncep_GFS_TCDC_total_54
ncep_GFS_flag_54
NDFD_sky_54
NDFD_global_54
NDFD_flag_54
ecmwf_ghi_54
ecmwf_flag_54
ecmwf_cloud_54
persistence_60
ncep_NAM_hires_DSWRF_inst_60
ncep_NAM_hires_CSDSF_60
ncep_NAM_hires_LCDC_60
ncep_NAM_hires_MCDC_60
ncep_NAM_hires_HCDC_60
ncep_NAM_hires_TCDC_60
ncep_NAM_DSWRF_60
ncep_NAM_TCDC_60
ncep_NAM_flag_60
ncep_GFS_sfc_DSWRF_surface_avg_60
ncep_GFS_sfc_DSWRF_surface_inst_60
ncep_GFS_sfc_TCDC_high_60
ncep_GFS_sfc_TCDC_mid_60
ncep_GFS_sfc_TCDC_low_60
ncep_GFS_sfc_TCDC_total_60
ncep_GFS_DSWRF_60
ncep_GFS_TCDC_total_60
ncep_GFS_flag_60
NDFD_sky_60
NDFD_global_60
NDFD_flag_60
ecmwf_ghi_60
ecmwf_flag_60
ecmwf_cloud_60
persistence_66
ncep_NAM_DSWRF_66
ncep_NAM_TCDC_66
ncep_NAM_flag_66
ncep_GFS_sfc_DSWRF_surface_avg_66
ncep_GFS_sfc_DSWRF_surface_inst_66
ncep_GFS_sfc_TCDC_high_66
ncep_GFS_sfc_TCDC_mid_66
ncep_GFS_sfc_TCDC_low_66
ncep_GFS_sfc_TCDC_total_66
ncep_GFS_DSWRF_66
ncep_GFS_TCDC_total_66
ncep_GFS_flag_66
NDFD_sky_66
NDFD_global_66
NDFD_flag_66
ecmwf_ghi_66
ecmwf_flag_66
ecmwf_cloud_66
persistence_72
ncep_NAM_DSWRF_72
ncep_NAM_TCDC_72
ncep_NAM_flag_72
ncep_GFS_sfc_DSWRF_surface_avg_72
ncep_GFS_sfc_DSWRF_surface_inst_72
ncep_GFS_sfc_TCDC_high_72
ncep_GFS_sfc_TCDC_mid_72
ncep_GFS_sfc_TCDC_low_72
ncep_GFS_sfc_TCDC_total_72
ncep_GFS_DSWRF_72
ncep_GFS_TCDC_total_72
ncep_GFS_flag_72
NDFD_sky_72
NDFD_global_72
NDFD_flag_72
ecmwf_ghi_72
ecmwf_flag_72
ecmwf_cloud_72
persistence_78
ncep_NAM_DSWRF_78
ncep_NAM_TCDC_78
ncep_NAM_flag_78
ncep_GFS_sfc_DSWRF_surface_avg_78
ncep_GFS_sfc_DSWRF_surface_inst_78
ncep_GFS_sfc_TCDC_high_78
ncep_GFS_sfc_TCDC_mid_78
ncep_GFS_sfc_TCDC_low_78
ncep_GFS_sfc_TCDC_total_78
ncep_GFS_DSWRF_78
ncep_GFS_TCDC_total_78
ncep_GFS_flag_78
NDFD_sky_78
NDFD_global_78
NDFD_flag_78
ecmwf_ghi_78
ecmwf_flag_78
ecmwf_cloud_78
persistence_84
ncep_NAM_DSWRF_84
ncep_NAM_TCDC_84
ncep_NAM_flag_84
ncep_GFS_sfc_DSWRF_surface_avg_84
ncep_GFS_sfc_DSWRF_surface_inst_84
ncep_GFS_sfc_TCDC_high_84
ncep_GFS_sfc_TCDC_mid_84
ncep_GFS_sfc_TCDC_low_84
ncep_GFS_sfc_TCDC_total_84
ncep_GFS_DSWRF_84
ncep_GFS_TCDC_total_84
ncep_GFS_flag_84
NDFD_sky_84
NDFD_global_84
NDFD_flag_84
ecmwf_ghi_84
ecmwf_flag_84
ecmwf_cloud_84
persistence_90
ncep_GFS_sfc_DSWRF_surface_avg_90
ncep_GFS_sfc_DSWRF_surface_inst_90
ncep_GFS_sfc_TCDC_high_90
ncep_GFS_sfc_TCDC_mid_90
ncep_GFS_sfc_TCDC_low_90
ncep_GFS_sfc_TCDC_total_90
ncep_GFS_DSWRF_90
ncep_GFS_TCDC_total_90
ncep_GFS_flag_90
NDFD_sky_90
NDFD_global_90
NDFD_flag_90
ecmwf_ghi_90
ecmwf_flag_90
ecmwf_cloud_90
persistence_96
ncep_GFS_sfc_DSWRF_surface_avg_96
ncep_GFS_sfc_DSWRF_surface_inst_96
ncep_GFS_sfc_TCDC_high_96
ncep_GFS_sfc_TCDC_mid_96
ncep_GFS_sfc_TCDC_low_96
ncep_GFS_sfc_TCDC_total_96
ncep_GFS_DSWRF_96
ncep_GFS_TCDC_total_96
ncep_GFS_flag_96
NDFD_sky_96
NDFD_global_96
NDFD_flag_96
ecmwf_ghi_96
ecmwf_flag_96
ecmwf_cloud_96
persistence_102
ncep_GFS_sfc_DSWRF_surface_avg_102
ncep_GFS_sfc_DSWRF_surface_inst_102
ncep_GFS_sfc_TCDC_high_102
ncep_GFS_sfc_TCDC_mid_102
ncep_GFS_sfc_TCDC_low_102
ncep_GFS_sfc_TCDC_total_102
ncep_GFS_DSWRF_102
ncep_GFS_TCDC_total_102
ncep_GFS_flag_102
NDFD_sky_102
NDFD_global_102
NDFD_flag_102
ecmwf_ghi_102
ecmwf_flag_102
ecmwf_cloud_102
persistence_108
ncep_GFS_sfc_DSWRF_surface_avg_108
ncep_GFS_sfc_DSWRF_surface_inst_108
ncep_GFS_sfc_TCDC_high_108
ncep_GFS_sfc_TCDC_mid_108
ncep_GFS_sfc_TCDC_low_108
ncep_GFS_sfc_TCDC_total_108
ncep_GFS_DSWRF_108
ncep_GFS_TCDC_total_108
ncep_GFS_flag_108
NDFD_sky_108
NDFD_global_108
NDFD_flag_108
ecmwf_ghi_108
ecmwf_flag_108
ecmwf_cloud_108
persistence_114
ncep_GFS_sfc_DSWRF_surface_avg_114
ncep_GFS_sfc_DSWRF_surface_inst_114
ncep_GFS_sfc_TCDC_high_114
ncep_GFS_sfc_TCDC_mid_114
ncep_GFS_sfc_TCDC_low_114
ncep_GFS_sfc_TCDC_total_114
ncep_GFS_DSWRF_114
ncep_GFS_TCDC_total_114
ncep_GFS_flag_114
NDFD_sky_114
NDFD_global_114
NDFD_flag_114
ecmwf_ghi_114
ecmwf_flag_114
ecmwf_cloud_114
persistence_120
ncep_GFS_sfc_DSWRF_surface_avg_120
ncep_GFS_sfc_DSWRF_surface_inst_120
ncep_GFS_sfc_TCDC_high_120
ncep_GFS_sfc_TCDC_mid_120
ncep_GFS_sfc_TCDC_low_120
ncep_GFS_sfc_TCDC_total_120
ncep_GFS_DSWRF_120
ncep_GFS_TCDC_total_120
ncep_GFS_flag_120
NDFD_sky_120
NDFD_global_120
NDFD_flag_120
ecmwf_ghi_120
ecmwf_flag_120
ecmwf_cloud_120
persistence_126
ncep_GFS_sfc_DSWRF_surface_avg_126
ncep_GFS_sfc_DSWRF_surface_inst_126
ncep_GFS_sfc_TCDC_high_126
ncep_GFS_sfc_TCDC_mid_126
ncep_GFS_sfc_TCDC_low_126
ncep_GFS_sfc_TCDC_total_126
ncep_GFS_DSWRF_126
ncep_GFS_TCDC_total_126
ncep_GFS_flag_126
NDFD_sky_126
NDFD_global_126
NDFD_flag_126
ecmwf_ghi_126
ecmwf_flag_126
ecmwf_cloud_126
persistence_132
ncep_GFS_sfc_DSWRF_surface_avg_132
ncep_GFS_sfc_DSWRF_surface_inst_132
ncep_GFS_sfc_TCDC_high_132
ncep_GFS_sfc_TCDC_mid_132
ncep_GFS_sfc_TCDC_low_132
ncep_GFS_sfc_TCDC_total_132
ncep_GFS_DSWRF_132
ncep_GFS_TCDC_total_132
ncep_GFS_flag_132
NDFD_sky_132
NDFD_global_132
NDFD_flag_132
ecmwf_ghi_132
ecmwf_flag_132
ecmwf_cloud_132
persistence_138
ncep_GFS_sfc_DSWRF_surface_avg_138
ncep_GFS_sfc_DSWRF_surface_inst_138
ncep_GFS_sfc_TCDC_high_138
ncep_GFS_sfc_TCDC_mid_138
ncep_GFS_sfc_TCDC_low_138
ncep_GFS_sfc_TCDC_total_138
ncep_GFS_DSWRF_138
ncep_GFS_TCDC_total_138
ncep_GFS_flag_138
NDFD_sky_138
NDFD_global_138
NDFD_flag_138
ecmwf_ghi_138
ecmwf_flag_138
ecmwf_cloud_138
persistence_144
ncep_GFS_sfc_DSWRF_surface_avg_144
ncep_GFS_sfc_DSWRF_surface_inst_144
ncep_GFS_sfc_TCDC_high_144
ncep_GFS_sfc_TCDC_mid_144
ncep_GFS_sfc_TCDC_low_144
ncep_GFS_sfc_TCDC_total_144
ncep_GFS_DSWRF_144
ncep_GFS_TCDC_total_144
ncep_GFS_flag_144
NDFD_sky_144
NDFD_global_144
NDFD_flag_144
ecmwf_ghi_144
ecmwf_flag_144
ecmwf_cloud_144
persistence_150
ncep_GFS_sfc_DSWRF_surface_avg_150
ncep_GFS_sfc_DSWRF_surface_inst_150
ncep_GFS_sfc_TCDC_high_150
ncep_GFS_sfc_TCDC_mid_150
ncep_GFS_sfc_TCDC_low_150
ncep_GFS_sfc_TCDC_total_150
ncep_GFS_DSWRF_150
ncep_GFS_TCDC_total_150
ncep_GFS_flag_150
NDFD_sky_150
NDFD_global_150
NDFD_flag_150
ecmwf_ghi_150
ecmwf_flag_150
ecmwf_cloud_150
persistence_156
ncep_GFS_sfc_DSWRF_surface_avg_156
ncep_GFS_sfc_DSWRF_surface_inst_156
ncep_GFS_sfc_TCDC_high_156
ncep_GFS_sfc_TCDC_mid_156
ncep_GFS_sfc_TCDC_low_156
ncep_GFS_sfc_TCDC_total_156
ncep_GFS_DSWRF_156
ncep_GFS_TCDC_total_156
ncep_GFS_flag_156
NDFD_sky_156
NDFD_global_156
NDFD_flag_156
ecmwf_ghi_156
ecmwf_flag_156
ecmwf_cloud_156
persistence_162
ncep_GFS_sfc_DSWRF_surface_avg_162
ncep_GFS_sfc_DSWRF_surface_inst_162
ncep_GFS_sfc_TCDC_high_162
ncep_GFS_sfc_TCDC_mid_162
ncep_GFS_sfc_TCDC_low_162
ncep_GFS_sfc_TCDC_total_162
ncep_GFS_DSWRF_162
ncep_GFS_TCDC_total_162
ncep_GFS_flag_162
NDFD_sky_162
NDFD_global_162
NDFD_flag_162
ecmwf_ghi_162
ecmwf_flag_162
ecmwf_cloud_162
persistence_168
ncep_GFS_sfc_DSWRF_surface_avg_168
ncep_GFS_sfc_DSWRF_surface_inst_168
ncep_GFS_sfc_TCDC_high_168
ncep_GFS_sfc_TCDC_mid_168
ncep_GFS_sfc_TCDC_low_168
ncep_GFS_sfc_TCDC_total_168
ncep_GFS_DSWRF_168
ncep_GFS_TCDC_total_168
ncep_GFS_flag_168
NDFD_sky_168
NDFD_global_168
NDFD_flag_168
ecmwf_ghi_168
ecmwf_flag_168
ecmwf_cloud_168
persistence_174
ncep_GFS_sfc_DSWRF_surface_avg_174
ncep_GFS_sfc_DSWRF_surface_inst_174
ncep_GFS_sfc_TCDC_high_174
ncep_GFS_sfc_TCDC_mid_174
ncep_GFS_sfc_TCDC_low_174
ncep_GFS_sfc_TCDC_total_174
ncep_GFS_DSWRF_174
ncep_GFS_TCDC_total_174
ncep_GFS_flag_174
ecmwf_ghi_174
ecmwf_flag_174
ecmwf_cloud_174
persistence_180
ncep_GFS_sfc_DSWRF_surface_avg_180
ncep_GFS_sfc_DSWRF_surface_inst_180
ncep_GFS_sfc_TCDC_high_180
ncep_GFS_sfc_TCDC_mid_180
ncep_GFS_sfc_TCDC_low_180
ncep_GFS_sfc_TCDC_total_180
ncep_GFS_DSWRF_180
ncep_GFS_TCDC_total_180
ncep_GFS_flag_180
ecmwf_ghi_180
ecmwf_flag_180
ecmwf_cloud_180
persistence_186
ncep_GFS_sfc_DSWRF_surface_avg_186
ncep_GFS_sfc_DSWRF_surface_inst_186
ncep_GFS_sfc_TCDC_high_186
ncep_GFS_sfc_TCDC_mid_186
ncep_GFS_sfc_TCDC_low_186
ncep_GFS_sfc_TCDC_total_186
ncep_GFS_DSWRF_186
ncep_GFS_TCDC_total_186
ncep_GFS_flag_186
ecmwf_ghi_186
ecmwf_flag_186
ecmwf_cloud_186
persistence_192
ncep_GFS_sfc_DSWRF_surface_avg_192
ncep_GFS_sfc_DSWRF_surface_inst_192
ncep_GFS_sfc_TCDC_high_192
ncep_GFS_sfc_TCDC_mid_192
ncep_GFS_sfc_TCDC_low_192
ncep_GFS_sfc_TCDC_total_192
ncep_GFS_DSWRF_192
ncep_GFS_TCDC_total_192
ncep_GFS_flag_192
ecmwf_ghi_192
ecmwf_flag_192
ecmwf_cloud_192
persistence_204
ncep_GFS_sfc_DSWRF_surface_avg_204
ncep_GFS_sfc_DSWRF_surface_inst_204
ncep_GFS_sfc_TCDC_high_204
ncep_GFS_sfc_TCDC_mid_204
ncep_GFS_sfc_TCDC_low_204
ncep_GFS_sfc_TCDC_total_204
ncep_GFS_DSWRF_204
ncep_GFS_TCDC_total_204
ncep_GFS_flag_204
ecmwf_ghi_204
ecmwf_flag_204
ecmwf_cloud_204
persistence_216
ncep_GFS_sfc_DSWRF_surface_avg_216
ncep_GFS_sfc_DSWRF_surface_inst_216
ncep_GFS_sfc_TCDC_high_216
ncep_GFS_sfc_TCDC_mid_216
ncep_GFS_sfc_TCDC_low_216
ncep_GFS_sfc_TCDC_total_216
ncep_GFS_DSWRF_216
ncep_GFS_TCDC_total_216
ncep_GFS_flag_216
ecmwf_ghi_216
ecmwf_flag_216
ecmwf_cloud_216
persistence_228
ncep_GFS_sfc_DSWRF_surface_avg_228
ncep_GFS_sfc_DSWRF_surface_inst_228
ncep_GFS_sfc_TCDC_high_228
ncep_GFS_sfc_TCDC_mid_228
ncep_GFS_sfc_TCDC_low_228
ncep_GFS_sfc_TCDC_total_228
ncep_GFS_DSWRF_228
ncep_GFS_TCDC_total_228
ncep_GFS_flag_228
ecmwf_ghi_228
ecmwf_flag_228
ecmwf_cloud_228
persistence_240
ncep_GFS_sfc_DSWRF_surface_avg_240
ncep_GFS_sfc_DSWRF_surface_inst_240
ncep_GFS_sfc_TCDC_high_240
ncep_GFS_sfc_TCDC_mid_240
ncep_GFS_sfc_TCDC_low_240
ncep_GFS_sfc_TCDC_total_240
ncep_GFS_DSWRF_240
ncep_GFS_TCDC_total_240
ncep_GFS_flag_240
ecmwf_ghi_240
ecmwf_flag_240
ecmwf_cloud_240
persistence_252
ncep_GFS_sfc_DSWRF_surface_avg_252
ncep_GFS_sfc_DSWRF_surface_inst_252
ncep_GFS_sfc_TCDC_high_252
ncep_GFS_sfc_TCDC_mid_252
ncep_GFS_sfc_TCDC_low_252
ncep_GFS_sfc_TCDC_total_252
persistence_264
ncep_GFS_sfc_DSWRF_surface_avg_264
ncep_GFS_sfc_DSWRF_surface_inst_264
ncep_GFS_sfc_TCDC_high_264
ncep_GFS_sfc_TCDC_mid_264
ncep_GFS_sfc_TCDC_low_264
ncep_GFS_sfc_TCDC_total_264
persistence_276
ncep_GFS_sfc_DSWRF_surface_avg_276
ncep_GFS_sfc_DSWRF_surface_inst_276
ncep_GFS_sfc_TCDC_high_276
ncep_GFS_sfc_TCDC_mid_276
ncep_GFS_sfc_TCDC_low_276
ncep_GFS_sfc_TCDC_total_276
persistence_288
ncep_GFS_sfc_DSWRF_surface_avg_288
ncep_GFS_sfc_DSWRF_surface_inst_288
ncep_GFS_sfc_TCDC_high_288
ncep_GFS_sfc_TCDC_mid_288
ncep_GFS_sfc_TCDC_low_288
ncep_GFS_sfc_TCDC_total_288
persistence_300
ncep_GFS_sfc_DSWRF_surface_avg_300
ncep_GFS_sfc_DSWRF_surface_inst_300
ncep_GFS_sfc_TCDC_high_300
ncep_GFS_sfc_TCDC_mid_300
ncep_GFS_sfc_TCDC_low_300
ncep_GFS_sfc_TCDC_total_300
persistence_312
ncep_GFS_sfc_DSWRF_surface_avg_312
ncep_GFS_sfc_DSWRF_surface_inst_312
ncep_GFS_sfc_TCDC_high_312
ncep_GFS_sfc_TCDC_mid_312
ncep_GFS_sfc_TCDC_low_312
ncep_GFS_sfc_TCDC_total_312
persistence_324
ncep_GFS_sfc_DSWRF_surface_avg_324
ncep_GFS_sfc_DSWRF_surface_inst_324
ncep_GFS_sfc_TCDC_high_324
ncep_GFS_sfc_TCDC_mid_324
ncep_GFS_sfc_TCDC_low_324
ncep_GFS_sfc_TCDC_total_324
persistence_336
ncep_GFS_sfc_DSWRF_surface_avg_336
ncep_GFS_sfc_DSWRF_surface_inst_336
ncep_GFS_sfc_TCDC_high_336
ncep_GFS_sfc_TCDC_mid_336
ncep_GFS_sfc_TCDC_low_336
ncep_GFS_sfc_TCDC_total_336
persistence_348
ncep_GFS_sfc_DSWRF_surface_avg_348
ncep_GFS_sfc_DSWRF_surface_inst_348
ncep_GFS_sfc_TCDC_high_348
ncep_GFS_sfc_TCDC_mid_348
ncep_GFS_sfc_TCDC_low_348
ncep_GFS_sfc_TCDC_total_348
persistence_360
ncep_GFS_sfc_DSWRF_surface_avg_360
ncep_GFS_sfc_DSWRF_surface_inst_360
ncep_GFS_sfc_TCDC_high_360
ncep_GFS_sfc_TCDC_mid_360
ncep_GFS_sfc_TCDC_low_360
ncep_GFS_sfc_TCDC_total_360
persistence_372
ncep_GFS_sfc_DSWRF_surface_avg_372
ncep_GFS_sfc_DSWRF_surface_inst_372
ncep_GFS_sfc_TCDC_high_372
ncep_GFS_sfc_TCDC_mid_372
ncep_GFS_sfc_TCDC_low_372
ncep_GFS_sfc_TCDC_total_372
 */
