/* 
 Forecast Optimizer.
 */

/* todo
 1) figure out how to exclude a model from some sites
 2) get model to run on all sites and produce one set of weights based on that
 */
#include "forecastOpt.h"

#define VERSION "1.0"

#define MIN_IRR -25
#define MAX_IRR 1500
    
void help(void);
void version(void);
void processForecast(forecastInputType *fci, char *fileName);
void initForecastInfo(forecastInputType *fci);
void incrementTimeSeries(forecastInputType *fci);
int  readForecastFile(forecastInputType *fci, char *fileName);
int  readDataFromLine(forecastInputType *fci, char *fields[]);
int  parseDateTime(forecastInputType *fci, dateTimeType *dt, char *dateTimeStr);
int  parseHourIndexes(forecastInputType *fci, char *optarg);
void initInputColumns(forecastInputType *fci);
void parseColumnInfo(forecastInputType *fci, char *colNamesLine);
int  parseArgs(forecastInputType *fci, int argC, char **argV);
void registerColumnInfo(forecastInputType *fci, char *columnName, char *columnDescription, int maxHourAhead, int hourIndex, int isReference);
void runErrorAnalysis(forecastInputType *fci);
void getNumberOfHoursAhead(forecastInputType *fci, char *origLine);
void printByHour(forecastInputType *fci);
void printByModel(forecastInputType *fci);
void printByAnalysisType(forecastInputType *fci);
char *getColumnNameByHourModel(forecastInputType *fci, int hrInd, int modInd);
FILE *openErrorTypeFile(forecastInputType *fci, char *analysisType);
void printRmseTableHour(forecastInputType *fci, int hourIndex);
FILE *openErrorTypeFileHourly(forecastInputType *fci, char *analysisType, int hourIndex);
void initSiteInfo(forecastInputType *fci);
void registerSiteName(siteType *si, char *siteName);
void registerSiteModel(siteType *si, char *modelName, int maxHoursAhead);
void setSite(forecastInputType *fci);
int  checkModelAgainstSite(forecastInputType *fci, char *modelName);
void setSiteInfo(forecastInputType *fci, char *line);

 
char *Progname, *OutputDirectory;
FILE *FilteredDataFp;
char *fileName;
char ErrStr[4096];
int  allocatedSamples, LineNumber, HashLineNumber;
char *Delimiter, MultipleSites;

int main(int argc,char **argv)
{
    forecastInputType fci;
    //signal(SIGSEGV, segvhandler); // catch memory reference errors

    Progname = basename(argv[0]);  // strip out path
  
    if (!parseArgs(&fci, argc, argv)) {
       exit(1);
    }
 
    processForecast(&fci, fileName);
    
    return(EXIT_SUCCESS);
}
  
