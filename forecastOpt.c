/* 
 Forecast Optimizer.
 */

/* todo
 * New file format.  Now, not all hours ahead will be present for all models.  So cm_ might only go out 9 hours.  However,
 * just because a model has a given hours ahead column, doesn't mean it's being used.  
 * 
 * Trying to figure out if I should attempt to figure on the fly whether a meas_hr is valid or take it from elsewhere.
 */

#include "forecastOpt.h"

#define VERSION "1.0"

#define MIN_IRR -25
#define MAX_IRR 1500
#define DUMP_ALL_DATA
    
void help(void);
void version(void);
void processForecast(forecastInputType *fci);
void initForecastInfo(forecastInputType *fci);
void incrementTimeSeries(forecastInputType *fci);
int  readForecastFile(forecastInputType *fci);
int  readDataFromLine(forecastInputType *fci, char *fields[]);
int  parseDateTime(forecastInputType *fci, dateTimeType *dt, char *dateTimeStr);
int  parseHourIndexes(forecastInputType *fci, char *optarg);
void scanHeaderLine(forecastInputType *fci);
void parseColumnInfo(forecastInputType *fci);
int  parseArgs(forecastInputType *fci, int argC, char **argV);
void registerColumnInfo(forecastInputType *fci, char *columnName, char *columnDescription, int isReference, int isForecast);
void runErrorAnalysis(forecastInputType *fci);
void getNumberOfHoursAhead(forecastInputType *fci, char *origLine);
void printByHour(forecastInputType *fci);
void printByModel(forecastInputType *fci);
void printByAnalysisType(forecastInputType *fci);
char *getColumnNameByHourModel(forecastInputType *fci, int hrInd, int modInd);
FILE *openErrorTypeFile(forecastInputType *fci, char *analysisType);
void printRmseTableHour(forecastInputType *fci, int hourIndex);
FILE *openErrorTypeFileHourly(forecastInputType *fci, char *analysisType, int hourIndex);
void registerSiteName(siteType *si, char *siteName);
void registerSiteModel(siteType *si, char *modelName, int maxHoursAhead);
void setSite(forecastInputType *fci);
int  checkModelAgainstSite(forecastInputType *fci, char *modelName);
void setSiteInfo(forecastInputType *fci, char *line);
void dumpWeightedTimeSeries(forecastInputType *fci,int hourIndex);
int runWeightedTimeSeriesAnalysis(forecastInputType *fci);
int readSummaryFile(forecastInputType *fci);
void studyData(forecastInputType *fci);
char *stripQuotes(char *str);
void stripComment(char *str);
int parseNumberFromString(char *str);

char *Progname, *OutputDirectory;
FILE *FilteredDataFp;
char ErrStr[4096];
int  allocatedSamples, HashLineNumber=1;
char *Delimiter, MultipleSites;

int main(int argc,char **argv)
{
    forecastInputType fci;
    //signal(SIGSEGV, segvhandler); // catch memory reference errors

    Progname = basename(argv[0]);  // strip out path
  

    if (!parseArgs(&fci, argc, argv)) {
       exit(1);
    }
 
    if(fci.runWeightedErrorAnalysis)
        runWeightedTimeSeriesAnalysis(&fci);
    else
        processForecast(&fci);
    
    return(EXIT_SUCCESS);
}
  