int parseArgs(forecastInputType *fci, int argC, char **argV)
{
    int c;
    extern char *optarg;
    extern int optind;
    HashLineNumber = 1;
    Delimiter = "\t";
    OutputDirectory = NULL;
    int parseDates(forecastInputType *fci, char *optarg);
    fci->startDate.year = -1;
    fci->endDate.year = -1;
    fci->outputDirectory = "./";
    fci->verbose = False;
    fci->multipleSites = False;
    fci->weightSumLowCutoff = 0.95;
    fci->weightSumHighCutoff = 1.05;
    fci->startHourLowIndex = -1;
    fci->startHourHighIndex = -1;
    
    //static char tabDel[32];
    //sprintf(tabDel, "%c", 9);  // ascii 9 = TAB
    
    while ((c=getopt(argC, argV, "a:cto:s:HhvVr:m")) != EOF) {
        switch (c) {
            case 'c': { Delimiter = ",";
                        break; }
            case 'a': { if(!parseDates(fci, optarg)) {
                        fprintf(stderr, "Failed to process archive dates\n");
                        return False;
                        }
                        break; }
            case 'o': { fci->outputDirectory = strdup(optarg);
                        break; }
            case 's': { HashLineNumber = atoi(optarg);
                        break; }
            case 't': { Delimiter = "\t";
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
            default:  return False;         
        }       
    }  

    if(optind < argC) {  // assume any left over args are fileNames
      fileName = argV[optind];
    }
    else {
        help();
        return False;
    }
    
    return True;
} 

void help(void) 
{
    version();
    printf( "usage: %s [-csmhvV] [-r beginHourIndex,endHourIndex] [-a begin,end] [-o outputDir] forecastFile\n", Progname);
    printf( "where: -c = comma separated input [TAB]\n");
    printf( "       -s hashLineNumber = specify which input file line number has the column defs[1]\n");
    printf( "       -r beginHourIndex,endHourIndex = specify which hour ahead indexes to start and end with\n");
    printf( "       -m = input data file contains multiple sites (concatenated)\n");
    printf( "       --o outputDir = specify where output files go\n");
    printf( "       -v = be versbose\n");
    printf( "       -h = help\n");
    printf( "       forecastFile = .csv forecast file\n");
}

void version(void)
{
  fprintf(stderr,"%s %s built [%s %s]\n", Progname, VERSION, __TIME__, __DATE__);
  return;
} 



void processForecast(forecastInputType *fci, char *fileName)
{        
    time_t start;
    
    start = time(NULL);
    fprintf(stderr, "=== Starting processing at %s\n", timeOfDayStr());
    fprintf(stderr, "=== Using date range: %s to ", dtToStringDateTime(&fci->startDate));
    fprintf(stderr, "%s\n", dtToStringDateTime(&fci->endDate));
    fprintf(stderr, "=== Weight Sum Range: %.2f to %.2f\n", fci->weightSumLowCutoff, fci->weightSumHighCutoff);

    initForecastInfo(fci);
    initSiteInfo(fci);    
    readForecastFile(fci, fileName);
    
    fprintf(stderr, "=== Hours Ahead: %d to %d\n\n", fci->hourErrorGroup[fci->startHourLowIndex].hoursAhead, fci->hourErrorGroup[fci->startHourHighIndex].hoursAhead);
    fprintf(stderr, "=== Number of    input records: %d\n", fci->numInputRecords);
    fprintf(stderr, "=== Number of daylight records: %d\n", fci->numDaylightRecords);
                
    runErrorAnalysis(fci);
     
    fprintf(stderr, "=== Ending at %s\n", timeOfDayStr());
    fprintf(stderr, "=== Elapsed time: %s\n", getElapsedTime(start));
    return;
}

#define LineLength 1024 * 32
#define MAX_FIELDS 2048

int readForecastFile(forecastInputType *fci, char *fileName)
{
    FILE *fp;
    char line[LineLength], *headerLine;
    int numFields;
    double lat, lon;
    char *fields[MAX_FIELDS], *fldPtr;
    dateTimeType currDate;    
    timeSeriesType *thisSample;
    char firstLine=True;
    
    if((fp = fopen(fileName, "r")) == NULL) {
        sprintf(ErrStr, "Couldn't open input file %s.", fileName);
        FatalError("readForecastFile()", ErrStr);
    }
    
    /*
    0 siteGroup	
    1 siteName	
    2 lat	
    3 lon	
    4 valid_time	
    5 sr_zen	
    6 sr_global	
    7 sr_direct	
    8 sr_diffuse	
    9 sr_temp	
    10 sr_wspd	
    11 sr_rh	
    12 lat	     <-- what should we do about these 3?
    13 lon	
    14 validTime	
    15 sat_ghi	
    16 clear_ghi	
    17 CosSolarZenithAngle	
    18 ncep_RAP_DSWRF_1 ...
    */

    
    while(fgets(line, LineLength, fp)) {
        LineNumber++;
        if(LineNumber <= HashLineNumber) {
            if(LineNumber == HashLineNumber) {
                headerLine = strdup(line);  // save for later -- like, the next line of input
            }
            continue;
        }        

        else if(firstLine) {
            firstLine = False;
            setSiteInfo(fci, line);
            parseColumnInfo(fci, headerLine);
        }
        
        numFields = split(line, fields, MAX_FIELDS, Delimiter);  /* split line */  
        if(numFields < 100) {
            sprintf(ErrStr, "Scanning line %d of file %s, got %d columns using delimiter \"%s\".  Either delimiter flag or -s arg is wrong", LineNumber, fileName, numFields, Delimiter);
            FatalError("readForecastFile()", ErrStr);
        }
                
        // date & time
        if(!parseDateTime(fci, &currDate, fields[4]))
            continue;
        
        incrementTimeSeries(fci);
        thisSample = &(fci->timeSeries[fci->numTotalSamples - 1]);  // switched these two lines
        thisSample->dateTime = currDate;

        // siteGroup
        fldPtr = fields[0];
        if(fci->siteGroup == NULL) {
            fci->siteGroup = strdup(fldPtr);
        }
        else {
            if(!fci->multipleSites && strcasecmp(fldPtr, fci->siteGroup) != 0) {
                fprintf(fci->warningsFp, "Warning: siteGroup changed from %s to %s\n", fci->siteGroup, fldPtr);
            }
        }
        
        // siteName
        fldPtr = fields[1];

        if(!fci->multipleSites && strcasecmp(fldPtr, fci->siteName) != 0) {
            fprintf(fci->warningsFp, "Warning: siteName changed from %s to %s\n", fci->siteName, fldPtr);
        }
                
        // lat & lon
        lat = atof(fields[2]);
        lon = atof(fields[3]);

        if(fci->lat == -999 || fci->lon == -999) {
            fci->lat = lat;
            fci->lon = lon;
        }
        else {
            if(!fci->multipleSites && fabs(lat - fci->lat) > 0.01) {
                fprintf(fci->warningsFp, "Warning: latitude changed from %.3f to %.3f\n", fci->lat, lat);
            }
            if(!fci->multipleSites && fabs(lon - fci->lon) > 0.01) {
                fprintf(fci->warningsFp, "Warning: longitude changed from %.3f to %.3f\n", fci->lon, lon);
            }        
        }
        
        (void) readDataFromLine(fci, fields);
        
/*
        if(fci->numValidSamples == 3)
            return True;
*/

        fci->numInputRecords++;
    }
    
    return True;
}

void setSiteInfo(forecastInputType *fci, char *line)
{
    char *tempLine = strdup(line);
    int numFields;
    char *fields[MAX_FIELDS];
    
    // now grab siteName and set the site specific model info
    numFields = split(tempLine, fields, MAX_FIELDS, Delimiter);  /* split line */
    fci->siteName = strdup(fields[1]);
    setSite(fci);
    
    // Now that we know the site name we can open the warnings file
    sprintf(fci->warningsFileName, "%s/%s.warnings.txt", fci->outputDirectory, fci->siteName);
    if((fci->warningsFp = fopen(fci->warningsFileName, "w")) == NULL) {
        fprintf(stderr, "Couldn't open warnings file %s\n", fci->warningsFileName);
        exit(1);
    }


    free(tempLine);
}

// this assumes fci->siteName has already been set by the input file parser
void setSite(forecastInputType *fci)
{
    int siteNum;
    
    for(siteNum=0; siteNum<fci->numSites; siteNum++) {
        if(strcmp(fci->siteName, fci->allSiteInfo[siteNum].siteName) == 0) {
            fci->thisSite = &fci->allSiteInfo[siteNum];
            return;
        }
    }
    
    fprintf(stderr, "Couldn't find site name \"%s\" in internal site list\n", fci->siteName);
    exit(1);
}

#define DT_FORMAT_1 1
#define DT_FORMAT_2 2

int parseDateTime(forecastInputType *fci, dateTimeType *dt, char *dateTimeStr)
{
    int dt_numFields;
    char *dt_fields[32];
    char origStr[1024];
    static char dtFormat = 0;
    
    strcpy(origStr, dateTimeStr);

#define DateTimeError { sprintf(ErrStr, "Bad date/time string in input, line %d: %s\n", LineNumber, origStr); FatalError("parseDateTime()", ErrStr); }
    
    if(!dtFormat) {
        if(strstr(dateTimeStr, "-") != NULL)
            dtFormat = DT_FORMAT_1;  // yyyy-mm-dd
        else
            dtFormat = DT_FORMAT_2;  // mm/dd/yyyy
    }
    
    dt_numFields = split(dateTimeStr, dt_fields, 32, " ");
    char *dt_date = strdup(dt_fields[0]);
    char *dt_time = strdup(dt_fields[1]);
    
    if(dt_date == NULL || strlen(dt_date) == 0) 
        DateTimeError
            
    if(dt_time == NULL || strlen(dt_time) == 0) 
        DateTimeError

    if(dtFormat == DT_FORMAT_1) {    
        dt_numFields = split(dt_date, dt_fields, 32, "-");  /* split date line */
        if(dt_numFields != 3) 
            DateTimeError
            
        dt->year  = atoi(dt_fields[0]);
        dt->month = atoi(dt_fields[1]);
        dt->day   = atoi(dt_fields[2]);
    }
    else {
        dt_numFields = split(dt_date, dt_fields, 32, "/");  /* split date line */
        if(dt_numFields != 3) 
            DateTimeError
            
        dt->month  = atoi(dt_fields[0]);
        dt->day = atoi(dt_fields[1]);
        dt->year   = atoi(dt_fields[2]);
    }
    
    dt_numFields = split(dt_time, dt_fields, 32, ":");  /* split time line */
    if(dt_numFields < 2)
        DateTimeError
            
    dt->hour  = atoi(dt_fields[0]);
    dt->min   = atoi(dt_fields[1]);
    setObsTime(dt);

    if(!dateTimeSanityCheck(dt))
        FatalError("parseDateTime()", "Bad date/time string");
    
    // finally, do a check to see if we're within the -a start,end range
    if(fci->startDate.year > 1900) {
        return(dt->obs_time >= fci->startDate.obs_time && dt->obs_time <= fci->endDate.obs_time);
    }
    
    return True;
}

#define checkTooLow(X,Y) { if(thisSample->sunIsUp && thisSample->X < 1) { fprintf(fci->warningsFp, "Warning: line %d: %s looks too low: %.1f\n", LineNumber, Y, thisSample->X); }}

int readDataFromLine(forecastInputType *fci, char *fields[])
{
    /*
    5 sr_zen	
    6 sr_global	
    7 sr_direct	
    8 sr_diffuse	
    9 sr_temp	
    10 sr_wspd	
    11 sr_rh
    */
#ifdef DUMP_ALL_DATA
    static char firstTime = True;
    static char *filteredDataFileName = "filteredInputData.csv";
#endif
    
    timeSeriesType *thisSample = &(fci->timeSeries[fci->numTotalSamples - 1]);  
        
    thisSample->zenith = atof(fields[fci->columnInfo[fci->zenithCol].inputColumnNumber]);
    if(thisSample->zenith < 0 || thisSample->zenith > 180) {
        sprintf(ErrStr, "Got bad zenith angle at line %d: %.2f\n", LineNumber, thisSample->zenith);
        FatalError("readDataFromLine()", ErrStr);
    }
    
    thisSample->sunIsUp = (thisSample->zenith < 85);
    if(thisSample->sunIsUp)
        fci->numDaylightRecords++;
       
    thisSample->groundGHI = atof(fields[fci->columnInfo[fci->groundGHICol].inputColumnNumber]);
/*
    if(thisSample->sunIsUp && thisSample->groundGHI < 1)
        fprintf(stderr, "Warning: line %d: groundGHI looks too low: %.1f\n", LineNumber, thisSample->groundGHI);
*/
    
    if(thisSample->groundGHI < MIN_IRR || thisSample->groundGHI > MAX_IRR) {
        sprintf(ErrStr, "Got bad surface GHI at line %d: %.2f\n", LineNumber, thisSample->groundGHI);
        FatalError("readDataFromLine()", ErrStr);
    }
    //checkTooLow(groundGHI, "groundGHI");
    if(thisSample->sunIsUp && thisSample->groundGHI < 1)  
        fprintf(fci->warningsFp, "Warning: line %d: %s looks too low: %.1f\n", LineNumber, "groundGHI", thisSample->groundGHI);
/*
    if(thisSample->groundGHI < MIN_GHI_VAL)
        thisSample->isValid = False;
*/
    
    thisSample->groundDNI = atof(fields[fci->columnInfo[fci->groundDNICol].inputColumnNumber]);
    if(thisSample->groundDNI < MIN_IRR || thisSample->groundDNI > MAX_IRR) {
        sprintf(ErrStr, "Got bad surface DNI at line %d: %.2f\n", LineNumber, thisSample->groundDNI);
        FatalError("readDataFromLine()", ErrStr);
    }
    
    thisSample->groundDiffuse = atof(fields[fci->columnInfo[fci->groundDiffuseCol].inputColumnNumber]);
    if(thisSample->groundDiffuse < MIN_IRR || thisSample->groundDiffuse > MAX_IRR) {
        sprintf(ErrStr, "Got bad surface diffuse at line %d: %.2f\n", LineNumber, thisSample->groundDiffuse);
        FatalError("readDataFromLine()", ErrStr);
    }  
    
    thisSample->clearskyGHI = atof(fields[fci->columnInfo[fci->clearskyGHICol].inputColumnNumber]);
    if(thisSample->clearskyGHI < MIN_IRR || thisSample->clearskyGHI > MAX_IRR) {
        sprintf(ErrStr, "Got bad clearsky GHI at line %d: %.2f\n", LineNumber, thisSample->clearskyGHI);
        FatalError("readDataFromLine()", ErrStr);
    }
    checkTooLow(clearskyGHI, "clearskyGHI");
    
    thisSample->satGHI = atof(fields[fci->columnInfo[fci->satGHICol].inputColumnNumber]);
    checkTooLow(satGHI, "satGHI");

    //if(thisSample->clearskyGHI < MIN_GHI_VAL)
    //    thisSample->isValid = False;
    
    thisSample->groundTemp = atof(fields[9]);
/*
    if(thisSample->groundTemp < -40 || thisSample->groundTemp > 50) {
        sprintf(ErrStr, "Got bad surface temperature at line %d: %.2f\n", LineNumber, thisSample->groundTemp);
        FatalError("readDataFromLine()", ErrStr);
    }
*/
    
    thisSample->groundWind = atof(fields[10]);
/*
    if(thisSample->groundWind < -90 || thisSample->groundWind > 90) {
        sprintf(ErrStr, "Got bad surface wind at line %d: %.2f\n", LineNumber, thisSample->groundWind);
        FatalError("readDataFromLine()", ErrStr);
    }
*/
    
    thisSample->groundRH = atof(fields[11]);
/*
    if(thisSample->groundRH < -1 || thisSample->groundRH > 110) {
        sprintf(ErrStr, "Got bad surface relative humidity at line %d: %.2f\n", LineNumber, thisSample->groundRH);
        FatalError("readDataFromLine()", ErrStr);
    }
*/
     
    
    int columnIndex, modelIndex, hourIndex;
    for(columnIndex=fci->startModelsColumnNumber; columnIndex<fci->numColumnInfoEntries; columnIndex++) {
        hourIndex = fci->columnInfo[columnIndex].hourGroupIndex;
        modelIndex = fci->columnInfo[columnIndex].modelIndex;
        thisSample->hourGroup[hourIndex].modelGHI[modelIndex] = atof(fields[fci->columnInfo[columnIndex].inputColumnNumber]);
        //fprintf(stderr, "modelDesc=%s,columnInfoIndex=%d,inputCol=%d,hourInd=%d,modelIndex=%d,scanVal=%s,atofVal=%.1f\n", fci->columnInfo[columnIndex].columnName, columnIndex, fci->columnInfo[columnIndex].inputColumnNumber, 
        //        hourIndex, modelIndex, fields[fci->columnInfo[columnIndex].inputColumnNumber],thisSample->hourGroup[hourIndex].modelGHI[modelIndex]);
        //if(thisSample->modelGHIvalues[modelIndex] < MIN_GHI_VAL)
        //    thisSample->isValid = False;
        //if(thisSample->sunIsUp && thisSample->hourGroup[hourIndex].modelGHI[modelIndex] < 1) { 
        //    fprintf(stderr, "Warning: line %d: %s looks too low: %.1f\n", LineNumber, fci->hourErrorGroup[hourIndex].modelError[modelIndex].columnName, thisSample->hourGroup[hourIndex].modelGHI[modelIndex]); 
        //}
    }

#ifdef DUMP_ALL_DATA
    if(firstTime) {        
        if(filteredDataFileName == NULL || (FilteredDataFp = fopen(filteredDataFileName, "w")) == NULL) {
            sprintf(ErrStr, "Couldn't open output file %s\n", filteredDataFileName);
            FatalError("readDataFromLine()", ErrStr);
        }
        //FilteredDataFp = stderr;
        
        fprintf(FilteredDataFp, "#year,month,day,hour,minute,zenith,groundGHI,groundDNI,groundDiffuse,clearskyGHI");
        for(columnIndex=fci->startModelsColumnNumber; columnIndex<fci->numColumnInfoEntries; columnIndex++) {
                fprintf(FilteredDataFp, ",%s", fci->columnInfo[columnIndex].columnName);
        }
        fprintf(FilteredDataFp, "\n");
        firstTime = False;
    }
#endif
    
    if(1) {
        //fci->numValidSamples++;
#ifdef DUMP_ALL_DATA
        fprintf(FilteredDataFp, "%s,%.2f,%.0f,%.0f,%.0f,%.0f", dtToStringCsv2(&thisSample->dateTime), thisSample->zenith, thisSample->groundGHI, thisSample->groundDNI, thisSample->groundDiffuse, thisSample->clearskyGHI);
#endif
        for(columnIndex=fci->startModelsColumnNumber; columnIndex<fci->numColumnInfoEntries; columnIndex++) {        
            hourIndex = fci->columnInfo[columnIndex].hourGroupIndex;
            modelIndex = fci->columnInfo[columnIndex].modelIndex;
#ifdef DUMP_ALL_DATA
            fprintf(FilteredDataFp, ",%.1f", thisSample->hourGroup[hourIndex].modelGHI[modelIndex]);
#endif
    }
#ifdef DUMP_ALL_DATA
        fprintf(FilteredDataFp, "\n");
#endif
    }
    
    return True;
}
/*
 
 Columns of interest
 
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

 Column Name                            Description
				
ncep_RAP_DSWRF_1                        NCEP RAP GHI		
    PERSISTANCE              PERSISTANCE	
ncep_NAM_hires_CSDSF_1                  NAM Hi Res Clear Sky		
ncep_NAM_hires_LCDC_1                   NAM Hi Res low cloud		
ncep_NAM_hires_MCDC_1                   NAM Hi Res Med cloud		
ncep_NAM_hires_HCDC_1                   NAM Hi Res High Cloud		
ncep_NAM_hires_TCDC_1                   NAM Hi Res Total Cloud		
ncep_NAM_DSWRF_1                        NAM Low Res Instant GHI		
ncep_NAM_TCDC_1                         NAM Low res total cloud		
ncep_NAM_flag_1                         NAM grab flag		
ncep_GFS_sfc_DSWRF_surface_avg_1	GFS Hi Res Average GHI		
ncep_GFS_sfc_DSWRF_surface_inst_1	GFS Hi Res Instant GHI		
ncep_GFS_sfc_TCDC_high_1                GFS High Res High clouds		
ncep_GFS_sfc_TCDC_mid_1                 GFS High Res Mid clouds		
ncep_GFS_sfc_TCDC_low_1                 GFS High Res Low Clouds		
ncep_GFS_sfc_TCDC_total_1               GFS High Res total clouds		
ncep_GFS_DSWRF_1                        GFS Low res Average GHI		
ncep_GFS_TCDC_total_1                   GFS Low Res total cloud		
ncep_GFS_flag_1                         GFS grab flag		
NDFD_sky_1                              NDFD Total Cloud		
NDFD_global_1                           NDFD GHI		
NDFD_flag_1                             NDFD Grab flag		
cm_1                                    Cloud motion GHI		
ecmwf_flag_1                            ECMWF grab flag		
ecmwf_cloud_1                           ECMWF total cloud
ecmwf_ghi_1                             ECMWF average GHI		
*/

void initForecastInfo(forecastInputType *fci)
{
    fci->lat = -999;
    fci->lon = -999;
    fci->siteGroup = NULL;
    fci->siteName = NULL;
    fci->numModels = 0;
    fci->numHourGroups = 0;
    fci->numTotalSamples = 0;
    allocatedSamples = 8670 * MAX_MODELS;
    fci->timeSeries = (timeSeriesType *) malloc(allocatedSamples * sizeof(timeSeriesType));
    fci->numColumnInfoEntries = 0;
    fci->numInputRecords = 0;
    fci->numDaylightRecords = 0;

    LineNumber = 0;
}

void incrementTimeSeries(forecastInputType *fci)
{
    fci->numTotalSamples++;
    if(fci->numTotalSamples == allocatedSamples) {
        allocatedSamples *= 2;
        fci->timeSeries = (timeSeriesType *) realloc(fci->timeSeries, allocatedSamples * sizeof(timeSeriesType));
    }
}

void initInputColumns(forecastInputType *fci)
{
    int i, hourIndex;
    
    //fci->numHourGroups = 15;       
/*
[adk2]:/home/jim/mom> head -2 test-data-4-jim-desert-rock.2.csv | tail -1 | sed -e 's/,/\n/g' | grep ncep_NAM_hires_DSWRF_inst_
ncep_NAM_hires_DSWRF_inst_1
ncep_NAM_hires_DSWRF_inst_2
ncep_NAM_hires_DSWRF_inst_3
ncep_NAM_hires_DSWRF_inst_4
ncep_NAM_hires_DSWRF_inst_5
ncep_NAM_hires_DSWRF_inst_6
ncep_NAM_hires_DSWRF_inst_7
ncep_NAM_hires_DSWRF_inst_8
ncep_NAM_hires_DSWRF_inst_9
ncep_NAM_hires_DSWRF_inst_12
ncep_NAM_hires_DSWRF_inst_15
ncep_NAM_hires_DSWRF_inst_18
ncep_NAM_hires_DSWRF_inst_21
ncep_NAM_hires_DSWRF_inst_24
ncep_NAM_hires_DSWRF_inst_30
*/

    
    // ground and satellite data columns
    fci->zenithCol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "sr_zen", "zenith angle", 0, -1, 0);
    fci->groundGHICol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "sr_global", "ground GHI", 0, -1, 0);   
    fci->groundDNICol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "sr_direct", "ground DNI", 0, -1, 0);   
    fci->groundDiffuseCol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "sr_diffuse", "ground diffuse", 0, -1, 0);   
    fci->satGHICol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "sat_ghi", "sat model GHI", 0, -1, 0);   
    fci->clearskyGHICol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "clear_ghi", "clearsky GHI", 0, -1, 0);  
    
    fci->startModelsColumnNumber = fci->numColumnInfoEntries;

    // this is where the forecast model data starts
    // right now you have to know the model names ahead of time
    for(hourIndex=0; hourIndex<fci->numHourGroups; hourIndex++) {        
        fci->numModels = 0;
        registerColumnInfo(fci, "ncep_RAP_DSWRF_", "NCEP RAP GHI", 18, hourIndex, 0);                      
        registerColumnInfo(fci, "persistence_", "Persistence GHI", 168, hourIndex, 1);
        registerColumnInfo(fci, "ncep_NAM_hires_DSWRF_inst_", "NAM Hi Res Instant GHI", 54, hourIndex, 0);		
        registerColumnInfo(fci, "ncep_NAM_DSWRF_", "NAM Low Res Instant GHI", 78, hourIndex, 0);	
        registerColumnInfo(fci, "ncep_GFS_sfc_DSWRF_surface_avg_", "GFS Hi Res Average GHI", 384, hourIndex, 0);		
        registerColumnInfo(fci, "ncep_GFS_sfc_DSWRF_surface_inst_", "GFS Hi Res Instant GHI", 384, hourIndex, 0);		
        registerColumnInfo(fci, "ncep_GFS_DSWRF_", "GFS Low res Average GHI", 192, hourIndex, 0);	
        registerColumnInfo(fci, "NDFD_global_", "NDFD GHI", 144, hourIndex, 0);
        registerColumnInfo(fci, "cm_", "Cloud motion GHI", 9, hourIndex, 0);
        registerColumnInfo(fci, "ecmwf_ghi_", "ECMWF average GHI", 240, hourIndex, 0);        
    }
    
    for(i=0; i<fci->numColumnInfoEntries; i++)
        fci->columnInfo[i].inputColumnNumber = -1;   // set default
    
}