int parseArgs(forecastInputType *fci, int argC, char **argV)
{
    int c;
    extern char *optarg;
    extern int optind;
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
    fci->runWeightedErrorAnalysis = False;
    fci->descriptionFile.fileName = NULL;
    fci->runHoursAfterSunrise = False;

    //static char tabDel[32];
    //sprintf(tabDel, "%c", 9);  // ascii 9 = TAB
    
    while ((c=getopt(argC, argV, "a:c:dto:sHhvVr:mw:")) != EOF) {
        switch (c) {
            case 'd': { Delimiter = ",";
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
            case 's': { fci->runHoursAfterSunrise = True;
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
            case 'w': { fci->summaryFile.fileName = strdup(optarg);
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
    printf( "usage: %s [-dsmhvV] [-r beginHourIndex,endHourIndex] [-a begin,end] [-o outputDir] -c configFile forecastFile\n", Progname);
    printf( "where: -d = comma separated input [TAB]\n");
    printf( "       -s run hours after sunrise mode as well\n");
    printf( "       -r beginHourIndex,endHourIndex = specify which hour ahead indexes to start and end with\n");
    printf( "       -m = input data file contains multiple sites (concatenated)\n");
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



void processForecast(forecastInputType *fci)
{        
    time_t start;
    
    start = time(NULL);
    fprintf(stderr, "=== Starting processing at %s\n", timeOfDayStr());
    fprintf(stderr, "=== Using date range: %s to ", dtToStringDateTime(&fci->startDate));
    fprintf(stderr, "%s\n", dtToStringDateTime(&fci->endDate));
    fprintf(stderr, "=== Weight Sum Range: %.2f to %.2f\n", fci->weightSumLowCutoff, fci->weightSumHighCutoff);

    initForecastInfo(fci);
    readForecastFile(fci);
    
    fprintf(stderr, "=== Hours Ahead: %d to %d\n", fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead);
    fprintf(stderr, "=== Number of    input records: %d\n", fci->numInputRecords);
    fprintf(stderr, "=== Number of daylight records: %d\n", fci->numDaylightRecords);
                
    runErrorAnalysis(fci);
     
    fprintf(stderr, "=== Ending at %s\n", timeOfDayStr());
    fprintf(stderr, "=== Elapsed time: %s\n", getElapsedTime(start));
    return;
}

#define MAX_FIELDS 2048
#define LINE_LENGTH MAX_FIELDS * 64  

int readForecastFile(forecastInputType *fci)
{
    char line[LINE_LENGTH];
    int numFields;
    double lat, lon;
    char *fields[MAX_FIELDS], *fldPtr;
    dateTimeType currDate;    
    timeSeriesType *thisSample;
    char firstLine=True;
    
    if((fci->forecastTableFile.fp = fopen(fci->forecastTableFile.fileName, "r")) == NULL) {
        sprintf(ErrStr, "Couldn't open input file %s.", fci->forecastTableFile.fileName);
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
    13 lon	     <--
    14 validTime     <--
    15 sat_ghi	
    16 clear_ghi	
    17 CosSolarZenithAngle	
    18 ncep_RAP_DSWRF_1 ...
    */

    
    while(fgets(line, LINE_LENGTH, fci->forecastTableFile.fp)) {
        fci->forecastLineNumber++;
        
        if(fci->forecastLineNumber <= HashLineNumber) {
            if(fci->forecastLineNumber == HashLineNumber) {
                fci->forecastHeaderLine = strdup(line);  // save for later
            }
            continue;
        }        

        else if(firstLine) {
            firstLine = False;
            setSiteInfo(fci, line);
            scanHeaderLine(fci);
        }
        
        numFields = split(line, fields, MAX_FIELDS, Delimiter);  /* split line */  
        if(numFields < 100) {
            sprintf(ErrStr, "Scanning line %d of file %s, got %d columns using delimiter \"%s\".  Either delimiter flag or -s arg is wrong", fci->forecastLineNumber, fci->forecastTableFile.fileName, numFields, Delimiter);
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
                fprintf(fci->warningsFile.fp, "Warning: siteGroup changed from %s to %s\n", fci->siteGroup, fldPtr);
            }
        }
        
        // siteName
        fldPtr = fields[1];

        if(!fci->multipleSites && strcasecmp(fldPtr, fci->siteName) != 0) {
            fprintf(fci->warningsFile.fp, "Warning: siteName changed from %s to %s\n", fci->siteName, fldPtr);
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
                fprintf(fci->warningsFile.fp, "Warning: latitude changed from %.3f to %.3f\n", fci->lat, lat);
            }
            if(!fci->multipleSites && fabs(lon - fci->lon) > 0.01) {
                fprintf(fci->warningsFile.fp, "Warning: longitude changed from %.3f to %.3f\n", fci->lon, lon);
            }        
        }
        
        // compute hours after sunrise this date/time
        thisSample->sunrise = calculateSunrise(&currDate, fci->lat, fci->lon);  // Get the sunrise time for this day
        thisSample->hoursAfterSunrise = thisSample->dateTime.hour - thisSample->sunrise.hour + 1;
        if(thisSample->hoursAfterSunrise < 1)
            thisSample->hoursAfterSunrise += 24;
        
        (void) readDataFromLine(fci, fields);
        
/*
        if(fci->numValidSamples == 3)
            return True;
*/

        fci->numInputRecords++;
    }
    
    studyData(fci);
    
    return True;
}

void setSiteInfo(forecastInputType *fci, char *line)
{
    char *tempLine = strdup(line);
    int numFields;
    char *fields[MAX_FIELDS];
    char tempFileName[2048];
        
    // now grab siteName and set the site specific model info
    numFields = split(tempLine, fields, MAX_FIELDS, Delimiter);  /* split line */
    fci->siteName = strdup(fields[1]);
    registerSiteName(&fci->allSiteInfo[fci->numSites], fci->siteName);
    fci->thisSite = &fci->allSiteInfo[fci->numSites];
    
    // Now that we know the site name we can open the warnings file
    sprintf(tempFileName, "%s/%s.warnings.txt", fci->outputDirectory, fci->siteName);
    fci->warningsFile.fileName = strdup(tempFileName);
    if((fci->warningsFile.fp = fopen(fci->warningsFile.fileName, "w")) == NULL) {
        fprintf(stderr, "Couldn't open warnings file %s\n", fci->warningsFile.fileName);
        exit(1);
    }

    fci->numSites++;
    
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

#define DateTimeError { sprintf(ErrStr, "Bad date/time string in input, line %d: %s\n", fci->forecastLineNumber, origStr); FatalError("parseDateTime()", ErrStr); }
    
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

#define checkTooLow(X,Y) { if(thisSample->sunIsUp && thisSample->X < 1) { fprintf(fci->warningsFile.fp, "Warning: line %d: %s looks too low: %.1f\n", fci->forecastLineNumber, Y, thisSample->X); }}

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
        sprintf(ErrStr, "Got bad zenith angle at line %d: %.2f\n", fci->forecastLineNumber, thisSample->zenith);
        FatalError("readDataFromLine()", ErrStr);
    }
    
    thisSample->sunIsUp = (thisSample->zenith < 85);
    if(thisSample->sunIsUp)
        fci->numDaylightRecords++;
       
    thisSample->groundGHI = atof(fields[fci->columnInfo[fci->groundGHICol].inputColumnNumber]);
/*
    if(thisSample->sunIsUp && thisSample->groundGHI < 1)
        fprintf(stderr, "Warning: line %d: groundGHI looks too low: %.1f\n", fci->forecastLineNumber, thisSample->groundGHI);
*/
    
    if(thisSample->groundGHI < MIN_IRR || thisSample->groundGHI > MAX_IRR) {
        sprintf(ErrStr, "Got bad surface GHI at line %d: %.2f\n", fci->forecastLineNumber, thisSample->groundGHI);
        FatalError("readDataFromLine()", ErrStr);
    }
    //checkTooLow(groundGHI, "groundGHI");
    if(thisSample->sunIsUp && thisSample->groundGHI < 1)  
        fprintf(fci->warningsFile.fp, "Warning: line %d: %s looks too low: %.1f\n", fci->forecastLineNumber, "groundGHI", thisSample->groundGHI);
/*
    if(thisSample->groundGHI < MIN_GHI_VAL)
        thisSample->isValid = False;
*/
    
    thisSample->groundDNI = atof(fields[fci->columnInfo[fci->groundDNICol].inputColumnNumber]);
    if(thisSample->groundDNI < MIN_IRR || thisSample->groundDNI > MAX_IRR) {
        sprintf(ErrStr, "Got bad surface DNI at line %d: %.2f\n", fci->forecastLineNumber, thisSample->groundDNI);
        FatalError("readDataFromLine()", ErrStr);
    }
    
    thisSample->groundDiffuse = atof(fields[fci->columnInfo[fci->groundDiffuseCol].inputColumnNumber]);
    if(thisSample->groundDiffuse < MIN_IRR || thisSample->groundDiffuse > MAX_IRR) {
        sprintf(ErrStr, "Got bad surface diffuse at line %d: %.2f\n", fci->forecastLineNumber, thisSample->groundDiffuse);
        FatalError("readDataFromLine()", ErrStr);
    }  
    
    thisSample->clearskyGHI = atof(fields[fci->columnInfo[fci->clearskyGHICol].inputColumnNumber]);
    if(thisSample->clearskyGHI < MIN_IRR || thisSample->clearskyGHI > MAX_IRR) {
        sprintf(ErrStr, "Got bad clearsky GHI at line %d: %.2f\n", fci->forecastLineNumber, thisSample->clearskyGHI);
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
        sprintf(ErrStr, "Got bad surface temperature at line %d: %.2f\n", fci->forecastLineNumber, thisSample->groundTemp);
        FatalError("readDataFromLine()", ErrStr);
    }
*/
    
    thisSample->groundWind = atof(fields[10]);
/*
    if(thisSample->groundWind < -90 || thisSample->groundWind > 90) {
        sprintf(ErrStr, "Got bad surface wind at line %d: %.2f\n", fci->forecastLineNumber, thisSample->groundWind);
        FatalError("readDataFromLine()", ErrStr);
    }
*/
    
    thisSample->groundRH = atof(fields[11]);
/*
    if(thisSample->groundRH < -1 || thisSample->groundRH > 110) {
        sprintf(ErrStr, "Got bad surface relative humidity at line %d: %.2f\n", fci->forecastLineNumber, thisSample->groundRH);
        FatalError("readDataFromLine()", ErrStr);
    }
*/
     
    
    int columnIndex, modelIndex, hourIndex;
    for(columnIndex=fci->startModelsColumnNumber; columnIndex<fci->numColumnInfoEntries; columnIndex++) {
        hourIndex = fci->columnInfo[columnIndex].hoursAheadIndex;
        modelIndex = fci->columnInfo[columnIndex].modelIndex;
/*
        if(strcmp(fci->columnInfo[columnIndex].columnName, "ncep_HRRR_DSWRF_1") == 0)
            printf("hi\n");
*/
        if(strlen(fields[fci->columnInfo[columnIndex].inputColumnNumber]) < 1) {  // no data present
            //fprintf(stderr, "%s line %d: input col = %d, data = %s\n", fci->columnInfo[columnIndex].columnName, fci->forecastLineNumber, fci->columnInfo[columnIndex].inputColumnNumber, fields[fci->columnInfo[columnIndex].inputColumnNumber]);
            thisSample->modelData[hourIndex].modelGHI[modelIndex] = -999;
        }
        else {
            thisSample->modelData[hourIndex].modelGHI[modelIndex] = atof(fields[fci->columnInfo[columnIndex].inputColumnNumber]);
        }
        //fprintf(stderr, "modelDesc=%s,columnInfoIndex=%d,inputCol=%d,hourInd=%d,modelIndex=%d,scanVal=%s,atofVal=%.1f\n", fci->columnInfo[columnIndex].columnName, columnIndex, fci->columnInfo[columnIndex].inputColumnNumber, 
        //        hourIndex, modelIndex, fields[fci->columnInfo[columnIndex].inputColumnNumber],thisSample->modelData[hourIndex].modelGHI[modelIndex]);
        //if(thisSample->modelGHIvalues[modelIndex] < MIN_GHI_VAL)
        //    thisSample->isValid = False;
        //if(thisSample->sunIsUp && thisSample->modelData[hourIndex].modelGHI[modelIndex] < 1) { 
        //    fprintf(stderr, "Warning: line %d: %s looks too low: %.1f\n", fci->forecastLineNumber, fci->hoursAheadGroup[hourIndex].hourlyModelStats[modelIndex].columnName, thisSample->modelData[hourIndex].modelGHI[modelIndex]); 
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
    
#ifdef DUMP_ALL_DATA
        fprintf(FilteredDataFp, "%s,%.2f,%.0f,%.0f,%.0f,%.0f", dtToStringCsv2(&thisSample->dateTime), thisSample->zenith, thisSample->groundGHI, thisSample->groundDNI, thisSample->groundDiffuse, thisSample->clearskyGHI);
        for(columnIndex=fci->startModelsColumnNumber; columnIndex<fci->numColumnInfoEntries; columnIndex++) {        
            hourIndex = fci->columnInfo[columnIndex].hoursAheadIndex;
            modelIndex = fci->columnInfo[columnIndex].modelIndex;
            fprintf(FilteredDataFp, ",%.1f", thisSample->modelData[hourIndex].modelGHI[modelIndex]);
    }
    fprintf(FilteredDataFp, "\n");
#endif
    
    return True;
}

void studyData(forecastInputType *fci)
{
    timeSeriesType *thisSample;
    modelStatsType *thisModelErr;
    int i, columnIndex, modelIndex, hourIndex, daylightData=0;
    
    for(i=0; i<fci->numTotalSamples; i++) {
        thisSample = &(fci->timeSeries[i]);
        if(thisSample->sunIsUp) {
            for(columnIndex=fci->startModelsColumnNumber; columnIndex<fci->numColumnInfoEntries; columnIndex++) {            
                    daylightData++;
                    hourIndex = fci->columnInfo[columnIndex].hoursAheadIndex;
                    modelIndex = fci->columnInfo[columnIndex].modelIndex;
                    thisModelErr = &fci->hoursAheadGroup[hourIndex].hourlyModelStats[modelIndex];

                    if(thisSample->modelData[hourIndex].modelGHI[modelIndex] == -999) {
                        fci->columnInfo[columnIndex].numMissing++;
                    }
                    else {
                        fci->columnInfo[columnIndex].numGood++;
                    }
                }
        }
    }
    
    if(daylightData < 1) {
        FatalError("studyData()", "Got no daylight data scanning input.");
    }
    
    fprintf(fci->warningsFile.fp, "======== Scanning Forecast Data For Active Models =========\n");
    for(columnIndex=fci->startModelsColumnNumber; columnIndex<fci->numColumnInfoEntries; columnIndex++) { 
        hourIndex = fci->columnInfo[columnIndex].hoursAheadIndex;
        modelIndex = fci->columnInfo[columnIndex].modelIndex;
/*
        if(fci->columnInfo[columnIndex].numGood < 1)
            fci->columnInfo[columnIndex].percentMissing = 100;
        else
*/
            fci->columnInfo[columnIndex].percentMissing = fci->columnInfo[columnIndex].numMissing/(fci->columnInfo[columnIndex].numMissing + fci->columnInfo[columnIndex].numGood) * 100.0;

            if(fci->columnInfo[columnIndex].percentMissing < 100 && fci->columnInfo[columnIndex].percentMissing > 0) {
                fprintf(stderr, "Warning: %s is neither completely on nor off in the input forecast data (%.0f%% missing)\n", fci->columnInfo[columnIndex].columnName, fci->columnInfo[columnIndex].percentMissing);
            }
            if(fci->columnInfo[columnIndex].percentMissing > 90) {
                fci->hoursAheadGroup[hourIndex].hourlyModelStats[modelIndex].isActive = False;
                fprintf(fci->warningsFile.fp, "%35s : off\n", fci->columnInfo[columnIndex].columnName); //, hoursAheadIndex = %d\n", fci->columnInfo[columnIndex].columnName, modelIndex, hourIndex);
                //fprintf(fci->warningsFile.fp, "Warning: disabling model %s : %.0f%% data missing : model index = %d hour index = %d\n", fci->columnInfo[columnIndex].columnName, fci->columnInfo[columnIndex].percentMissing, modelIndex, hourIndex); //, hoursAheadIndex = %d\n", fci->columnInfo[columnIndex].columnName, modelIndex, hourIndex);
            }
            else {
                fci->hoursAheadGroup[hourIndex].hourlyModelStats[modelIndex].isActive = True;
                fprintf(fci->warningsFile.fp, "%35s : on\n", fci->columnInfo[columnIndex].columnName); //, hoursAheadIndex = %d\n", fci->columnInfo[columnIndex].columnName, modelIndex, hourIndex);
            }
            
            // use isUsable (in optimization stage) to signify isActive and not isReference forecast model (such as persistence)
            fci->hoursAheadGroup[hourIndex].hourlyModelStats[modelIndex].isUsable = fci->hoursAheadGroup[hourIndex].hourlyModelStats[modelIndex].isActive 
                                        && !fci->hoursAheadGroup[hourIndex].hourlyModelStats[modelIndex].isReference;

        //fprintf(stderr, "%s : column index = %d : model index = %d : hour index = %d : missing = %.0f%%\n", fci->columnInfo[columnIndex].columnName, columnIndex, fci->columnInfo[columnIndex].modelIndex, fci->columnInfo[columnIndex].hoursAheadIndex, fci->columnInfo[columnIndex].percentMissing);
        
        // disable models if percentMissing < threshold
    }
    fprintf(fci->warningsFile.fp, "===========================================================\n");   
  
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
    int i;
    size_t size;
    
    fci->lat = -999;
    fci->lon = -999;
    fci->siteGroup = NULL;
    fci->siteName = NULL;
    fci->numModels = 0;
    fci->numSites = 0;
    fci->maxModelIndex = 0;
    fci->numTotalSamples = 0;
    allocatedSamples = 8670 * MAX_MODELS;
    size = allocatedSamples * sizeof(timeSeriesType);
    if((fci->timeSeries = (timeSeriesType *) malloc(size)) == NULL) {
        FatalError("initForecastInfo()", "memory alloc error");
    }
    fci->numColumnInfoEntries = 0;
    fci->numInputRecords = 0;
    fci->numDaylightRecords = 0;

    for(i=0; i<MAX_HOURS_AHEAD; i++) {
        fci->hoursAheadGroup[i].hoursAhead = -1;       
    }
    
    fci->forecastLineNumber = 0;
}

void incrementTimeSeries(forecastInputType *fci)
{
    fci->numTotalSamples++;
    if(fci->numTotalSamples == allocatedSamples) {
        allocatedSamples *= 2;
        fci->timeSeries = (timeSeriesType *) realloc(fci->timeSeries, allocatedSamples * sizeof(timeSeriesType));
    }
}

void scanHeaderLine(forecastInputType *fci)
{
    //fci->maxModelIndex = 15;       
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
    registerColumnInfo(fci, "sr_zen", "zenith angle", 0, 0);
    fci->groundGHICol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "sr_global", "ground GHI", 1, 0);   
    fci->groundDNICol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "sr_direct", "ground DNI", 0, 0);   
    fci->groundDiffuseCol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "sr_diffuse", "ground diffuse", 0, 0);   
    fci->satGHICol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "sat_ghi", "sat model GHI", 1, 0);   
    fci->clearskyGHICol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "clear_ghi", "clearsky GHI", 0, 0);  
    
    fci->startModelsColumnNumber = fci->numColumnInfoEntries;

    if((fci->descriptionFile.fp = fopen(fci->descriptionFile.fileName, "r")) == NULL) {
        sprintf(ErrStr, "Couldn't open model description file \"%s\"", fci->descriptionFile.fileName);
        FatalError("scanHeaderLine()", ErrStr);
    }

    fci->numModels = 0;
    
    // Now we open up the models description file
    char line[LINE_LENGTH], saveLine[LINE_LENGTH];
    char *fields[MAX_FIELDS], *modelName, *modelDesc;
    int i, numFields, isReference, hoursAheadColMap[64],thisHour;
    double weight;
    int modelStartColumn = 3; 
            
    fci->descriptionFile.lineNumber = 0;
    
    // Need to find a way to generate this file too
    while(fgets(line, LINE_LENGTH, fci->descriptionFile.fp)) {
        fci->descriptionFile.lineNumber++;
        fprintf(stderr, "line %d:%s\n", fci->descriptionFile.lineNumber, line);
        if(fci->descriptionFile.lineNumber == 1)  // this is just an informational line
            continue;
        if(fci->descriptionFile.lineNumber == 2) {  // this is the header line with (possibly) hours ahead info in it
                strcpy(saveLine, line);
                // #Model description file for forecastOpt
                // #modelName           modelDesc                       isReferenceModel	HA_1 weights	HA_2 weights	HA_12 weights
                // ncep_NAM_DSWRF_	 "NAM Low Res Instant GHI"	 0                      0.1             0.2             0.1
                numFields = split(line, fields, MAX_FIELDS, ",\t");  /* split line */
                if(numFields > modelStartColumn) {
                    fci->runOptimizer = False;  // we want to use a weighting scheme that's been provided
                    for(i=3; i<numFields; i++) {
                        hoursAheadColMap[i] = parseNumberFromString(fields[i]);  // HA_1 weights => 1
                        if(hoursAheadColMap[i] < 1 || hoursAheadColMap[i] > MAX_HOURS_AHEAD) {
                            sprintf(ErrStr, "Something's fishy about header line %d in %s: can't parse hours ahead number out of column %d in header line.\nline = %s\n",
                                    fci->descriptionFile.lineNumber, fci->descriptionFile.fileName, i+1, saveLine);
                            FatalError("scanHeaderLine()", ErrStr);
                        }
                    }
                }
                else
                    fci->runOptimizer = True; // only model names, desc, isRef were provided -- fire up the optimizer
        }
        else {
            stripComment(line);
            // split by space then by =
            numFields = split(line, fields, MAX_FIELDS, ",\t");  /* split line */
            if(numFields == 0)
                continue;
            if((modelName = stripQuotes(fields[0])) == NULL )
                FatalError("scanHeaderLine()", ErrStr);
            if((modelDesc = stripQuotes(fields[1])) == NULL) 
                FatalError("scanHeaderLine()", ErrStr);
            isReference = atoi(fields[2]);
            
            registerColumnInfo(fci, modelName, modelDesc, isReference, 1);    // register this model as one we want to use

            if(numFields > modelStartColumn) {
                    for(i=modelStartColumn; i<numFields; i++) {
                        thisHour = hoursAheadColMap[i];
                        if(thisHour < 1 || thisHour > 500) {
                            sprintf(ErrStr, "Internal error keeping track of current hour ahead while parsing model config file %s, line %d: hours ahead for column %d = %d\n",
                                    fci->descriptionFile.fileName, fci->descriptionFile.lineNumber, i+1, thisHour);
                            FatalError("scanHeaderLine()", ErrStr);
                        }
                        weight = atof(fields[i]);
                        if(weight < -0.5 || weight > 1.5) {
                            sprintf(ErrStr, "Bad weight while parsing model config file %s, line %d: hours ahead for column %d, weight = %f\n", 
                                    fci->descriptionFile.fileName, fci->descriptionFile.lineNumber, i+1, weight);
                            FatalError("scanHeaderLine()", ErrStr);                           
                        }
                        // now we need to set the weight and isUsable flags for the current modelIndex and hourIndex
                        // but this is best done when we're finished reading in the forecast table
                        //fci->hoursAheadGroup[i].hourlyModelStats[modelIndex].optimizedWeightPass2 = weight;
                        
                    }                
            }
        }
    }

    if(fci->startHourLowIndex == -1) {
        fci->startHourLowIndex = 0;
        fci->startHourHighIndex = fci->maxModelIndex;
    }

    fclose(fci->descriptionFile.fp);
}