void registerColumnInfo(forecastInputType *fci, char *columnName, char *columnDescription, int maxHourAhead, int hourIndex, int isReference) 
{
    char tempColName[1024], tempColDesc[1024];    
    
    if(hourIndex >= 0) {
        if(!checkModelAgainstSite(fci, columnName)) // if this forecast model isn't on the site's model list, don't register it
            return;
        
        fci->hourErrorGroup[hourIndex].modelError[fci->numModels].isReference = isReference;
        fci->columnInfo[fci->numColumnInfoEntries].hourGroupIndex = hourIndex;   // this is passed as an arg
        fci->columnInfo[fci->numColumnInfoEntries].modelIndex = fci->numModels;
        sprintf(tempColName, "%s%d", columnName, fci->hourErrorGroup[hourIndex].hoursAhead);
        sprintf(tempColDesc, "%s +%d hours", columnDescription, fci->hourErrorGroup[hourIndex].hoursAhead);
        fci->columnInfo[fci->numColumnInfoEntries].columnName = strdup(tempColName); //"ncep_RAP_DSWRF_1";                      
        fci->columnInfo[fci->numColumnInfoEntries].columnDescription = strdup(tempColDesc); //"NCEP RAP GHI";  
        fci->columnInfo[fci->numColumnInfoEntries].maxHourAhead = maxHourAhead;
        fci->numColumnInfoEntries++;
        fci->numModels++;
    }
    else {
        fci->columnInfo[fci->numColumnInfoEntries].columnName = columnName; //"ncep_RAP_DSWRF_1";                      
        fci->columnInfo[fci->numColumnInfoEntries].columnDescription = columnDescription; //"NCEP RAP GHI";  
        fci->columnInfo[fci->numColumnInfoEntries].maxHourAhead = maxHourAhead;
        fci->columnInfo[fci->numColumnInfoEntries].hourGroupIndex = -1;  
        fci->columnInfo[fci->numColumnInfoEntries].modelIndex = -1;
        fci->numColumnInfoEntries++;
    }
}

int checkModelAgainstSite(forecastInputType *fci, char *modelName)
{
    int modelNum;
    
    for(modelNum=0; modelNum<fci->thisSite->numModels; modelNum++) {
        if(strcmp(modelName, fci->thisSite->modelNames[modelNum]) == 0) {
            return True;
        }
    } 
    
    fprintf(stderr, "Note: model %s not turned on for site %s\n", modelName, fci->siteName);
    return False;
}

void initSiteInfo(forecastInputType *fci)
{
    siteType *si;
/*
typedef struct {
    int numModels;
    char siteNames[2048];
    char modelNames[MAX_MODELS][2048];
    int maxHoursAhead[MAX_MODELS];
} siteType;
*/
    fci->numSites = 0;
    
    si = &fci->allSiteInfo[fci->numSites]; 
    registerSiteName(si, "Desert_Rock_NV");
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_RAP_DSWRF_", 18);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "persistence_", 168);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_NAM_hires_DSWRF_inst_", 54);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_NAM_DSWRF_", 78);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_sfc_DSWRF_surface_avg_", 384);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_sfc_DSWRF_surface_inst_", 384);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_DSWRF_", 192);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "NDFD_global_", 144);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "cm_", 9);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ecmwf_ghi_", 240);
    fci->numSites++;
    
    si = &fci->allSiteInfo[fci->numSites];    
    registerSiteName(si, "Goodwin_Creek_MS");
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_RAP_DSWRF_", 18);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "persistence_", 168);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_NAM_hires_DSWRF_inst_", 54);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_NAM_DSWRF_", 78);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_sfc_DSWRF_surface_avg_", 384);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_sfc_DSWRF_surface_inst_", 384);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_DSWRF_", 192);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "NDFD_global_", 144);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "cm_", 9);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ecmwf_ghi_", 240);
    fci->numSites++;
    
    si = &fci->allSiteInfo[fci->numSites];    
    registerSiteName(si, "Penn_State_PA");
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_RAP_DSWRF_", 18);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "persistence_", 168);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_NAM_hires_DSWRF_inst_", 54);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_NAM_DSWRF_", 78);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_sfc_DSWRF_surface_avg_", 384);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_sfc_DSWRF_surface_inst_", 384);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_DSWRF_", 192);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "NDFD_global_", 144);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "cm_", 9);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ecmwf_ghi_", 240);
    fci->numSites++;
    
    si = &fci->allSiteInfo[fci->numSites];  
    registerSiteName(si, "Boulder_CO");
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_RAP_DSWRF_", 18);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "persistence_", 168);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_NAM_hires_DSWRF_inst_", 54);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_NAM_DSWRF_", 78);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_sfc_DSWRF_surface_avg_", 384);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_sfc_DSWRF_surface_inst_", 384);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_DSWRF_", 192);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "NDFD_global_", 144);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "cm_", 9);
    //registerSiteModel(&fci->allSiteInfo[fci->numSites], "ecmwf_ghi_", 240);
    fci->numSites++;
    
    si = &fci->allSiteInfo[fci->numSites];  
    registerSiteName(si, "Bondville_IL");
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_RAP_DSWRF_", 18);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "persistence_", 168);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_NAM_hires_DSWRF_inst_", 54);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_NAM_DSWRF_", 78);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_sfc_DSWRF_surface_avg_", 384);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_sfc_DSWRF_surface_inst_", 384);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_DSWRF_", 192);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "NDFD_global_", 144);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "cm_", 9);
    //registerSiteModel(&fci->allSiteInfo[fci->numSites], "ecmwf_ghi_", 240);
    fci->numSites++;
    
    si = &fci->allSiteInfo[fci->numSites];  
    registerSiteName(si, "Hanford_CA");
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_RAP_DSWRF_", 18);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "persistence_", 168);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_NAM_hires_DSWRF_inst_", 54);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_NAM_DSWRF_", 78);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_sfc_DSWRF_surface_avg_", 384);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_sfc_DSWRF_surface_inst_", 384);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_DSWRF_", 192);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "NDFD_global_", 144);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "cm_", 9);
    //registerSiteModel(&fci->allSiteInfo[fci->numSites], "ecmwf_ghi_", 240);
    fci->numSites++;
    
    si = &fci->allSiteInfo[fci->numSites];  
    registerSiteName(si, "Sioux_Falls_SD");
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_RAP_DSWRF_", 18);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "persistence_", 168);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_NAM_hires_DSWRF_inst_", 54);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_NAM_DSWRF_", 78);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_sfc_DSWRF_surface_avg_", 384);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_sfc_DSWRF_surface_inst_", 384);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "ncep_GFS_DSWRF_", 192);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "NDFD_global_", 144);
    registerSiteModel(&fci->allSiteInfo[fci->numSites], "cm_", 9);
    //registerSiteModel(&fci->allSiteInfo[fci->numSites], "ecmwf_ghi_", 240);
    fci->numSites++;
        
}