char *stripQuotes(char *str)
{
    char *q, *p = str;
    char origStr[1024];
    int firstQuote = 1;
    
    strcpy(origStr, str);
    while(*p != '\0') {
        if(*p == '"' || *p == '\'') {
            if(firstQuote) {
                q = p+1;
                firstQuote = 0;
            }
            else {
                *p = '\0';
                return q;
            }
        }
        p++;
    }
    
    sprintf(ErrStr, "got unterminated quote string: %s", origStr);
    return NULL;
}

// Everything after the '#' gets stripped out
void stripComment(char *str)
{
    char *p = str;

    while(*p) {
        if(*p == '#') {
            *p = '\0';
            return;
        }
        p++;
    }
}

// look for positive integers in a string
// only returns the first one found
int parseNumberFromString(char *str)
{
    char *q, *p = str;
    char origStr[1024], inNumber = 0;
    
    // HA_12 weights
    strcpy(origStr, str);
    while(*p != '\0') {
        if(isdigit(*p)) {
            if(!inNumber) {
                q = p;
                inNumber = 1;
            }
            // otherwise just continue along
        }
        else if(inNumber) {
            *p = '\0';
            break;
        }
        p++;
    }
    
    if(inNumber)
        return atoi(q);
    
    //sprintf(ErrStr, "couldn't find number in string: %s", origStr);
    return -1;
}