void registerSiteName(siteType *si, char *siteName)
{
    si->siteName = strdup(siteName);
    si->numModels = 0;
}

void registerSiteModel(siteType *si, char *modelName, int maxHoursAhead)
{
    si->modelNames[si->numModels] = strdup(modelName);
    si->maxHoursAhead[si->numModels] = maxHoursAhead;
    si->numModels++;
}

void parseColumnInfo(forecastInputType *fci, char *colNamesLine)
{
    int numFields;
    char *fields[MAX_FIELDS];
    int i, j, matches=0;
    
    //fprintf(stderr, "Line:%s\n", colNamesLine);
    
    getNumberOfHoursAhead(fci, colNamesLine);
    initInputColumns(fci);

    numFields = split(colNamesLine, fields, MAX_FIELDS, Delimiter);  /* split line */   
    
    for(i=0; i<numFields; i++) {
        //fprintf(stderr, "Checking column name %s...", fields[i]);
        for(j=0; j<fci->numColumnInfoEntries; j++) {
            if(strcasecmp(fields[i], fci->columnInfo[j].columnName) == 0) { // || strcasecmp(fields[i], fci->columnInfo[j].columnDescription) == 0) {
                fci->columnInfo[j].inputColumnNumber = i;
                //fprintf(stderr, "%s\n", fci->columnInfo[j].columnName);
                matches++;
                break;
            }
        }
        //if(j == fci->numColumnInfoEntries) fprintf(stderr, "no match\n");
    }

    for(i=0; i<fci->numColumnInfoEntries; i++) {
        if(fci->columnInfo[i].modelIndex >= 0) {
            fci->hourErrorGroup[fci->columnInfo[i].hourGroupIndex].modelError[fci->columnInfo[i].modelIndex].columnInfoIndex = i;
            fci->hourErrorGroup[fci->columnInfo[i].hourGroupIndex].modelError[fci->columnInfo[i].modelIndex].columnName = fci->columnInfo[i].columnName;
        }
        //fprintf(stderr, "[%d] col=%d, label=%s desc=\"%s\" modelnumber=%d hourGroup=%d\n", i, fci->columnInfo[i].inputColumnNumber, fci->columnInfo[i].columnName, fci->columnInfo[i].columnDescription,
        //                fci->columnInfo[i].modelIndex, fci->columnInfo[i].hourGroupIndex);
    }
    
    for(i=0; i<fci->numColumnInfoEntries; i++) {
        if(fci->columnInfo[i].inputColumnNumber < 0)
            fprintf(fci->warningsFp, "No match for expected column %s\n", fci->columnInfo[i].columnName);
/*
        else
            fprintf(stderr, "Got column %s\n", fci->columnInfo[i].columnName);
*/
    }

    if(matches < fci->numColumnInfoEntries) {
        sprintf(ErrStr, "Scanning line %d, only got %d out of %d matches with expected fields", LineNumber, matches, fci->numColumnInfoEntries);
        FatalError("parseColumnInfo()", ErrStr);
    }
    
    //for(i=)
 
}