void registerColumnInfo(forecastInputType *fci, char *columnName, char *columnDescription, int isReference, int isForecast) 
{
    // We want to search the forecastHeaderLine for columnName and see how far out it goes
    // note that just because a measurement goes out to N hours ahead doesn't guarantee that there's 
    // good data out there.  This may be a sticky issue.
    static int numFields=-1, numGotModels=0;
    static char *fields[MAX_FIELDS];
    int i, hoursAhead, hoursAheadIndex=-1;
    char tempColDesc[1024], tempHeader[MAX_FIELDS * 64], *p;    

    if(numFields == -1) {
        strcpy(tempHeader, fci->forecastHeaderLine);
        numFields = split(tempHeader, fields, MAX_FIELDS, Delimiter);  /* split line just once */   
    }

    if(isForecast) {
        for(i=0; i<numFields; i++) {
            if(strncasecmp(fields[i], columnName, strlen(columnName)) == 0) { /* got a hit */
                p = fields[i] + strlen(fields[i]) - 1;
                while(*p != '_' && p > fields[i]) p--;
                p++;
                hoursAhead = atoi(p);
                hoursAheadIndex++;
                fci->columnInfo[fci->numColumnInfoEntries].hoursAheadIndex = hoursAheadIndex;   // this is passed as an arg
                fci->columnInfo[fci->numColumnInfoEntries].modelIndex = fci->numModels;
                sprintf(tempColDesc, "%s +%d hours", columnDescription, hoursAhead); //fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
                fci->columnInfo[fci->numColumnInfoEntries].columnName = strdup(fields[i]); //"ncep_RAP_DSWRF_1";                      
                fci->columnInfo[fci->numColumnInfoEntries].columnDescription = strdup(tempColDesc); //"NCEP RAP GHI";  
                fci->columnInfo[fci->numColumnInfoEntries].maxhoursAhead = hoursAhead;  // this will keep increasing
                fci->columnInfo[fci->numColumnInfoEntries].inputColumnNumber = i;  // this tells us the column numner of the input forecast table
                
                if(fci->hoursAheadGroup[hoursAheadIndex].hoursAhead != -1) {
                    if(fci->hoursAheadGroup[hoursAheadIndex].hoursAhead != hoursAhead) {
                        sprintf(ErrStr, "found inconsistency in number of hours ahead: for hours ahead index %d: it changed from %d hours to %d hours", hoursAheadIndex, fci->hoursAheadGroup[hoursAheadIndex].hoursAhead, hoursAhead);
                        FatalError("registerColumnInfo()", ErrStr);
                    }
                }
                else
                    fci->hoursAheadGroup[hoursAheadIndex].hoursAhead = hoursAhead;
                
                // make hourErrorGroup links
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].isReference = isReference;
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].columnInfoIndex = fci->numColumnInfoEntries;
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].columnName = fci->columnInfo[fci->numColumnInfoEntries].columnName;
                fci->maxModelIndex = MAX(fci->maxModelIndex, hoursAheadIndex);  // the max hoursAheadIndex is the number of hour groups
                //fprintf(stderr, "registering %s, data col index = %d, input col = %d, hour index = %d\n", fields[i], fci->numColumnInfoEntries-1, i, hoursAheadIndex);
                
                numGotModels++;
                fci->numColumnInfoEntries++;
            } 
        }
        if(i == numFields && numGotModels == 0) {
            sprintf(ErrStr, "model name %s not found in header line", columnName);
            FatalError("registerColumnInfo()", ErrStr);
        }
        fci->numModels++;
    }

    // not a forecast but we still need to set a few things
    else {
        for(i=0; i<numFields; i++) {
            if(strncasecmp(fields[i], columnName, strlen(columnName)) == 0) { /* got a hit */
                numGotModels = 1;
                fci->columnInfo[fci->numColumnInfoEntries].columnName = strdup(columnName); // ;                      
                fci->columnInfo[fci->numColumnInfoEntries].columnDescription = strdup(columnDescription); //"NCEP RAP GHI";  
                fci->columnInfo[fci->numColumnInfoEntries].maxhoursAhead = 0;
                fci->columnInfo[fci->numColumnInfoEntries].hoursAheadIndex = -1;  
                fci->columnInfo[fci->numColumnInfoEntries].modelIndex = -1;
                fci->columnInfo[fci->numColumnInfoEntries].inputColumnNumber = i;  // this tells us the column numner of the input forecast table
/*
                fci->hoursAheadGroup[0].hourlyModelStats[fci->numModels].isReference = isReference;
                fci->hoursAheadGroup[0].hourlyModelStats[fci->numModels].columnInfoIndex = fci->numColumnInfoEntries;
                fci->hoursAheadGroup[0].hourlyModelStats[fci->numModels].columnName = fci->columnInfo[fci->numColumnInfoEntries].columnName;
*/
                fci->numColumnInfoEntries++;
                //fprintf(stderr, "registering non-model %s, data index = %d, hour index = %d, input col = %d\n", fields[i], fci->numColumnInfoEntries-1, hoursAheadIndex, i);
            }
        }
        if(i == numFields && numGotModels == 0) {
            sprintf(ErrStr, "measuremnet name %s not found in header line", columnName);
            FatalError("registerColumnInfo()", ErrStr);
        }
    }
}