void runErrorAnalysis(forecastInputType *fci) 
{
    int hourIndex;
    
    for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
        fprintf(stderr, "\n############ Running for hour ahead %d\n\n", fci->hourErrorGroup[hourIndex].hoursAhead);
        doErrorAnalysis(fci, hourIndex);
        printRmseTableHour(fci, hourIndex);
        printHourlySummary(fci, hourIndex);
        //runOptimizer(fci, hourIndex);
        runOptimizerNested(fci, hourIndex);
        fprintf(stderr, "\n############ End hour ahead %d\n", fci->hourErrorGroup[hourIndex].hoursAhead);

    }

//    printByHour(fci);
//    printByModel(fci);
    printByAnalysisType(fci);
    printSummaryCsv(fci);
}

void printByHour(forecastInputType *fci)
{
    FILE *fp;
    char fileName[1024];
    int modelIndex, hourIndex;
    modelErrorType *hourGroup;
    modelStatsType *err;
         
    for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
        hourGroup = &fci->hourErrorGroup[hourIndex];

        sprintf(fileName, "%s/%s.forecast.error.hoursAhead=%02d.csv", fci->outputDirectory, fci->siteName, hourGroup->hoursAhead);
        //fprintf(stderr, "hour[%d] hour=%d file=%s\n", hourIndex, hourGroup->hoursAhead, fileName);
        fp = fopen(fileName, "w");

        fprintf(fp, "#siteName=%s,lat=%.2f,lon=%.3f,hours ahead=%d,N=%d,mean measured GHI=%.1f\n",fci->siteName, fci->lat, fci->lon, hourGroup->hoursAhead, hourGroup->numValidSamples, hourGroup->meanMeasuredGHI);
        fprintf(fp, "#model,sum( model-ground ), sum( abs(model-ground) ), sum( (model-ground)^2 ), mae, mae percent, mbe, mbe percent, rmse, rmse percent\n");

        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
            err = &hourGroup->modelError[modelIndex];
            fprintf(fp, "%s,%.0f,%.0f,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", fci->columnInfo[err->columnInfoIndex].columnDescription, err->sumModel_Ground, err->sumAbs_Model_Ground, err->sumModel_Ground_2,
                            err->mae, err->maePct*100, err->mbe, err->mbePct*100, err->rmse, err->rmsePct*100);
        }
        fclose(fp);
    }
}

void printByModel(forecastInputType *fci)
{
    FILE *fp;
    char fileName[1024];
    int modelIndex, hourIndex;
    modelErrorType *hourGroup;
    modelStatsType *err;
    
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {   
        sprintf(fileName, "%s/%s.forecast.error.model=%s.csv", fci->outputDirectory, fci->siteName, getGenericModelName(fci, modelIndex));
        fp = fopen(fileName, "w");


        fprintf(fp, "#siteName=%s,lat=%.2f,lon=%.3f,error analysis=%s\n",fci->siteName, fci->lat, fci->lon, getGenericModelName(fci, modelIndex));
        fprintf(fp, "#model,N,hours ahead, mean measured GHI, sum( model-ground ), sum( abs(model-ground) ), sum( (model-ground)^2 ), mae, mae percent, mbe, mbe percent, rmse, rmse percent\n");

            for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
                hourGroup = &fci->hourErrorGroup[hourIndex];

                err = &hourGroup->modelError[modelIndex];
                fprintf(fp, "%s,%d,%d,%.1f,%.0f,%.0f,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", fci->columnInfo[err->columnInfoIndex].columnDescription, 
                    hourGroup->numValidSamples,fci->hourErrorGroup[hourIndex].hoursAhead,hourGroup->meanMeasuredGHI,err->sumModel_Ground, err->sumAbs_Model_Ground, err->sumModel_Ground_2,
                    err->mae, err->maePct*100, err->mbe, err->mbePct*100, err->rmse, err->rmsePct*100);
        }
        fclose(fp);
    }    
}

void printByAnalysisType(forecastInputType *fci)
{
    FILE *fp;   
    int modelIndex, hourIndex;
    modelErrorType *hourGroup;
    modelStatsType *err;
    
    if(!(fp = openErrorTypeFile(fci, "MAE")))
        return;
    
    for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
        hourGroup = &fci->hourErrorGroup[hourIndex];
        fprintf(fp, "%d,%d", hourGroup->hoursAhead, hourGroup->numValidSamples);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {          
            err = &hourGroup->modelError[modelIndex];
            fprintf(fp, ",%.1f%%", err->maePct*100);
        }
        fprintf(fp, "\n");
    }    
    fclose(fp);   

    if(!(fp = openErrorTypeFile(fci, "MBE")))
        return;
    
    for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
        hourGroup = &fci->hourErrorGroup[hourIndex];
        fprintf(fp, "%d,%d", hourGroup->hoursAhead, hourGroup->numValidSamples);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {          
            err = &hourGroup->modelError[modelIndex];
            fprintf(fp, ",%.1f%%", err->mbePct*100);
        }
        fprintf(fp, "\n");
    }    
    fclose(fp);   

    if(!(fp = openErrorTypeFile(fci, "RMSE")))
        return;
    
    for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
        hourGroup = &fci->hourErrorGroup[hourIndex];
        fprintf(fp, "%d,%d", hourGroup->hoursAhead, hourGroup->numValidSamples);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {          
            err = &hourGroup->modelError[modelIndex];
            fprintf(fp, ",%.1f%%", err->rmsePct*100);
        }
        fprintf(fp, "\n");
    }    
    fclose(fp);   
}

void printRmseTableHour(forecastInputType *fci, int hourIndex)
{
    FILE *fp;   
    int modelIndex;
    modelErrorType *hourGroup = &fci->hourErrorGroup[hourIndex];
    
    if(!(fp = openErrorTypeFileHourly(fci, "RMSE", hourIndex)))
       return;
    //fp = stderr;
    
    //for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
        fprintf(fp, "hours ahead = %d\nN = %d\n", hourGroup->hoursAhead, hourGroup->numValidSamples);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {          
            if(hourGroup->modelError[modelIndex].isActive)
                fprintf(fp, "%-35s = %.1f%%\n", getGenericModelName(fci, modelIndex), hourGroup->modelError[modelIndex].rmsePct*100);
        }
        fprintf(fp, "\n");
    //}    
    //fclose(fp);   
}

FILE *openErrorTypeFileHourly(forecastInputType *fci, char *analysisType, int hourIndex)
{
    char fileName[1024];
    static FILE *fp;
//    int modelIndex;
    char satGHIerr[1024];
//    modelErrorType *hourGroup = &fci->hourErrorGroup[hourIndex];
    
    if(fci->outputDirectory == NULL || fci->siteName == NULL) {
        fprintf(stderr, "openErrorTypeFile(): got null outputDirectory or siteName\n");
        return NULL;
    }
    
    if(strcasecmp(analysisType, "rmse") == 0)
        sprintf(satGHIerr, "sat GHI RMSE=%.1f%%", fci->hourErrorGroup[hourIndex].satModelError.rmsePct * 100);
    else if(strcasecmp(analysisType, "mae") == 0)
        sprintf(satGHIerr, "sat GHI MAE=%.1f%%", fci->hourErrorGroup[hourIndex].satModelError.maePct * 100);
    else 
        sprintf(satGHIerr, "sat GHI MBE=%.1f%%", fci->hourErrorGroup[hourIndex].satModelError.mbePct * 100);

    
    sprintf(fileName, "%s/%s.forecast.analysisType=%s.csv", fci->outputDirectory, fci->siteName, analysisType);
    //fp = fopen(fileName, "w");
    fp = stderr;
    
    fprintf(fp, "siteName=%s\nlat=%.2f\nlon=%.3f\nanalysisType=%s\n%s\n",fci->siteName, fci->lat, fci->lon, analysisType, satGHIerr);
/*
    fprintf(fp, "hours ahead,N,");
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++)  {
        if(hourGroup->modelError[modelIndex].isActive)
            fprintf(fp, "%s,", getGenericModelName(fci, modelIndex));
    }
    fprintf(fp, "\n");
*/
    
    return fp;
}

FILE *openErrorTypeFile(forecastInputType *fci, char *analysisType)
{
    char fileName[1024];
    static FILE *fp;
    int modelIndex;
    char satGHIerr[1024];
    
    if(fci->outputDirectory == NULL || fci->siteName == NULL) {
        fprintf(stderr, "openErrorTypeFile(): got null outputDirectory or siteName\n");
        return NULL;
    }
    
    if(strcasecmp(analysisType, "rmse") == 0)
        sprintf(satGHIerr, "sat GHI RMSE=%.1f%%", fci->hourErrorGroup[0].satModelError.rmsePct * 100);
    else if(strcasecmp(analysisType, "mae") == 0)
        sprintf(satGHIerr, "sat GHI MAE=%.1f%%", fci->hourErrorGroup[0].satModelError.maePct * 100);
    else 
        sprintf(satGHIerr, "sat GHI MBE=%.1f%%", fci->hourErrorGroup[0].satModelError.mbePct * 100);

    
    sprintf(fileName, "%s/%s.forecast.analysisType=%s.csv", fci->outputDirectory, fci->siteName, analysisType);
    fp = fopen(fileName, "w");
    
    fprintf(fp, "#siteName=%s,lat=%.2f,lon=%.3f,analysisType=%s,%s\n",fci->siteName, fci->lat, fci->lon, analysisType, satGHIerr);
    fprintf(fp, "#hours ahead,N,");
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++)    
        fprintf(fp, "%s,", getGenericModelName(fci, modelIndex));
    fprintf(fp, "\n");
    
    return fp;
}

int getMaxHoursAhead(forecastInputType *fci, int modelIndex)
{
    int columnIndex = fci->hourErrorGroup[0].modelError[modelIndex].columnInfoIndex;
    return (fci->columnInfo[columnIndex].maxHourAhead);
}

char *getGenericModelName(forecastInputType *fci, int modelIndex)
{
    static char modelDesc[1024];
    
    if(modelIndex < 0)
        return("satellite");
    strcpy(modelDesc, fci->hourErrorGroup[0].modelError[modelIndex].columnName);  // should be something like ncep_RAP_DSWRF_1
    modelDesc[strlen(modelDesc)-2] = '\0';   // lop off the _1 => ncep_RAP_DSWRF

    return(modelDesc);
}

char *getColumnNameByHourModel(forecastInputType *fci, int hrInd, int modInd)
{
    int col;
    static char modelDesc[1024];
    
    // this should probably be solved in a data structure
    for(col=0; col<fci->numColumnInfoEntries; col++) {
            if(fci->columnInfo[col].hourGroupIndex == hrInd && fci->columnInfo[col].modelIndex == modInd) {
                strcpy(modelDesc, fci->columnInfo[col].columnName);
                modelDesc[strlen(modelDesc)-2] = '\0';
                return modelDesc;
            }
    }
    
    return NULL;
}

// this automatically parses out the hours ahead from the column names line
// assumes uniformity in the number of hours ahead -- that is, that all models have the same number of 
// hours ahead columns
void getNumberOfHoursAhead(forecastInputType *fci, char *origLine)
{
    char *p, *q, *currPtr, copyLine[1024 * 1024], measName[1024];
    int lineLength = strlen(origLine);
    int hour;
 
    //copyLine = (char *) malloc(1024 * 16);
    strcpy(copyLine, origLine);  // this function is string destructive
    currPtr = copyLine;

    q = strstr(copyLine, "_1");  // this assumes the first instance of _1 will be a model entry...
    if(q == NULL) {
        FatalError("getNumberOfHoursAhead()", "problem getting number of hours ahead");
    }
    
    p = q;
    while(*p != *Delimiter && p > currPtr) p--;  
    p++;
    strncpy(measName, p, q-p+1);  // include the _
    measName[q-p+1] = '\0';
    
    
    while((q = strstr(currPtr, measName)) != NULL) {
        while(*q != *Delimiter && q < (copyLine + lineLength)) q++;
        p = q;
        while(*p != '_' && p > copyLine) p--;
        p++;
        *q = '\0';
        hour = atoi(p);

        if(hour <= 0 || hour > 500)
            FatalError("getNumberOfHoursAhead()", "problem getting number of hours ahead");

        //fprintf(stderr, "hour[%d] = %d\n", fci->numHourGroups, hour);
        
        fci->hourErrorGroup[fci->numHourGroups].hoursAhead = hour;
        fci->numHourGroups++;
        
        currPtr = q+1;
    }
    
    if(fci->numHourGroups < 3) {
        FatalError("getNumberOfHoursAhead()", "problem getting number of hours ahead");
    }
    
    if(fci->startHourLowIndex == -1) {
        fci->startHourLowIndex = 0;
        fci->startHourHighIndex = fci->numHourGroups;
    }
    //free(copyLine);
}