void oldregisterColumnInfo(forecastInputType *fci, char *columnName, char *columnDescription, int maxhoursAhead, int hourIndex, int isReference) 
{
    char tempColName[1024], tempColDesc[1024];    
    
    if(hourIndex >= 0) {
        if(!checkModelAgainstSite(fci, columnName)) // if this forecast model isn't on the site's model list, don't register it
            return;
        
        fci->hoursAheadGroup[hourIndex].hourlyModelStats[fci->numModels].isReference = isReference;
        fci->columnInfo[fci->numColumnInfoEntries].hoursAheadIndex = hourIndex;   // this is passed as an arg
        fci->columnInfo[fci->numColumnInfoEntries].modelIndex = fci->numModels;
        sprintf(tempColName, "%s%d", columnName, fci->hoursAheadGroup[hourIndex].hoursAhead);
        sprintf(tempColDesc, "%s +%d hours", columnDescription, fci->hoursAheadGroup[hourIndex].hoursAhead);
        fci->columnInfo[fci->numColumnInfoEntries].columnName = strdup(tempColName); //"ncep_RAP_DSWRF_1";                      
        fci->columnInfo[fci->numColumnInfoEntries].columnDescription = strdup(tempColDesc); //"NCEP RAP GHI";  
        fci->columnInfo[fci->numColumnInfoEntries].maxhoursAhead = maxhoursAhead;
        fci->columnInfo[fci->numColumnInfoEntries].numGood = 0;
        fci->columnInfo[fci->numColumnInfoEntries].numMissing = 0;
        fci->numColumnInfoEntries++;
        fci->numModels++;
    }
    else {
        fci->columnInfo[fci->numColumnInfoEntries].columnName = columnName; //"ncep_RAP_DSWRF_1";                      
        fci->columnInfo[fci->numColumnInfoEntries].columnDescription = columnDescription; //"NCEP RAP GHI";  
        fci->columnInfo[fci->numColumnInfoEntries].maxhoursAhead = maxhoursAhead;
        fci->columnInfo[fci->numColumnInfoEntries].hoursAheadIndex = -1;  
        fci->columnInfo[fci->numColumnInfoEntries].modelIndex = -1;
        fci->numColumnInfoEntries++;
    }
}

int checkModelAgainstSite(forecastInputType *fci, char *modelName)
{
    int modelNum;
    
    //fprintf(stderr, "checking %s: number of models = %d\n", modelName, fci->thisSite->numModels);

    for(modelNum=0; modelNum<fci->thisSite->numModels; modelNum++) {
        //fprintf(stderr, "checking %s against %s\n", modelName, fci->thisSite->modelNames[modelNum]);
        if(strcmp(modelName, fci->thisSite->modelNames[modelNum]) == 0) {
            return True;
        }
    } 
    
    fprintf(stderr, "Note: model %s not turned on for site %s\n", modelName, fci->siteName);
    return False;
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

void parseColumnInfo(forecastInputType *fci)
{
    int numFields;
    char *fields[MAX_FIELDS];
    int i, j, matches=0;
    
    //fprintf(stderr, "Line:%s\n", forecastHeaderLine);
    
    scanHeaderLine(fci);
    //getNumberOfHoursAhead(fci, forecastHeaderLine);
    
    numFields = split(fci->forecastHeaderLine, fields, MAX_FIELDS, Delimiter);  /* split line */   
    
    for(i=0; i<numFields; i++) {
        //fprintf(stderr, "Checking in column name %s...\n", fields[i]);
        for(j=0; j<fci->numColumnInfoEntries; j++) {
            //fprintf(stderr, "%s\n", fci->columnInfo[j].columnName);

            if(strcasecmp(fields[i], fci->columnInfo[j].columnName) == 0) { // || strcasecmp(fields[i], fci->columnInfo[j].columnDescription) == 0) {
                fci->columnInfo[j].inputColumnNumber = i;
                //fprintf(stderr, "%s\n", fci->columnInfo[j].columnName);
                matches++;
                break;
            }
        }
        if(j == fci->numColumnInfoEntries) fprintf(stderr, "no match for %s...\n", fields[i]);
    }

    for(i=0; i<fci->numColumnInfoEntries; i++) {
        if(fci->columnInfo[i].modelIndex >= 0) {
            fci->hoursAheadGroup[fci->columnInfo[i].hoursAheadIndex].hourlyModelStats[fci->columnInfo[i].modelIndex].columnInfoIndex = i;
            fci->hoursAheadGroup[fci->columnInfo[i].hoursAheadIndex].hourlyModelStats[fci->columnInfo[i].modelIndex].columnName = fci->columnInfo[i].columnName;
        }
        //fprintf(stderr, "[%d] col=%d, label=%s desc=\"%s\" modelnumber=%d modelData=%d\n", i, fci->columnInfo[i].inputColumnNumber, fci->columnInfo[i].columnName, fci->columnInfo[i].columnDescription,
        //                fci->columnInfo[i].modelIndex, fci->columnInfo[i].hoursAheadIndex);
    }
    
    for(i=0; i<fci->numColumnInfoEntries; i++) {
        if(fci->columnInfo[i].inputColumnNumber < 0)
            fprintf(fci->warningsFile.fp, "No match for expected column %s\n", fci->columnInfo[i].columnName);
/*
        else
            fprintf(stderr, "Got column %s\n", fci->columnInfo[i].columnName);
*/
    }

    if(matches < fci->numColumnInfoEntries) {
        sprintf(ErrStr, "Scanning line %d, only got %d out of %d matches with expected fields", fci->forecastLineNumber, matches, fci->numColumnInfoEntries);
        FatalError("parseColumnInfo()", ErrStr);
    }
    
    //for(i=)
 
}

void runErrorAnalysis(forecastInputType *fci) 
{
    int hourIndex;
    
    for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
        fprintf(stderr, "\n############ Running for hour ahead %d\n\n", fci->hoursAheadGroup[hourIndex].hoursAhead);
        doErrorAnalysis(fci, hourIndex);
        dumpNumModelsReportingTable(fci);
        printRmseTableHour(fci, hourIndex);
        printHourlySummary(fci, hourIndex);
        //runOptimizer(fci, hourIndex);
        runOptimizerNested(fci, hourIndex);
        fprintf(stderr, "\n############ End hour ahead %d\n", fci->hoursAheadGroup[hourIndex].hoursAhead);

        dumpWeightedTimeSeries(fci,hourIndex);
    }
    

//    printByHour(fci);
//    printByModel(fci);
    printByAnalysisType(fci);
    printSummaryCsv(fci);
    
    if(fci->runHoursAfterSunrise) {
        
    }
}

void printByHour(forecastInputType *fci)
{
    FILE *fp;
    char fileName[1024];
    int modelIndex, hourIndex;
    modelErrorType *modelData;
    modelStatsType *err;
         
    for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
        modelData = &fci->hoursAheadGroup[hourIndex];

        sprintf(fileName, "%s/%s.forecast.error.hoursAhead=%02d.csv", fci->outputDirectory, fci->siteName, modelData->hoursAhead);
        //fprintf(stderr, "hour[%d] hour=%d file=%s\n", hourIndex, modelData->hoursAhead, fileName);
        fp = fopen(fileName, "w");

        fprintf(fp, "#siteName=%s,lat=%.2f,lon=%.3f,hours ahead=%d,N=%d,mean measured GHI=%.1f\n",fci->siteName, fci->lat, fci->lon, modelData->hoursAhead, modelData->numValidSamples, modelData->meanMeasuredGHI);
        fprintf(fp, "#model,sum( model-ground ), sum( abs(model-ground) ), sum( (model-ground)^2 ), mae, mae percent, mbe, mbe percent, rmse, rmse percent\n");

        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
            err = &modelData->hourlyModelStats[modelIndex];
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
    modelErrorType *modelData;
    modelStatsType *err;
    
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {   
        sprintf(fileName, "%s/%s.forecast.error.model=%s.csv", fci->outputDirectory, fci->siteName, getGenericModelName(fci, modelIndex));
        fp = fopen(fileName, "w");


        fprintf(fp, "#siteName=%s,lat=%.2f,lon=%.3f,error analysis=%s\n",fci->siteName, fci->lat, fci->lon, getGenericModelName(fci, modelIndex));
        fprintf(fp, "#model,N,hours ahead, mean measured GHI, sum( model-ground ), sum( abs(model-ground) ), sum( (model-ground)^2 ), mae, mae percent, mbe, mbe percent, rmse, rmse percent\n");

            for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
                modelData = &fci->hoursAheadGroup[hourIndex];

                err = &modelData->hourlyModelStats[modelIndex];
                fprintf(fp, "%s,%d,%d,%.1f,%.0f,%.0f,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", fci->columnInfo[err->columnInfoIndex].columnDescription, 
                    modelData->numValidSamples,fci->hoursAheadGroup[hourIndex].hoursAhead,modelData->meanMeasuredGHI,err->sumModel_Ground, err->sumAbs_Model_Ground, err->sumModel_Ground_2,
                    err->mae, err->maePct*100, err->mbe, err->mbePct*100, err->rmse, err->rmsePct*100);
        }
        fclose(fp);
    }    
}