int parseHourIndexes(forecastInputType *fci, char *optarg)
{
    char *lowStr, *highStr;
    char backup[256];
    
    if(optarg == NULL || *optarg == '\0') {
        fprintf(stderr, "Got bad low and/or high indexes for -r flag");
        return False;
    }

    strncpy(backup, optarg, 256);
    lowStr = highStr = backup;
    
    while(*highStr && *highStr != ',')
        highStr++;

    if(*highStr) {
        *highStr = '\0';
        highStr++;
    }
    else {
        fprintf(stderr, "Got bad high index to -r flag");
        return False;
    }

    fci->startHourLowIndex = atoi(lowStr);
    fci->startHourHighIndex = atoi(highStr);
    
    if(fci->startHourHighIndex < 0 || fci->startHourHighIndex > 100) {
        fprintf(stderr, "Got bad high index to -r flag");
        return False;
    }        
    if(fci->startHourLowIndex < 0 || fci->startHourLowIndex > 100) {
        fprintf(stderr, "Got bad low index to -r flag");
        return False;
    }        
    
    return True;
}

int parseDates(forecastInputType *fci, char *optarg)
{
    char *startStr, *endStr;
    char backup[256];
    dateTimeType *start, *end;
    
    if(optarg == NULL || *optarg == '\0') {
        fprintf(stderr, "Got bad start and/or end dates for -a flag");
        return False;
    }

    strncpy(backup, optarg, 256);
    startStr = endStr = backup;

//    start = &sat->archiverStartDate;
//    end = &sat->archiverEndDate;
    
    start = &fci->startDate;
    end = &fci->endDate;
    start->year = start->month = start->hour = start->min = 0;  // in case we get a short date
    end->year = end->month = end->hour = end->min = 0;  // in case we get a short date

    while(*endStr && *endStr != ',')
        endStr++;

    if(*endStr) {
        *endStr = '\0';
        endStr++;
    }
    else {
        fprintf(stderr, "Got bad end date to -a flag");
        return False;
    }
                  // yyyymmddhhmm
    sscanf(startStr, "%04d%02d%02d%02d%02d", &start->year, &start->month, &start->day, &start->hour, &start->min);   
    setObsTime(start);  // calculate time_t and doy numbers

    if(!dateTimeSanityCheck(start)) {
        fprintf(stderr, "parseDates(): error in start date %s in argument string %s\n", startStr, backup);
        return(False);
    }
    
    sscanf(endStr  , "%4d%02d%02d%02d%02d", &end->year, &end->month, &end->day, &end->hour, &end->min);    
    setObsTime(end);

    if(!dateTimeSanityCheck(end)) {
        fprintf(stderr, "parseDates(): error in end date %s in argument string %s\n", endStr, backup);
        return(False);
    }
            
    return True;
}
        

//
// Print message and exit with exit code = 1
//
void fatalError(char *functName, char *errStr, char *file, int linenumber)
{
    int exitCode = 1;
    fatalErrorWithExitCode(functName, errStr, file, linenumber, exitCode);
}

//
// Print message and exit with specific exitCode
//
void fatalErrorWithExitCode(char *functName, char *errStr, char *file, int linenumber, int exitCode)
{
    fprintf(stderr, "FATAL: %s: %s in %s at line %d\n", functName, errStr, file, linenumber);
    exit(exitCode);
}

#define getModelN(modelIndex) (modelIndex < 0 ? (hourGroup->satModelError.N) : (hourGroup->modelError[modelIndex].N))

void printHourlySummary(forecastInputType *fci, int hourIndex)
{
    int modelIndex, hoursAhead = fci->hourErrorGroup[hourIndex].hoursAhead;
    modelErrorType *hourGroup = &fci->hourErrorGroup[hourIndex];
    
    fprintf(stderr, "\nHR%d=== Summary for hour %d (number of good samples) ===\n", hoursAhead, hoursAhead);
    fprintf(stderr, "HR%d\t%-40s : %d\n", hoursAhead, "N for group", hourGroup->numValidSamples);
    fprintf(stderr, "HR%d\t%-40s : %d\n", hoursAhead, "ground GHI", hourGroup->ground_N);
    for(modelIndex=-1; modelIndex < fci->numModels; modelIndex++) {
        if(modelIndex < 0 || hourGroup->modelError[modelIndex].isActive)
            fprintf(stderr, "HR%d\t%-40s : %d\n", hoursAhead, getGenericModelName(fci, modelIndex), getModelN(modelIndex));
    }
}
    
void printSummaryCsv(forecastInputType *fci)
{
    int hourIndex, modelIndex;
    modelErrorType *hourGroup;
    char filename[1024], modelName[1024];
    FILE *fp;
    
    sprintf(filename, "%s/%s.wtRange=%.2f-%.2f_ha=%d-%d.csv", fci->outputDirectory, fci->siteName, fci->weightSumLowCutoff, fci->weightSumHighCutoff, fci->hourErrorGroup[fci->startHourLowIndex].hoursAhead, fci->hourErrorGroup[fci->startHourHighIndex].hoursAhead);
    
    if((fp = fopen(filename, "w")) == NULL) {
        fprintf(stderr, "Couldn't open file %s\n", filename);
        exit(1);
    }
    // print the header
    fprintf(fp, "#site=%s lat=%.3f lon=%.3f\n", fci->siteName, fci->lat, fci->lon);
    fprintf(fp, "#hoursAhead,group N,sat RMSE,phase 1 RMSE,phase 2 RMSE,phase 1 RMSE calls,phase 2 RMSE calls");
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        strcpy(modelName, getGenericModelName(fci, modelIndex));
        fprintf(fp, ",%s model, %s status,%s N,%s RMSE,%s Weight 1,%s weight 2", modelName, modelName, modelName, modelName, modelName, modelName);
    }
    fprintf(fp, "\n");

    // generate model results
    for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
        hourGroup = &fci->hourErrorGroup[hourIndex];
        fprintf(fp, "%d,%d,%.2f,%.2f,%.2f,%ld,%ld", hourGroup->hoursAhead, hourGroup->numValidSamples, hourGroup->satModelError.rmsePct * 100,
                hourGroup->optimizedRMSEphase1 * 100, hourGroup->optimizedRMSEphase2 * 100, hourGroup->phase1RMSEcalls, hourGroup->phase2RMSEcalls);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {           
            //if(hourGroup->modelError[modelIndex].isActive) {
                fprintf(fp, ",%s", hourGroup->modelError[modelIndex].isReference ? "reference" : "forecast");
                fprintf(fp, ",%s", hourGroup->modelError[modelIndex].isActive ? "active" : "inactive");
                fprintf(fp, ",%d", hourGroup->modelError[modelIndex].isActive ? hourGroup->modelError[modelIndex].N : -999);
                fprintf(fp, ",%.2f", hourGroup->modelError[modelIndex].isActive ? hourGroup->modelError[modelIndex].rmsePct * 100 : -999);
                fprintf(fp, ",%.2f", hourGroup->modelError[modelIndex].isActive ? hourGroup->modelError[modelIndex].optimizedWeightPass1 : -999);   
                fprintf(fp, ",%.2f", hourGroup->modelError[modelIndex].isActive ? hourGroup->modelError[modelIndex].optimizedWeightPass2 : -999);   
            //}
        }
        fprintf(fp, "\n");
    }
    
    fclose(fp);
}