void printByAnalysisType(forecastInputType *fci)
{
    FILE *fp;   
    int modelIndex, hourIndex;
    modelErrorType *modelData;
    modelStatsType *err;
    
    if(!(fp = openErrorTypeFile(fci, "MAE")))
        return;
    
    for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
        modelData = &fci->hoursAheadGroup[hourIndex];
        fprintf(fp, "%d,%d", modelData->hoursAhead, modelData->numValidSamples);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {          
            err = &modelData->hourlyModelStats[modelIndex];
            fprintf(fp, ",%.1f%%", err->maePct*100);
        }
        fprintf(fp, "\n");
    }    
    fclose(fp);   

    if(!(fp = openErrorTypeFile(fci, "MBE")))
        return;
    
    for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
        modelData = &fci->hoursAheadGroup[hourIndex];
        fprintf(fp, "%d,%d", modelData->hoursAhead, modelData->numValidSamples);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {          
            err = &modelData->hourlyModelStats[modelIndex];
            fprintf(fp, ",%.1f%%", err->mbePct*100);
        }
        fprintf(fp, "\n");
    }    
    fclose(fp);   

    if(!(fp = openErrorTypeFile(fci, "RMSE")))
        return;
    
    for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
        modelData = &fci->hoursAheadGroup[hourIndex];
        fprintf(fp, "%d,%d", modelData->hoursAhead, modelData->numValidSamples);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {          
            err = &modelData->hourlyModelStats[modelIndex];
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
    modelErrorType *modelData = &fci->hoursAheadGroup[hourIndex];
    
    if(!(fp = openErrorTypeFileHourly(fci, "RMSE", hourIndex)))
       return;
    //fp = stderr;
    
    //for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
        fprintf(fp, "hours ahead = %d\nN = %d\n", modelData->hoursAhead, modelData->numValidSamples);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {          
            if(modelData->hourlyModelStats[modelIndex].isActive)
                fprintf(fp, "%-35s = %.1f%%\n", getGenericModelName(fci, modelIndex), modelData->hourlyModelStats[modelIndex].rmsePct*100);
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
//    modelErrorType *modelData = &fci->hoursAheadGroup[hourIndex];
    
    if(fci->outputDirectory == NULL || fci->siteName == NULL) {
        fprintf(stderr, "openErrorTypeFile(): got null outputDirectory or siteName\n");
        return NULL;
    }
    
    if(strcasecmp(analysisType, "rmse") == 0)
        sprintf(satGHIerr, "sat GHI RMSE=%.1f%%", fci->hoursAheadGroup[hourIndex].satModelStats.rmsePct * 100);
    else if(strcasecmp(analysisType, "mae") == 0)
        sprintf(satGHIerr, "sat GHI MAE=%.1f%%", fci->hoursAheadGroup[hourIndex].satModelStats.maePct * 100);
    else 
        sprintf(satGHIerr, "sat GHI MBE=%.1f%%", fci->hoursAheadGroup[hourIndex].satModelStats.mbePct * 100);

    
    sprintf(fileName, "%s/%s.forecast.analysisType=%s.csv", fci->outputDirectory, fci->siteName, analysisType);
    //fp = fopen(fileName, "w");
    fp = stderr;
    
    fprintf(fp, "siteName=%s\nlat=%.2f\nlon=%.3f\nanalysisType=%s\n%s\n",fci->siteName, fci->lat, fci->lon, analysisType, satGHIerr);
/*
    fprintf(fp, "hours ahead,N,");
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++)  {
        if(modelData->hourlyModelStats[modelIndex].isActive)
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
        sprintf(satGHIerr, "sat GHI RMSE=%.1f%%", fci->hoursAheadGroup[0].satModelStats.rmsePct * 100);
    else if(strcasecmp(analysisType, "mae") == 0)
        sprintf(satGHIerr, "sat GHI MAE=%.1f%%", fci->hoursAheadGroup[0].satModelStats.maePct * 100);
    else 
        sprintf(satGHIerr, "sat GHI MBE=%.1f%%", fci->hoursAheadGroup[0].satModelStats.mbePct * 100);

    
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
    int columnIndex = fci->hoursAheadGroup[0].hourlyModelStats[modelIndex].columnInfoIndex;
    return (fci->columnInfo[columnIndex].maxhoursAhead);
}

char *getGenericModelName(forecastInputType *fci, int modelIndex)
{
    static char modelDesc[1024];
    
    if(modelIndex < 0)
        return("satellite");
    strcpy(modelDesc, fci->hoursAheadGroup[0].hourlyModelStats[modelIndex].columnName);  // should be something like ncep_RAP_DSWRF_1
    modelDesc[strlen(modelDesc)-2] = '\0';   // lop off the _1 => ncep_RAP_DSWRF

    return(modelDesc);
}

char *getColumnNameByHourModel(forecastInputType *fci, int hrInd, int modInd)
{
    int col;
    static char modelDesc[1024];
    
    // this should probably be solved in a data structure
    for(col=0; col<fci->numColumnInfoEntries; col++) {
            if(fci->columnInfo[col].hoursAheadIndex == hrInd && fci->columnInfo[col].modelIndex == modInd) {
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

        //fprintf(stderr, "hour[%d] = %d\n", fci->maxModelIndex, hour);
        
        fci->hoursAheadGroup[fci->maxModelIndex].hoursAhead = hour;
        fci->maxModelIndex++;
        
        currPtr = q+1;
    }
    
    if(fci->maxModelIndex < 3) {
        FatalError("getNumberOfHoursAhead()", "problem getting number of hours ahead");
    }
    
    if(fci->startHourLowIndex == -1) {
        fci->startHourLowIndex = 0;
        fci->startHourHighIndex = fci->maxModelIndex;
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

#define getModelN(modelIndex) (modelIndex < 0 ? (modelData->satModelStats.N) : (modelData->hourlyModelStats[modelIndex].N))

void printHourlySummary(forecastInputType *fci, int hourIndex)
{
    int modelIndex, hoursAhead = fci->hoursAheadGroup[hourIndex].hoursAhead;
    modelErrorType *modelData = &fci->hoursAheadGroup[hourIndex];
    
    fprintf(stderr, "\nHR%d=== Summary for hour %d (number of good samples) ===\n", hoursAhead, hoursAhead);
    fprintf(stderr, "HR%d\t%-40s : %d\n", hoursAhead, "N for group", modelData->numValidSamples);
    fprintf(stderr, "HR%d\t%-40s : %d\n", hoursAhead, "ground GHI", modelData->ground_N);
    for(modelIndex=-1; modelIndex < fci->numModels; modelIndex++) {
        if(modelIndex < 0 || modelData->hourlyModelStats[modelIndex].isActive)
            fprintf(stderr, "HR%d\t%-40s : %d\n", hoursAhead, getGenericModelName(fci, modelIndex), getModelN(modelIndex));
    }
}
    
void printSummaryCsv(forecastInputType *fci)
{
    int hourIndex, modelIndex;
    modelErrorType *modelData;
    char modelName[1024], tempFileName[2048];

    
    //sprintf(filename, "%s/%s.wtRange=%.2f-%.2f_ha=%d-%d.csv", fci->outputDirectory, fci->siteName, fci->weightSumLowCutoff, fci->weightSumHighCutoff, fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead);
    sprintf(tempFileName, "%s/forecastSummary.%s.%s_%s_hours=%d-%d.csv", fci->outputDirectory, fci->siteName, dtToStringFilename(&fci->startDate), dtToStringFilename(&fci->endDate), fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead);
    fci->summaryFile.fileName = strdup(tempFileName);
    
    if((fci->summaryFile.fp = fopen(fci->summaryFile.fileName, "w")) == NULL) {
        sprintf(ErrStr, "Couldn't open file %s\n", fci->summaryFile.fileName);
        FatalError("printSummaryCsv()", ErrStr);
    }
    // print the header
    fprintf(fci->summaryFile.fp, "#site=%s lat=%.3f lon=%.3f start date=%s end date=%s\n", fci->siteName, fci->lat, fci->lon, dtToStringFilename(&fci->startDate), dtToStringFilename(&fci->endDate));
    fprintf(fci->summaryFile.fp, "#hoursAhead,group N,sat RMSE,phase 1 RMSE,phase 2 RMSE,phase 1 RMSE calls,phase 2 RMSE calls");
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        strcpy(modelName, getGenericModelName(fci, modelIndex));
        fprintf(fci->summaryFile.fp, ",%s model, %s status,%s N,%s RMSE,%s %s,%s %s", modelName, modelName, modelName, modelName, modelName, WEIGHT_1_STR, modelName, WEIGHT_2_STR);
    }
    fprintf(fci->summaryFile.fp, "\n");

    // generate model results
    for(hourIndex=fci->startHourLowIndex; hourIndex <= fci->startHourHighIndex; hourIndex++) {
        modelData = &fci->hoursAheadGroup[hourIndex];
        fprintf(fci->summaryFile.fp, "%d,%d,%.2f,%.2f,%.2f,%ld,%ld", modelData->hoursAhead, modelData->numValidSamples, modelData->satModelStats.rmsePct * 100,
                modelData->optimizedRMSEphase1 * 100, modelData->optimizedRMSEphase2 * 100, modelData->phase1RMSEcalls, modelData->phase2RMSEcalls);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {           
            //if(modelData->hourlyModelStats[modelIndex].isActive) {
                fprintf(fci->summaryFile.fp, ",%s", modelData->hourlyModelStats[modelIndex].isReference ? "reference" : "forecast");
                fprintf(fci->summaryFile.fp, ",%s", modelData->hourlyModelStats[modelIndex].isActive ? "active" : "inactive");
                fprintf(fci->summaryFile.fp, ",%d", modelData->hourlyModelStats[modelIndex].isActive ? modelData->hourlyModelStats[modelIndex].N : -999);
                fprintf(fci->summaryFile.fp, ",%.2f", modelData->hourlyModelStats[modelIndex].isActive ? modelData->hourlyModelStats[modelIndex].rmsePct * 100 : -999);
                fprintf(fci->summaryFile.fp, ",%.2f", modelData->hourlyModelStats[modelIndex].isActive ? modelData->hourlyModelStats[modelIndex].optimizedWeightPass1 : -999);   
                fprintf(fci->summaryFile.fp, ",%.2f", modelData->hourlyModelStats[modelIndex].isActive ? modelData->hourlyModelStats[modelIndex].optimizedWeightPass2 : -999);   
            //}
        }
        fprintf(fci->summaryFile.fp, "\n");
    }
    
    fclose(fci->summaryFile.fp);
}

int runWeightedTimeSeriesAnalysis(forecastInputType *fci)
{
    
    initForecastInfo(fci);
        fprintf(stderr, "fci->timeSeries[0] = %p\n", &fci->timeSeries[0]);

    readForecastFile(fci);

    if(!readSummaryFile(fci)) 
        return False;
    
    return True;
}

// run as: -t -s 1 -a 2013062200,2013082500 -r0,15 -v -o /home/jim/mom/forecastOpt/output -w /home/jim/mom/forecastOpt/output/Sioux_Falls_SD.wtRange=0.95-1.05_ha=1-36.csv /var/www/html/kmh/data/adam/forecast.sioux_falls.csv

// read in forecastOpt summary file (i.e, hours ahead, weights and RMSE numbers from a previous run)
int readSummaryFile(forecastInputType *fci)
{
    FILE *fp;
    char line[LINE_LENGTH], saveLine[LINE_LENGTH];
    char siteStr[256], latStr[256], lonStr[256];
    char *fields[MAX_FIELDS], *fldPtr;
    int numFields, i;
    
    if((fp = fopen(fci->summaryFile.fileName, "r")) == NULL) {
        sprintf(ErrStr, "Couldn't open summary file %s : \"%s\"", fci->summaryFile.fileName, strerror(errno));
        FatalError("readSummaryFile()", ErrStr);
    }
    
/*
#site=Goodwin_Creek_MS lat=34.250 lon=-89.870
#hoursAhead,group N,sat RMSE,phase 1 RMSE,phase 2 RMSE,phase 1 RMSE calls,phase 2 RMSE calls,ncep_RAP_DSWRF model, ncep_RAP_DSWRF status,ncep_RAP_DSWRF N,ncep_RAP_DSWRF RMSE,ncep_RAP_DSWRF Weight 1,ncep_RAP_DSWRF weight 2,persistence model, persistence status,persistence N,persistence RMSE,persistence Weight 1,persistence weight 2,ncep_NAM_hires_DSWRF_inst model, ncep_NAM_hires_DSWRF_inst status,ncep_NAM_hires_DSWRF_inst N,ncep_NAM_hires_DSWRF_inst RMSE,ncep_NAM_hires_DSWRF_inst Weight 1,ncep_NAM_hires_DSWRF_inst weight 2,ncep_NAM_DSWRF model, ncep_NAM_DSWRF status,ncep_NAM_DSWRF N,ncep_NAM_DSWRF RMSE,ncep_NAM_DSWRF Weight 1,ncep_NAM_DSWRF weight 2,ncep_GFS_sfc_DSWRF_surface_avg model, ncep_GFS_sfc_DSWRF_surface_avg status,ncep_GFS_sfc_DSWRF_surface_avg N,ncep_GFS_sfc_DSWRF_surface_avg RMSE,ncep_GFS_sfc_DSWRF_surface_avg Weight 1,ncep_GFS_sfc_DSWRF_surface_avg weight 2,ncep_GFS_sfc_DSWRF_surface_inst model, ncep_GFS_sfc_DSWRF_surface_inst status,ncep_GFS_sfc_DSWRF_surface_inst N,ncep_GFS_sfc_DSWRF_surface_inst RMSE,ncep_GFS_sfc_DSWRF_surface_inst Weight 1,ncep_GFS_sfc_DSWRF_surface_inst weight 2,ncep_GFS_DSWRF model, ncep_GFS_DSWRF status,ncep_GFS_DSWRF N,ncep_GFS_DSWRF RMSE,ncep_GFS_DSWRF Weight 1,ncep_GFS_DSWRF weight 2,NDFD_global model, NDFD_global status,NDFD_global N,NDFD_global RMSE,NDFD_global Weight 1,NDFD_global weight 2,cm model, cm status,cm N,cm RMSE,cm Weight 1,cm weight 2,ecmwf_ghi model, ecmwf_ghi status,ecmwf_ghi N,ecmwf_ghi RMSE,ecmwf_ghi Weight 1,ecmwf_ghi weight 2
1,777,21.53,18.40,18.30,43758,73689,forecast,active,837,36.93,0.10,0.11,reference,active,836,27.03,0.00,0.00,forecast,active,837,38.62,0.00,0.00,forecast,active,846,40.64,0.00,0.00,forecast,active,837,32.50,0.00,0.00,forecast,active,820,32.35,0.10,0.07,forecast,active,830,32.30,0.00,0.00,forecast,active,829,38.35,0.00,0.00,forecast,active,807,19.65,0.80,0.84,forecast,active,838,30.11,0.00,0.00
2,778,21.59,23.02,22.98,43758,6672666,forecast,active,835,39.38,0.10,0.10,reference,active,836,32.59,0.00,0.00,forecast,active,840,38.81,0.10,0.07,forecast,active,845,41.29,0.00,0.00,forecast,active,837,32.27,0.00,0.00,forecast,active,820,32.23,0.10,0.13,forecast,active,830,32.10,0.00,0.00,forecast,active,829,38.60,0.00,0.00,forecast,active,803,25.83,0.60,0.60,forecast,active,838,30.01,0.10,0.11
*/

    // first comment line should look like this:
    // #site=Goodwin_Creek_MS lat=34.250 lon=-89.870
    fgets(line, LINE_LENGTH, fp);
    strcpy(saveLine, line);
    // split by space then by =
    numFields = split(line, fields, MAX_FIELDS, " ");  /* split line */
    if(numFields != 3) {
        fprintf(stderr, "Error parsing %s: header line 1: expecting 3 fields but got %d: %s\n", fci->summaryFile.fileName, numFields, saveLine);
        exit(1);
    }
    strcpy(siteStr, fields[0]);
    strcpy(latStr, fields[1]);
    strcpy(lonStr, fields[2]);
    
    // siteName
    numFields = split(siteStr, fields, MAX_FIELDS, "=");
    if(numFields != 2) {
        fprintf(stderr, "Error parsing %s: header line 1: can't figure siteName: %s\n", fci->summaryFile.fileName, saveLine);
        exit(1);
    }
    fci->siteName = strdup(fields[1]);

    // latitude
    numFields = split(latStr, fields, MAX_FIELDS, "=");
    if(numFields != 2) {
        fprintf(stderr, "Error parsing %s: header line 1: can't figure latitude: %s\n", fci->summaryFile.fileName, saveLine);
        exit(1);
    }
    fci->lat = atof(fields[1]);
    if(fci->lat < -90 || fci->lat > 90) {
        fprintf(stderr, "Error parsing %s: header line 1: can't figure latitude: %s\n", fci->summaryFile.fileName, saveLine);
        exit(1);        
    }

    // longitude
    numFields = split(lonStr, fields, MAX_FIELDS, "=");
    if(numFields != 2) {
        fprintf(stderr, "Error parsing %s: header line 1: can't figure longitude: %s\n", fci->summaryFile.fileName, saveLine);
        exit(1);
    }
    fci->lon = atof(fields[1]);
    if(fci->lat < -180 || fci->lat > 180) {
        fprintf(stderr, "Error parsing %s: header line 1: can't figure longitude: %s\n", fci->summaryFile.fileName, saveLine);
        exit(1);        
    }

    // now we have to parse the column names line (example file: Penn_State_PA.wtRange=0.95-1.05_ha=1-36.csv)
    // #hoursAhead,group N,sat RMSE,phase 1 RMSE,phase 2 RMSE,phase 1 RMSE calls,phase 2 RMSE calls,ncep_RAP_DSWRF model, ncep_RAP_DSWRF status,ncep_RAP_DSWRF N,ncep_RAP_DSWRF RMSE,ncep_RAP_DSWRF Weight 1,ncep_RAP_DSWRF weight 2,persistence model, persistence status,persistence N,persistence RMSE,persistence Weight 1,persistence weight 2,ncep_NAM_hires_DSWRF_inst model, ncep_NAM_hires_DSWRF_inst status,ncep_NAM_hires_DSWRF_inst N,ncep_NAM_hires_DSWRF_inst RMSE,ncep_NAM_hires_DSWRF_inst Weight 1,ncep_NAM_hires_DSWRF_inst weight 2,ncep_NAM_DSWRF model, ncep_NAM_DSWRF status,ncep_NAM_DSWRF N,ncep_NAM_DSWRF RMSE,ncep_NAM_DSWRF Weight 1,ncep_NAM_DSWRF weight 2,ncep_GFS_sfc_DSWRF_surface_avg model, ncep_GFS_sfc_DSWRF_surface_avg status,ncep_GFS_sfc_DSWRF_surface_avg N,ncep_GFS_sfc_DSWRF_surface_avg RMSE,ncep_GFS_sfc_DSWRF_surface_avg Weight 1,ncep_GFS_sfc_DSWRF_surface_avg weight 2,ncep_GFS_sfc_DSWRF_surface_inst model, ncep_GFS_sfc_DSWRF_surface_inst status,ncep_GFS_sfc_DSWRF_surface_inst N,ncep_GFS_sfc_DSWRF_surface_inst RMSE,ncep_GFS_sfc_DSWRF_surface_inst Weight 1,ncep_GFS_sfc_DSWRF_surface_inst weight 2,ncep_GFS_DSWRF model, ncep_GFS_DSWRF status,ncep_GFS_DSWRF N,ncep_GFS_DSWRF RMSE,ncep_GFS_DSWRF Weight 1,ncep_GFS_DSWRF weight 2,NDFD_global model, NDFD_global status,NDFD_global N,NDFD_global RMSE,NDFD_global Weight 1,NDFD_global weight 2,cm model, cm status,cm N,cm RMSE,cm Weight 1,cm weight 2,ecmwf_ghi model, ecmwf_ghi status,ecmwf_ghi N,ecmwf_ghi RMSE,ecmwf_ghi Weight 1,ecmwf_ghi weight 2

    char *forecastHeaderLine = line+1;  // skip # char
    fgets(forecastHeaderLine, LINE_LENGTH, fp);
    
    numFields = split(line, fields, MAX_FIELDS, ",");  /* split line */
    
    for(i=0; i<numFields; i++) {
        
        
       if(strstr(fields[i], WEIGHT_2_STR) != NULL) {
            fprintf(stderr, "\tgot %s\n", fields[i]);
        }
    }
    
    fci->forecastLineNumber = 2;

    //modelErrorType *modelData;
    
    while(fgets(line, LINE_LENGTH, fp)) {
        fci->forecastLineNumber++; 
        
        numFields = split(line, fields, MAX_FIELDS, ",");  /* split line */  
        if(numFields < 20) {
            sprintf(ErrStr, "Scanning line %d of file %s, got %d columns using delimiter \"%s\" (expecting at least 20).\nEither delimiter flag or -s arg is wrong", fci->forecastLineNumber, fci->forecastTableFile.fileName, numFields, Delimiter);
            FatalError("readSummaryFile()", ErrStr);
        }
                      
        //int hoursAhead = atoi(fields[0]);
        //modelData = &fci->hoursAheadGroup[hourIndex];
        
        // siteGroup
        fldPtr = fields[0];
/*
        if(fci->siteGroup == NULL) {
            fci->siteGroup = strdup(fldPtr);
        }
        else {
            if(!fci->multipleSites && strcasecmp(fldPtr, fci->siteGroup) != 0) {
                fprintf(fci->warningsFile.fp, "Warning: siteGroup changed from %s to %s\n", fci->siteGroup, fldPtr);
            }
        }
        
        // siteName
        fldPtr = fields[1];

        if(!fci->multipleSites && strcasecmp(fldPtr, fci->siteName) != 0) {
            fprintf(fci->warningsFile.fp, "Warning: siteName changed from %s to %s\n", fci->siteName, fldPtr);
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
                fprintf(fci->warningsFile.fp, "Warning: latitude changed from %.3f to %.3f\n", fci->lat, lat);
            }
            if(!fci->multipleSites && fabs(lon - fci->lon) > 0.01) {
                fprintf(fci->warningsFile.fp, "Warning: longitude changed from %.3f to %.3f\n", fci->lon, lon);
            }        
        }
        
        (void) readDataFromLine(fci, fields);
        

        if(fci->numValidSamples == 3)
            return True;
*/

        fci->numInputRecords++;

    }
    
    return(True);
}

void dumpWeightedTimeSeries(forecastInputType *fci,int hourIndex)
{
    int sampleInd, modelIndex;
    double weight, weightTotal;
    timeSeriesType *thisSample;
    modelErrorType *modelData = &fci->hoursAheadGroup[hourIndex];
    modelStatsType *thisModelErr;

/*
  if(!fci->weightedTimeSeriesFp)
    return;
*/

    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->isValid) {
            weightTotal = 0;
            thisSample->weightedModelGHI = 0;
            for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                thisModelErr = &fci->hoursAheadGroup[hourIndex].hourlyModelStats[modelIndex];
                            
                if(modelData->hourlyModelStats[modelIndex].isActive) {
                    weight = thisModelErr->optimizedWeightPass2;
                    thisSample->weightedModelGHI += (thisSample->modelData[hourIndex].modelGHI[modelIndex] * weight);
                    weightTotal += weight;            
                }            
            }        
        }

        //fprintf(stderr, "DWTS: %s,%d,%f,%f,%f\n",dtToStringCsv2(&thisSample->dateTime),fci->hoursAheadGroup[hourIndex].hoursAhead,thisSample->weightedModelGHI/weightTotal,thisSample->weightedModelGHI,weightTotal);
    }
}

/* 
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