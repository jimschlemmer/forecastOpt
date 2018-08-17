#include "forecastOpt.h"
#include "forecastOptUtils.h"

char ErrStr[4096];

int allocatedSamples = 1;
void setModelOrder(forecastInputType *fci);
void registerModelInfo(forecastInputType *fci, char *modelName, char *modelDescription, int isReference, int isForecast, int hoursAhead);

#define KTBINS_6

//#define NO_KTI

//
// Print message and exit with specific exitCode
//

void fatalErrorWithExitCode(char *functName, char *errStr, char *file, int linenumber, int exitCode)
{
    fprintf(stderr, "FATAL: %s: %s in %s at line %d\n", functName, errStr, file, linenumber);
    exit(exitCode);
}

//
// Print message and exit with exit code = 1
//

void fatalError(char *functName, char *errStr, char *file, int linenumber)
{
    int exitCode = 1;
    fatalErrorWithExitCode(functName, errStr, file, linenumber, exitCode);
}

// [sunspot]:/home/jim/forecast/forecastOpt/data> ls
// nwp  surfrad  v3
// boulder.HRRR.HA14.csv 

int readForecastData(forecastInputType *fci)
{
    char line[1024];
    int numFields;
    char *fields[MAX_FIELDS];
    dateTimeType currDate;
    timeSeriesType *thisSample;
    int hoursAheadIndex, numFiles = 0;
    char pattern[256], **filenames = NULL, tempFilename[1024];
    void findKtModelColumn(forecastInputType *fci, char *filename);

    fci->numInputFiles = 0;
    /* 
    Data are now arranged in .csv files, all in one file.  For example

    [sunspot]:/home/jim/forecast/forecastOpt/data/all> head bondville.surface.v3.nwp.HA1.csv
    #year,month,day,hour,min,surface,zen,v3,CMM-east,ECMWF,GFS,HRRR,NDFD,clearsky_GHI
    2015,7,12,0,0,64.941667,76.05,81,-999,110,115,37,38,167
    2015,7,12,1,0,-2.355,86.55,23,8,23,23,6,1,23
    2015,7,12,2,0,-3.678333,96.59,0,0,0,0,0,0,0
    2015,7,12,3,0,-3.728333,105.16,0,0,0,0,0,0,0
    2015,7,12,4,0,-3.67,111.97,0,-999,0,0,0,0,0

    So it makes sense to cycle by site and HA 
     */

    fprintf(stderr, "Scanning input directory %s for files\n", fci->inputDirectory);

    int i;
    for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        sprintf(pattern, "HA%d.csv", hoursAheadIndex);
        if((filenames = readDirInOrder(fci->inputDirectory, pattern, &numFiles)) == NULL)
            FatalError("readForecastData()", "Something went wrong reading input directory");
        for(i = 0; i < numFiles; i++) {
            sprintf(tempFilename, "%s/%s", fci->inputDirectory, filenames[i]);
            fci->inputFiles[fci->numInputFiles].fileName = strdup(tempFilename);
            fprintf(stderr, "Found file %s\n", fci->inputFiles[fci->numInputFiles].fileName);
            fci->numInputFiles++;
        }
    }

    fprintf(stderr, "Got %d files\n", fci->numInputFiles);

    fci->startHourLowIndex = 0;
    fci->startHourHighIndex = fci->numInputFiles - 1;

    for(hoursAheadIndex = 0; hoursAheadIndex < fci->numInputFiles; hoursAheadIndex++) {
        // open input file
        fprintf(stderr, "Reading NWP file %s\n", fci->inputFiles[hoursAheadIndex].fileName);
        if((fci->inputFiles[hoursAheadIndex].fp = fopen(fci->inputFiles[hoursAheadIndex].fileName, "r")) == NULL) {
#ifdef WRITE_WARNINGS
            sprintf(ErrStr, "Couldn't open warnings file %s: %s", fci->inputFiles[hoursAheadIndex].fileName, strerror(errno));
#endif
            FatalError("readForecastData()", ErrStr);
        }

        // process header lines
        fgets(fci->forecastHeaderLine1, LINE_LENGTH, fci->inputFiles[hoursAheadIndex].fp);
        fgets(fci->forecastHeaderLine2, LINE_LENGTH, fci->inputFiles[hoursAheadIndex].fp);

        parseNwpHeaderLine(fci, fci->inputFiles[hoursAheadIndex].fileName);
        findKtModelColumn(fci, fci->inputFiles[hoursAheadIndex].fileName);

        fci->forecastLineNumber = 2;

        while(fgets(line, LINE_LENGTH, fci->inputFiles[hoursAheadIndex].fp)) {
            fci->forecastLineNumber++;

            if(line[0] == '#') {
                strcpy(fci->forecastHeaderLine1, line);
                fgets(fci->forecastHeaderLine2, LINE_LENGTH, fci->inputFiles[hoursAheadIndex].fp);
                parseNwpHeaderLine(fci, fci->inputFiles[hoursAheadIndex].fileName);
                fci->forecastLineNumber++;
                continue;
            }

            numFields = split(line, fields, MAX_FIELDS, fci->delimiter); /* split line */
            if(numFields != fci->numHeaderFields) {
                sprintf(ErrStr, "Scanning line %d of file %s, got %d columns using delimiter \"%s\" but was expecting %d based on header line.",
                        fci->forecastLineNumber, fci->forecastTableFile.fileName, numFields, fci->delimiter, fci->numHeaderFields);
                FatalError("readForecastData()", ErrStr);
            }

            // date & time
            if(!parseDateTime(fci, &currDate, fields, numFields)) {
                sprintf(ErrStr, "Scanning line %d of file %s, got bad datetime", fci->forecastLineNumber, fci->forecastTableFile.fileName);
                FatalError("readForecastData()", ErrStr);
            }

            //            incrementTimeSeries(fci);
            //thisSample = &(fci->timeSeries[fci->numTotalSamples - 1]); // switched these two lines

            // on the first file we allocate the T/S structs
            if(hoursAheadIndex == 0) {
                thisSample = allocTimeSeriesSample(fci, hoursAheadIndex); // &fci->nwpTimeSeries[hoursAheadIndex].timeSeries[numTimeSeriesSamples];
                thisSample->dateTime = currDate;
                thisSample->siteName = strdup(fci->thisSite->siteName);
            }
                // on subsequent HAs we look it up
            else
                thisSample = findTimeSeriesSample(fci, &currDate);

            //thisSample->hoursAheadIndex = hoursAheadIndex;

            //#define USE_ZENITH_INSTEAD_OF_HAS
            // compute hours after sunrise this date/time
            dateTimeType sunrise = calculateSunrise(&currDate, fci->thisSite->lat, fci->thisSite->lon); // Get the sunrise time for this day
            thisSample->zenith = calculateZenithAngle(&currDate, fci->thisSite->lat, fci->thisSite->lon);

            readDataFromLine(fci, hoursAheadIndex, thisSample, fields, numFields); // get the forecast data -- also sets ktIndex

            if(thisSample->sunIsUp) {
                // it seems as though the sunae zenith angle is running a fraction ahead (~30 sec) the Surfrad zenith angles
                if(thisSample->zenith < 0 || thisSample->zenith > 90) {
                    sprintf(ErrStr, "Got bad zenith angle at line %d: %.2f", fci->forecastLineNumber, thisSample->zenith);
                    FatalError("readForecastData()", ErrStr);
                }

#ifdef USE_ZENITH_INSTEAD_OF_HAS
                thisSample->hoursAfterSunrise = (int) ((90 - thisSample->zenith) / 10) + 1; // 9 buckets of 10 degrees each
#else
                thisSample->hoursAfterSunrise = thisSample->dateTime.hour - sunrise.hour;
                if(thisSample->hoursAfterSunrise < 1)
                    thisSample->hoursAfterSunrise += 24;
                if(thisSample->hoursAfterSunrise >= 24)
                    thisSample->hoursAfterSunrise = 1;

#ifdef DEBUG_HAS
                fprintf(stderr, "%s,%.4f,%.4f,time=%s,", fci->thisSite->siteName, fci->thisSite->lat, fci->thisSite->lon, dtToStringDateTime(&thisSample->dateTime));
                fprintf(stderr, "sunrise=%s,hoursAfterSunrise=%d\n", dtToStringDateTime(&thisSample->sunrise), thisSample->hoursAfterSunrise);
#endif

#endif
            }
            else
                thisSample->hoursAfterSunrise = -1;

            fci->numInputRecords++;
        }
    }

    if(fci->ktModelColumn < 0 || fci->ktModelColumn > fci->numModels) {
        sprintf(ErrStr, "Looks like the ktModelColumn (%d) is out of range.  numModels = %d", fci->ktModelColumn, fci->numModels);
        FatalError("readForecastData()", ErrStr);
    }

    studyData(fci);

    fci->multipleSites = (fci->numSites > 1);
    fci->gotForecastFile = True;

    return True;
}

void setSiteInfo(forecastInputType *fci, char *site, double lat, double lon, int hoursAhead)
{
    int i;

    for(i = 0; i < fci->numSites; i++) { // find site from list of sites
        if(strcmp(site, fci->allSiteInfo[i].siteName) == 0) {
            fci->siteIndex = i;
            fci->thisSite = &fci->allSiteInfo[i];
            // paranoia check
            if(fabs(fci->thisSite->lat - lat) > 0.01) {
                sprintf(ErrStr, "Current lat(%.2f) for site %s conflicts with previous lat(%.2f)", lat, site, fci->thisSite->lat);
                FatalError("setSiteInfo()", ErrStr);
            }
            if(fabs(fci->thisSite->lon - lon) > 0.01) {
                sprintf(ErrStr, "Current lon(%.2f) for site %s conflicts with previous lon(%.2f)", lon, site, fci->thisSite->lon);
                FatalError("setSiteInfo()", ErrStr);
            }
            break;
        }
    }
    if(i == fci->numSites) { // new site
        fci->allSiteInfo[fci->numSites].siteName = strdup(site);
        fci->allSiteInfo[fci->numSites].lat = lat;
        fci->allSiteInfo[fci->numSites].lon = lon;
        fci->thisSite = &fci->allSiteInfo[fci->numSites];
        fci->numSites++;
    }

    fprintf(stderr, "=== Setting site to %s ===\n", fci->thisSite->siteName);


    // Now that we know the site name we can open the warnings file
#ifdef WRITE_WARNINGS
    sprintf(tempFileName, "%s/%s.HA%d.warnings.txt", fci->outputDirectory, fci->thisSite->siteName, hoursAhead);

    if(fci->warningsFile.fp != NULL)
        fclose(fci->warningsFile.fp);
    fci->warningsFile.fileName = strdup(tempFileName);
    if((fci->warningsFile.fp = fopen(fci->warningsFile.fileName, "w")) == NULL) {
        sprintf(ErrStr, "Couldn't open warnings file %s: %s", fci->warningsFile.fileName, strerror(errno));
        FatalError("setSiteInfo()", ErrStr);
    }
#endif
}

void parseNwpHeaderLine(forecastInputType *fci, char *filename)
{
    // header line 1:
    // #site,HA,lat,lon
    // header line 2:
    // #year,month,day,hour,min,surface,zen,v3,CMM-east,ECMWF,GFS,HRRR,NDFD,clearsky_GHI
    // assumptions: 
    // 1) the first eight columns will be year,month,day,hour,min,surface,zen,v3
    // 2) the last column will be clearsky_GHI
    // 3) the models will appear in between

    int i, HA;
    char tempLine[1024];
    char *fields[MAX_FIELDS];
    double lat, lon;
    // /home/jim/forecast/forecastOpt/data/etc/bondville.surface.v3.nwp.HA1.csv

    if(fci->verbose)
        fprintf(stderr, "parsing...\nheader line 1:%sheader line 2:%s", fci->forecastHeaderLine1, fci->forecastHeaderLine2);

    strcpy(tempLine, fci->forecastHeaderLine1);
    fci->numHeaderFields = split(tempLine + 1, fields, MAX_FIELDS, ","); /* split path */

    if(fci->numHeaderFields != 4) {
        sprintf(ErrStr, "%s : Too few columns in header line 1 to work with in input file:\n%s\n", filename, fci->forecastHeaderLine1);
        FatalError("parseNwpHeaderLine()", ErrStr);
    }

    // parse the first header line.  Looks like:
    // #bondville,1,40.05,-88.35
    HA = atoi(fields[1]);
    lat = atof(fields[2]);
    lon = atof(fields[3]);
    if(lat < -90 || lat > 90) {
        sprintf(ErrStr, "%s : Bad latitude in header line 1: %s\n", filename, fci->forecastHeaderLine1);
        FatalError("parseNwpHeaderLine()", ErrStr);
    }
    if(lon < -180 || lon > 180) {
        sprintf(ErrStr, "%s : Bad longitude in header line 1: %s\n", filename, fci->forecastHeaderLine1);
        FatalError("parseNwpHeaderLine()", ErrStr);
    }

    if(HA < 1 || HA > 300) {
        sprintf(ErrStr, "%s : Something wrong with first line of input file.  Expecting site,HA,lat,lon\n", filename);
        FatalError("parseNwpHeaderLine()", ErrStr);
    }

    setSiteInfo(fci, fields[0], lat, lon, HA);
    if(fci->numModels > 0) {
        if(fci->verbose)
            fprintf(stderr, "Resetting model isActive switches...\n");
        for(i = 0; i < fci->numModels; i++) {
            fci->hoursAheadGroup[HA - 1].hourlyModelStats[i].isActive = False;
        }
    }

    // header line 2 should look like:
    // #year,month,day,hour,min,surface,v3,GFS,NDFD,CMM-east,ECMWF,HRRR,clearsky_GHI

    strcpy(tempLine, fci->forecastHeaderLine2);
    fci->numHeaderFields = split(tempLine + 1, fields, MAX_FIELDS, ","); /* split path */

    if(fci->numHeaderFields < 10) {
        sprintf(ErrStr, "%s : Too few columns in header line 2 to work with in input file:\n%s", filename, fci->forecastHeaderLine2);
        FatalError("parseNwpHeaderLine()", ErrStr);
    }

    // now register the NWP models
    for(i = fci->startModelsColumnNumber; i < (fci->numHeaderFields - 1); i++) { // assuming clearsky comes last so don't register it
        registerModelInfo(fci, fields[i], fields[i], IsNotReference, IsForecast, HA);
    }

    fci->clearskyGHICol = i; // assumed to be the last

}

void findKtModelColumn(forecastInputType *fci, char *filename)
{
    // #year,month,day,hour,min,surface,zen,v3,CMM-east,ECMWF,GFS,HRRR,NDFD,clearsky_GHI

    int i, modelIndex, numFields;
    char tempLine[1024];
    char *fields[MAX_FIELDS];

    if(fci->numKtBins == 1)
        return;

    strcpy(tempLine, fci->forecastHeaderLine2);
    numFields = split(tempLine + 1, fields, MAX_FIELDS, ","); /* split path */

    // ktModelColumn get detected automatically
    char gotKtColumn = False;
    for(i = fci->startModelsColumnNumber, modelIndex = 0; i < (numFields - 1); i++, modelIndex++) { // assuming clearsky comes last so don't register it
        if(strcmp(fields[i], fci->ktModelColumnName) == 0) {
            fci->ktModelColumn = modelIndex;
            gotKtColumn = True;
            fprintf(stderr, "Setting KT model column to %d for model %s\n", fci->ktModelColumn, fci->ktModelColumnName);
        }
    }

    if(!gotKtColumn) {
        sprintf(ErrStr, "%s : Couldn't find a model that matches %s", filename, fci->ktModelColumnName);
        FatalError("parseNwpHeaderLine()", ErrStr);
    }


}

// this assumes fci->thisSite->siteName has already been set by the input file parser

void setSite(forecastInputType *fci)
{
    int siteNum;

    for(siteNum = 0; siteNum < fci->numSites; siteNum++) {
        if(strcmp(fci->thisSite->siteName, fci->allSiteInfo[siteNum].siteName) == 0) {
            fci->thisSite = &fci->allSiteInfo[siteNum];
            return;
        }
    }

    fprintf(stderr, "Couldn't find site name \"%s\" in internal site list\n", fci->thisSite->siteName);
    exit(1);
}

#define DT_FORMAT_1 1
#define DT_FORMAT_2 2

int parseDateTime(forecastInputType *fci, dateTimeType *dt, char **fields, int numFields)
{
    //   #year,month,day,hour,min,surface,zen,v3,CMM-east,ECMWF,GFS,HRRR,NDFD,clearsky_GHI
    //   2015,7,12,0,0,64.941667,76.05,81,-999,110,115,37,38,167 

    if(numFields < 5)
        return False;

    dt->year = atoi(fields[0]);
    dt->month = atoi(fields[1]);
    dt->day = atoi(fields[2]);
    dt->hour = atoi(fields[3]);
    dt->min = atoi(fields[4]);

    setObsTime(dt);
    return (dateTimeSanityCheck(dt));
}

#define checkTooLow(X,Y) { if(thisSample->sunIsUp && thisSample->X < 1) { fprintf(fci->warningsFile.fp, "Warning: line %d: %s looks too low: %.1f\n", fci->forecastLineNumber, Y, thisSample->X); }}

void copyHoursAfterData(forecastInputType *fci)
{
    int hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, modelIndex;
    modelRunType *modelRun;

    for(hoursAheadIndex = 0; hoursAheadIndex <= fci->maxHoursAheadIndex; hoursAheadIndex++)
        for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
            for(ktIndex = 0; ktIndex < fci->numKtBins; ktIndex++) {
                modelRun = &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex];
                modelRun->hoursAfterSunrise = hoursAfterSunriseIndex + 1;
                modelRun->hoursAhead = fci->hoursAheadGroup[hoursAheadIndex].hoursAhead;
                for(modelIndex = 0; modelIndex < MAX_MODELS; modelIndex++)
                    modelRun->hourlyModelStats[modelIndex] = fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex];
            }
        }
}

int readDataFromLine(forecastInputType *fci, int hoursAheadIndex, timeSeriesType *thisSample, char *fields[], int numFields)
{

#ifdef DUMP_ALL_DATA
    static char firstTime = True;
    static char *filteredDataFileName = "filteredInputData.csv";
#endif
    //timeSeriesType *thisSample = &(fci->timeSeries[fci->numTotalSamples - 1]);
    //thisSample->zenith = atof(fields[fci->zenithCol]);

    thisSample->siteName = strdup(fci->thisSite->siteName); // for multiple site runs

    thisSample->sunIsUp = (thisSample->zenith < 90);
    if(thisSample->sunIsUp)
        fci->numDaylightRecords++;

    thisSample->groundGHI = atof(fields[fci->groundGHICol]);
    /*
        if(thisSample->sunIsUp && thisSample->groundGHI < 1)
            fprintf(stderr, "Warning: line %d: groundGHI looks too low: %.1f\n", fci->forecastLineNumber, thisSample->groundGHI);
     */

    if(thisSample->groundGHI < MIN_IRR || thisSample->groundGHI > MAX_IRR) {
        sprintf(ErrStr, "Got bad surface GHI at line %d: %.2f", fci->forecastLineNumber, thisSample->groundGHI);
        thisSample->groundGHI = 0;
        //FatalError("readDataFromLine()", ErrStr);
    }
#ifdef WRITE_WARNINGS
    if(thisSample->sunIsUp && thisSample->groundGHI < 1)
        fprintf(fci->warningsFile.fp, "Warning: line %d: %s looks too low: %.1f\n", fci->forecastLineNumber, "groundGHI", thisSample->groundGHI);
#endif

    thisSample->clearskyGHI = atof(fields[fci->clearskyGHICol]);
    if(thisSample->clearskyGHI < MIN_IRR || thisSample->clearskyGHI > MAX_IRR) {
        sprintf(ErrStr, "Got bad clearsky GHI at line %d: %.2f", fci->forecastLineNumber, thisSample->clearskyGHI);
        FatalError("readDataFromLine()", ErrStr);
    }
#ifdef WRITE_WARNINGS
    checkTooLow(clearskyGHI, "clearskyGHI");
#endif
    thisSample->satGHI = atof(fields[fci->satGHICol]);
#ifdef WRITE_WARNINGS
    checkTooLow(satGHI, "satGHI");
#endif
    int modelIndex, fieldIndex = 0;
    // a little bit tricky here.  if we have more models than fields -- as can happen when we 
    // have CMM at lower HAs but it disappears after 5HA -- we have to skip over the CMM modelIndex
    // and hold up the fieldIndex
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        // skip this model "slot" if it's not active -- for example, the CMM at HA 7
        if(fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isActive) {
            thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] = atof(fields[fieldIndex + fci->startModelsColumnNumber]);
            if(thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] < 0)
                thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] = 0;
            fieldIndex++; // only increment when we have an isActive model
        }
        else {
            thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] = 0;
        }
    }

    // set ktTargetNWP for this time series sample
    if(thisSample->clearskyGHI < 1)
        thisSample->forecastData[hoursAheadIndex].ktTargetNWP = 0;
    else {
        thisSample->forecastData[hoursAheadIndex].ktTargetNWP = thisSample->forecastData[hoursAheadIndex].modelGHI[fci->ktModelColumn] / thisSample->clearskyGHI;
        if(thisSample->forecastData[hoursAheadIndex].ktTargetNWP > 1)
            thisSample->forecastData[hoursAheadIndex].ktTargetNWP = 1;
    }
    //setKtIndex(fci, thisSample, hoursAheadIndex);  // this gets done when the data is read in, sets the 

    //fprintf(stderr, "HA=%d,%s,GHI=%.0f,CLR=%.0f,kt=%.3f\n", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead, dtToStringCsv2(&thisSample->dateTime), thisSample->forecastData[hoursAheadIndex].modelGHI[fci->ktModelColumn], thisSample->clearskyGHI, thisSample->forecastData[hoursAheadIndex].ktTargetNWP);
    // #define DUMP_ALL_DATA
#ifdef DUMP_ALL_DATA
    static int firstTime = True;

    if(1) { // || hoursAheadIndex == 15 || hoursAheadIndex == 5) {
        if(firstTime) {
            /*        if(filteredDataFileName == NULL || (FilteredDataFp = fopen(filteredDataFileName, "w")) == NULL) {
                        sprintf(ErrStr, "Couldn't open output file %s : %s", filteredDataFileName, strerror(errno));
                        FatalError("readDataFromLine()", ErrStr);
                    }
                    //FilteredDataFp = stderr;
             */

            fprintf(stderr, "#year,month,day,hour,minute,groundGHI,v3GHI");
            for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
                fprintf(stderr, ",%s", fci->modelInfo[modelIndex].modelName);
            }
            fprintf(stderr, ",clearsky_GHI,ktTargetNWP,ktIndex\n");
            firstTime = False;
        }
#endif

#ifdef DUMP_ALL_DATA
        fprintf(stderr, "%s,%.0f,%.0f", dtToStringCsv2(&thisSample->dateTime), thisSample->groundGHI, thisSample->satGHI);
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            //hoursAheadIndex = fci->modelInfo[modelIndex].hoursAheadIndex;
            //fprintf(stderr, ",%.1f", thisSample->nwpData[modelIndex]);
            fprintf(stderr, ",%.0f", thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex]);
        }
        fprintf(stderr, ",%.0f,%.3f,%d\n", thisSample->clearskyGHI, thisSample->forecastData[hoursAheadIndex].ktTargetNWP, thisSample->forecastData[hoursAheadIndex].ktIndex);
    }
#endif

    return True;
}

// this function sets the groupIsValid flag
// hoursAfterSunriseIndex == -1 means filter all data (across all HAS bins)
// ktIndex is 0-based ktIndex.  [0 : 0.0 < kt <= 0.1], [1 : 0.1 < kt <= 0.2], ... , [9 : 0.9 < kt <= 1.0]
// <not used at present>

int setKtLimits(int ktIndex, double *ktLow, double *ktHigh)
{
    switch(ktIndex) {
        case 0: *ktLow = 0.0;
            *ktHigh = 0.2;
            return True;
        case 1: *ktLow = 0.2;
            *ktHigh = 0.4;
            return True;
        case 2: *ktLow = 0.4;
            *ktHigh = 0.6;
            return True;
        case 3: *ktLow = 0.6;
            *ktHigh = 0.8;
            return True;
        case 4: *ktLow = 0.8;
            *ktHigh = 0.9;
            return True;
        case 5: *ktLow = 0.9;
            *ktHigh = 1.0;
            return True;
        default:
        {
            fprintf(stderr, "ktIndex = %d\n", ktIndex);
            FatalError("setKtLimits()", "ktIndex out of range");
        }
    }
    return False;
}

// kt bins -- 0.2,0.2,0.2,0.2,0.1,0.1

void setKtIndex(forecastInputType *fci, timeSeriesType *thisTS, int hoursAheadIndex)
{
    double kt;
    forecastDataType *thisSample = &thisTS->forecastData[hoursAheadIndex];
    int *ktIndex;

    // we're either in ktbootstrap mode or just running without kt bins
    if(fci->numKtBins <= 1) {
        thisSample->ktIndexNWP = 0;
        return;
    }

    // set kt value
    if(fci->inKtBootstrap) {
        thisSample->ktOpt = thisTS->clearskyGHI > 1 ? thisSample->optimizedGHI1 / thisTS->clearskyGHI : 0;
        kt = thisSample->ktOpt;
        fprintf(stderr, "setKtIndex:%s HA%d CLR=%.1f optimzedGHI1=%.1f ktOpt=%.3f\n", dtToString(&thisTS->dateTime), hoursAheadIndex + 1, thisTS->clearskyGHI, thisSample->optimizedGHI1, thisSample->ktOpt);
        ktIndex = &thisSample->ktIndexOpt;
    }
    else {
        kt = thisSample->ktTargetNWP;
        ktIndex = &thisSample->ktIndexNWP;
    }

#ifdef KTBINS_6
    if(kt <= 0.2) {
        *ktIndex = 0;
        return;
    }
    if(kt <= 0.4) {
        *ktIndex = 1;
        return;
    }
    if(kt <= 0.6) {
        *ktIndex = 2;
        return;
    }
    if(kt <= 0.8) {
        *ktIndex = 3;
        return;
    }
    if(kt <= 0.9) {
        *ktIndex = 4;
        return;
    }
    *ktIndex = 5;
    return;
#endif
#ifdef KTBINS_2
    if(kt <= 0.6) {
        *ktIndex = 0;
        return;
    }
    *ktIndex = 1;
    return;
#endif
#ifdef KTBINS_3
    if(kt <= 0.6) {
        *ktIndex = 0;
        return;
    }
    if(kt <= 0.8) {
        *ktIndex = 1;
        return;
    }
    *ktIndex = 2;
    return;
#endif
}

// review the input data for holes

void studyData(forecastInputType *fci)
{

    timeSeriesType *thisSample;
    int i, modelIndex, hoursAheadIndex, daylightData = 0;

    for(i = 0; i < fci->numTotalSamples; i++) {
        thisSample = &(fci->timeSeries[i]);
        if(thisSample->sunIsUp) {
            for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
                daylightData++;
                hoursAheadIndex = fci->modelInfo[modelIndex].hoursAheadIndex;

                if(thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] == -999) {
                    fci->modelInfo[modelIndex].numMissing++;
                }
                else {
                    fci->modelInfo[modelIndex].numGood++;
                }
            }
        }
    }

    if(daylightData < 1) {
        FatalError("studyData()", "Got no daylight data scanning input.");
    }

#ifdef WRITE_WARNINGS
    fprintf(fci->warningsFile.fp, "======== Scanning Forecast Data For Active Models =========\n");
#endif
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        hoursAheadIndex = fci->modelInfo[modelIndex].hoursAheadIndex;
        modelIndex = fci->modelInfo[modelIndex].modelIndex;
        if(fci->modelInfo[modelIndex].numGood < 1)
            fci->modelInfo[modelIndex].percentMissing = 100;
        else
            fci->modelInfo[modelIndex].percentMissing = fci->modelInfo[modelIndex].numMissing / (fci->modelInfo[modelIndex].numMissing + fci->modelInfo[modelIndex].numGood) * 100.0;

        if(fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isActive) { // hasn't been disabled yet
            if(fci->modelInfo[modelIndex].percentMissing < 100 && fci->modelInfo[modelIndex].percentMissing > 0) {
                fprintf(stderr, "Warning: %s is neither completely on nor off in the input forecast data (%.0f%% missing)\n", fci->modelInfo[modelIndex].modelName, fci->modelInfo[modelIndex].percentMissing);
            }
            // deactivate this model on account of too much missing data
            if(fci->modelInfo[modelIndex].percentMissing > 90) {
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isActive = False;
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].tooMuchDataMissing = True;
#ifdef WRITE_WARNINGS
                fprintf(fci->warningsFile.fp, "%35s : off\n", fci->modelInfo[modelIndex].modelName); //, hoursAheadIndex = %d\n", fci->modelInfo[modelIndex].modelName, modelIndex, hoursAheadIndex);
                fprintf(fci->warningsFile.fp, "WARNING: disabling model '%s' : %.0f%% data missing : model index = %d hour index = %d\n", fci->modelInfo[modelIndex].modelName, fci->modelInfo[modelIndex].percentMissing, modelIndex, hoursAheadIndex); //, hoursAheadIndex = %d\n", fci->modelInfo[modelIndex].modelName, modelIndex, hoursAheadIndex);
#endif
                fprintf(stderr, "WARNING: disabling model '%s' : %.0f%% data missing : model index = %d hour index = %d\n", fci->modelInfo[modelIndex].modelName, fci->modelInfo[modelIndex].percentMissing, modelIndex, hoursAheadIndex); //, hoursAheadIndex = %d\n", fci->modelInfo[modelIndex].modelName, modelIndex, hoursAheadIndex);
            }
            else {
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isActive = True;
#ifdef WRITE_WARNINGS
                fprintf(fci->warningsFile.fp, "%35s : on\n", fci->modelInfo[modelIndex].modelName); //, hoursAheadIndex = %d\n", fci->modelInfo[modelIndex].modelName, modelIndex, hoursAheadIndex);
#endif
            }
        }

        // use isContributingModel (in optimization stage) to signify isActive and not isReference forecast model (such as persistence)
        fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isContributingModel =
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isActive
                && !fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isReference;

        //fprintf(stderr, "%s : column index = %d : model index = %d : hour index = %d : missing = %.0f%%\n", fci->modelInfo[modelIndex].modelName, modelIndex, fci->modelInfo[modelIndex].modelIndex, fci->modelInfo[modelIndex].hoursAheadIndex, fci->modelInfo[modelIndex].percentMissing);

        // disable models if percentMissing < threshold
    }
#ifdef WRITE_WARNINGS
    fprintf(fci->warningsFile.fp, "===========================================================\n");
#endif
}

/*
 
 Potential columns of interest
 
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
    int i, j;
    size_t size;

    fci->delimiter = ",";
    fci->startDate.year = -1;
    fci->endDate.year = -1;
    fci->outputDirectory = NULL;
    fci->inputDirectory = NULL;
    fci->verbose = False;
    fci->multipleSites = False;
    fci->gotConfigFile = False;
    fci->gotForecastFile = False;
    fci->weightSumLowCutoff = 92; // experiment with other values such as 90 < sum < 110 ?
    fci->weightSumHighCutoff = 108;
    fci->startHourLowIndex = -1;
    fci->startHourHighIndex = -1;
    fci->numDaylightRecords = 0;
    fci->numDivisions = 7;
    fci->runWeightedErrorAnalysis = False;
    fci->forecastLineNumber = 0;
    fci->runHoursAfterSunrise = False;
    fci->maxHoursAfterSunrise = MAX_HOURS_AFTER_SUNRISE;
    fci->filterWithSatModel = True;
    fci->skipPhase2 = False;
    fci->numModels = 0;
    fci->numContribModels = 0;
    fci->numSites = 0;
    fci->maxModelIndex = 0;
    fci->numTotalSamples = 0;

    // allocate space for all model data
    allocatedSamples = 8670 * MAX_MODELS;
    size = allocatedSamples * sizeof (timeSeriesType);
    /*
        for(i = 0; i < MAX_HOURS_AHEAD; i++) {
            if((fci->nwpTimeSeries[i].timeSeries = (timeSeriesType *) malloc(size)) == NULL) {
                FatalError("initForecastInfo()", "memory alloc error");
            }
            fci->nwpTimeSeries[i].numGood = 0;
            fci->nwpTimeSeries[i].numMissing = 0;
            fci->nwpTimeSeries[i].numTimeSeriesSamples = 0;
        }
     */
            
    fprintf(stderr, "Allocating %ld bytes for fci->timeSeries\n", size);
    if((fci->timeSeries = (timeSeriesType *) malloc(size)) == NULL) {
        FatalError("initForecastInfo()", "memory alloc error");
    }

    fci->numColumnInfoEntries = 0;
    fci->numInputRecords = 0;
    fci->numDaylightRecords = 0;
    fci->doModelPermutations = True;
    fci->modelMixDirectory = "modelMixes";

    for(i = 0; i < MAX_HOURS_AHEAD; i++) {
        fci->hoursAheadGroup[i].hoursAhead = -1;
        for(j = 0; j < MAX_MODELS; j++)
            fci->hoursAheadGroup[i].hourlyModelStats[j].isActive = False;
    }

    fci->configFile.fileName = NULL;
    fci->warningsFile.fileName = NULL;
    fci->weightTableFile.fileName = NULL;
    fci->modelMixFileOutput.fileName = NULL;
    fci->modelMixFileInput.fileName = NULL;
    fci->warningsFile.fp = NULL;
    fci->configFile.fp = NULL;
    fci->weightTableFile.fp = NULL;
    fci->modelMixFileOutput.fp = NULL;
    fci->modelMixFileInput.fp = NULL;
    fci->correctionStatsFile.fp = NULL;

    fci->modelPermutations.numPermutations = 0;

    // #year,month,day,hour,min,surface,v3,GFS,NDFD,CMM-west,ECMWF,HRRR,clearsky_GHI
    // 0-based indexes, naturally
    fci->groundGHICol = 5;
    fci->satGHICol = 6;
    fci->startModelsColumnNumber = 7;

    // specify target GHI for kt calculations
    fci->ktModelColumn = 3; // ECMWF, in terms of 0-based model indices [GFS=0, NDFD=1, CMM=2, ECMWF=3, HRRR=4]
    fci->ktModelColumnName = "ECMWF";
    //fci->ktModelColumn = 1;  // NDFD column from input files
    //fci->ktModelColumnName = "NDFD";
    //fci->ktModelColumn = 0; // GFS column from input files
    //fci->ktModelColumnName = "GFS";

#ifdef KTBINS_6
    fci->numKtBins = 6; // needs to be changed if the setKtIndex is changes
#endif
#ifdef KTBINS_2
    fci->numKtBins = 2;
#endif
#ifdef KTBINS_3
    fci->numKtBins = 3;
#endif

#ifdef NO_KTI
    fci->numKtBins = 1;
#endif
    fci->numModelsRegistered = 0;
    fci->useSatelliteDataAsRef = False;
    fci->doKtBootstrap = False;
    fci->inKtBootstrap = False;
    fci->doKtNWP = False;
    fci->doKtOpt = False;
    fci->doKtAndNonKt = False;
}

void incrementTimeSeries(forecastInputType *fci)
{
    /*
        fci->numTotalSamples++;
        if(fci->numTotalSamples == allocatedSamples) {

            allocatedSamples *= 2;
            fci->timeSeries = (timeSeriesType *) realloc(fci->timeSeries, allocatedSamples * sizeof (timeSeriesType));
        }
     */
}

// thisSample = allocTimeSeriesSample(fci, hoursAheadIndex);
// Previously, the timeseries could just be incremented as we moved through the NWP input file as 
// that files contained all NWPs and HAs on a single line.  Now we have NWPs and HAs in a single file
// so we will have to search for the timeSeriesIndex that matches the current date/time

timeSeriesType *allocTimeSeriesSample(forecastInputType *fci, int hoursAheadIndex)
{
    int timeSeriesIndex = fci->numTotalSamples;

    //fci->nwpTimeSeries[hoursAheadIndex].numTimeSeriesSamples++;
    fci->numTotalSamples++;
    if(fci->numTotalSamples == allocatedSamples) {
        allocatedSamples *= 2;
        fci->timeSeries = (timeSeriesType *) realloc(fci->timeSeries, allocatedSamples * sizeof (timeSeriesType));
        fprintf(stderr, "Reallocating %ld bytes for fci->timeSeries\n", allocatedSamples * sizeof (timeSeriesType));
        //fci->nwpTimeSeries[hoursAheadIndex].timeSeries = (timeSeriesType *) realloc(fci->nwpTimeSeries[hoursAheadIndex].timeSeries, allocatedSamples * sizeof (timeSeriesType));
    }

    //return (&fci->nwpTimeSeries[hoursAheadIndex].timeSeries[timeSeriesIndex]);
    return (&fci->timeSeries[timeSeriesIndex]);
}

timeSeriesType *findTimeSeriesSample(forecastInputType *fci, dateTimeType *dt)
{
    int i;

    for(i = 0; i < fci->numTotalSamples; i++) {
        // match date/time and siteName
        if(fci->timeSeries[i].dateTime.obs_time == dt->obs_time && strcmp(fci->timeSeries[i].siteName, fci->thisSite->siteName) == 0)
            return (&fci->timeSeries[i]);
    }

    return NULL;
}

char *stripQuotes(char *str)
{
    char *q = NULL, *p = str;
    char origStr[1024];
    int firstQuote = 1;

    strcpy(origStr, str);
    while(*p != '\0') {
        if(*p == '"' || *p == '\'') {
            if(firstQuote) {
                q = p + 1;
                firstQuote = 0;
            }
            else {
                *p = '\0';
                return q;
            }
        }
        p++;
    }

    if(!firstQuote) {
        sprintf(ErrStr, "got unterminated quote string: %s", origStr);
        return NULL;
    }

    return str;
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
    char *q = NULL, *p = str;
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

// this simulates the model order of the old version of forecastOpt
// 
// actually, this screws up downstream code.  best to have the models
// in the order you want from the NWP .csv files

void setModelOrder(forecastInputType *fci)
{
    fci->modelInfo[0].modelName = "GFS";
    fci->modelInfo[1].modelName = "NDFD";
    fci->modelInfo[2].modelName = "CMM";
    fci->modelInfo[3].modelName = "ECMWF";
    fci->modelInfo[4].modelName = "HRRR";
    fci->numModels = 5;
}

int getModelIndex(forecastInputType *fci, char *modelName, int *modelIndex)
{
    int i;
    for(i = 0; i < fci->numModels; i++) {
        if(strcmp(modelName, fci->modelInfo[i].modelName) == 0) {
            *modelIndex = i;
            return False;
        }
        if(strstr(modelName, "CMM") != NULL && strstr(fci->modelInfo[i].modelName, "CMM") != NULL) { // some CMM model
            *modelIndex = i;
            return False;
        }
    }

    // add new model
    fci->modelInfo[fci->numModels].modelName = strdup(modelName);
    *modelIndex = fci->numModels;
    fci->numModels++;

    return True;
}

void registerModelInfo(forecastInputType *fci, char *modelName, char *modelDescription, int isReference, int isForecast, int hoursAhead)
{
    // We want to search the forecastHeaderLine for modelNames

    static int numFields = -1;
    static char *fields[MAX_FIELDS];
    int i, modelIndex;
    static int hoursAheadIndex = 0, lastHoursAhead = -1;
    char tempColDesc[1024];

    if(lastHoursAhead > 0 && lastHoursAhead != hoursAhead)
        hoursAheadIndex++;
    lastHoursAhead = hoursAhead;

    fci->maxHoursAheadIndex = hoursAheadIndex;

    /*
        if(!isReference)
            fci->numContribModels++;
     */


    fci->hoursAheadGroup[hoursAheadIndex].hoursAhead = hoursAhead;

    int newModel = getModelIndex(fci, modelName, &modelIndex);
    fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isActive = True;
    fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isReference = isReference;
    fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].tooMuchDataMissing = False;
    fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].modelName = fci->modelInfo[modelIndex].modelName;
    if(fci->verbose) {
        fprintf(stderr, "\tactivating %s for HA %d\n", fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].modelName, hoursAheadIndex + 1);
    }

    if(!newModel) // already registered this model
        return;

    if(isForecast) {
        fci->modelInfo[fci->numModelsRegistered].modelIndex = modelIndex;
        fci->modelInfo[fci->numModelsRegistered].hoursAheadIndex = hoursAheadIndex;
        //fci->modelInfo[fci->numModelsRegistered].modelName = strdup(modelName); //"ncep_RAP_DSWRF_1";  
        /*
                        if(strstr(fci->modelInfo[fci->numColumnInfoEntries].modelName, "HRRR") != NULL) {
                            fci->doHrrrGhiRecalc = True;
                    
                        }
         */
        sprintf(tempColDesc, "%s +%d hours", modelDescription, hoursAhead); //fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
        fci->modelInfo[fci->numModelsRegistered].modelDescription = strdup(tempColDesc); //"NCEP RAP GHI";  
        fci->modelInfo[fci->numModelsRegistered].maxhoursAhead = MAX(hoursAhead, fci->modelInfo[fci->numModelsRegistered].maxhoursAhead); // this will keep increasing

        fprintf(stderr, "registering %s HA=%d, HAindex=%d, modelIndex=%d\n", modelName, hoursAhead, hoursAheadIndex, fci->modelInfo[fci->numModelsRegistered].modelIndex);

        fci->maxModelIndex = MAX(fci->maxModelIndex, hoursAheadIndex); // the max hoursAheadIndex is the number of hour groups
        /*
        fprintf(stderr, "[%d] registering %s, data col index = %d, input col = %d, hour index = %d,", fci->numModels, fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].modelName,
                fci->numColumnInfoEntries-1, i, hoursAheadIndex);
        fprintf(stderr, "isActive=%d, isReference=%d, maxHoursAhead=%d\n", 
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].isActive,
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].isReference,
                fci->modelInfo[fci->numColumnInfoEntries].maxhoursAhead);
         */
        fci->numModelsRegistered++;
    }
        // not a forecast but we still need to set a few things
    else {

        for(i = 0; i < numFields; i++) {
            if(strncasecmp(fields[i], modelName, strlen(modelName)) == 0) { /* got a hit */
                //fci->modelInfo[fci->numModels].modelName = strdup(modelName); // ;                      
                fci->modelInfo[fci->numModels].modelDescription = strdup(modelDescription); //"NCEP RAP GHI";  
                fci->modelInfo[fci->numModels].maxhoursAhead = 0;
                fci->modelInfo[fci->numModels].hoursAheadIndex = -1;
                fci->modelInfo[fci->numModels].modelIndex = -1;
                fci->modelInfo[fci->numModels].inputColumnNumber = i; // this tells us the column numner of the input forecast table
                fci->numColumnInfoEntries++;
                //fprintf(stderr, "registering non-model %s, data index = %d, hour index = %d, input col = %d\n", fields[i], fci->numColumnInfoEntries-1, hoursAheadIndex, i);
            }
        }
    }

    //fci->numModels++;

}

int checkModelAgainstSite(forecastInputType *fci, char *modelName)
{
    int modelNum;

    //fprintf(stderr, "checking %s: number of models = %d\n", modelName, fci->thisSite->numModels);

    for(modelNum = 0; modelNum < fci->thisSite->numModels; modelNum++) {
        //fprintf(stderr, "checking %s against %s\n", modelName, fci->thisSite->modelNames[modelNum]);
        if(strcmp(modelName, fci->thisSite->modelNames[modelNum]) == 0) {
            return True;
        }
    }

    fprintf(stderr, "Note: model %s not turned on for site %s\n", modelName, fci->thisSite->siteName);
    return False;
}

void registerSiteModel(siteType *si, char *modelName, int maxHoursAhead)
{
    si->modelNames[si->numModels] = strdup(modelName);
    si->maxHoursAhead[si->numModels] = maxHoursAhead;
    si->numModels++;
}

void dumpHoursAfterSunrise(forecastInputType *fci)
{
    int hoursAheadIndex, hoursAfterSunriseIndex, ktIndex;
    modelRunType *modelRun;

    // print header
    fprintf(stderr, "\n\n#hours ahead");
    for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++)
        for(ktIndex = 0; ktIndex < fci->numKtBins; ktIndex++)
            fprintf(stderr, "\tHAS=%02d:ktBin=%d", hoursAfterSunriseIndex + 1, ktIndex);
    fprintf(stderr, "\n");

    for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex < fci->startHourHighIndex; hoursAheadIndex++) {
        fprintf(stderr, "%d", fci->hoursAfterSunriseGroup[hoursAheadIndex][0][0].hoursAhead);
        for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
            for(ktIndex = 0; ktIndex < fci->numKtBins; ktIndex++) {
                modelRun = &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex];
                fprintf(stderr, "\t%.1f", modelRun->optimizedPctRMSEphase2 * 100);
            }
            fprintf(stderr, "\n");
        }
    }
}

void dumpModelMix_EachModel_HAxHAS(forecastInputType *fci)
{
    //    for each model
    // +------ HAS -----
    // | %mix, %mix
    // H
    // A
    // |
    // |

    FILE *fp;
    char fileName[1024];
    char outBuff[1024 * 1024];
    char *buffPtr;
    int modelIndex, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, emptyOutput;
    //int hoursAhead, hoursAfterSunrise;
    modelRunType *modelRun;
    modelStatsType *err;

    if(fci->verbose)
        fprintf(stderr, "\nGenerating model mix percentage files by model...\n");

    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        buffPtr = outBuff; // reset memory buffer
        emptyOutput = True;

        // print header
        buffPtr += sprintf(buffPtr, "#For model %s: model percent by hours ahead (HA) and hours after sunrise (HAS)\n#siteName=%s,lat=%.2f,lon=%.3f,date span=%s-%s\n#HA,",
                getModelName(fci, modelIndex), genProxySiteName(fci), fci->multipleSites ? 999 : fci->thisSite->lat, fci->multipleSites ? 999 : fci->thisSite->lon, fci->startDateStr, fci->endDateStr);
        for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++)
            for(ktIndex = 0; ktIndex < fci->numKtBins; ktIndex++)
                buffPtr += sprintf(buffPtr, "HAS=%d:ktIndex=%d%c", hoursAfterSunriseIndex + 1, ktIndex, hoursAfterSunriseIndex == fci->maxHoursAfterSunrise - 1 ? '\n' : ',');

        for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex < fci->maxHoursAfterSunrise; hoursAheadIndex++) {
            if(fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].maskSwitchOn) {
                buffPtr += sprintf(buffPtr, "%d,", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
                for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
                    for(ktIndex = 0; ktIndex < fci->numKtBins; ktIndex++) {
                        modelRun = &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex];
                        err = &modelRun->hourlyModelStats[modelIndex];
                        if(isContributingModel(err)) {
                            buffPtr += sprintf(buffPtr, "%d%c", fci->skipPhase2 ? err->optimizedWeightPhase1 : err->optimizedWeightPhase2, hoursAfterSunriseIndex == fci->maxHoursAfterSunrise - 1 ? '\n' : ',');
                            emptyOutput = False;
                        }
                    }
                }
            }
        }
        if(!emptyOutput) {
            sprintf(fileName, "%s/%s.ModelMixBy.HA_HAS.%s.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), getModelName(fci, modelIndex), fci->modelPermutations.currentPermutationIndex);
            if((fp = fopen(fileName, "w")) == NULL) {
                sprintf(ErrStr, "Couldn't open file %s : %s", fileName, strerror(errno));
                FatalError("dumpModelMix_EachModel_HAxHAS()", ErrStr);
            }
            fprintf(fp, outBuff);
            fclose(fp);
        }
    }
}

int isContributingModel(modelStatsType *model)
{
    // the current model is active if:
    // 1) maskSwitchOn is set
    // 2) it's not a reference model (such as persistence)
    // 3) it hasn't been shut off because of too much missing data
    return (model->maskSwitchOn && !model->isReference);
}

void dumpModelMix_EachHAS_HAxModel(forecastInputType *fci)
{
    //    for each HAS
    // +------ model -----
    // | %mix, %mix, ...
    // H
    // A
    // |
    // |

    FILE *fp;
    char fileName[1024];
    int modelIndex, hoursAheadIndex, hoursAfterSunriseIndex, ktIndex;
    //int hoursAhead, hoursAfterSunrise;
    modelRunType *modelRun;
    modelStatsType *err;

    if(fci->verbose)
        fprintf(stderr, "\nGenerating model mix percentage files by hours after sunrise...\n");

    for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
        for(ktIndex = 0; ktIndex < fci->numKtBins; ktIndex++) {
            sprintf(fileName, "%s/%s.percentByHAS.HA_Model.HAS=%d.KTI=%d.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), hoursAfterSunriseIndex + 1, ktIndex, fci->modelPermutations.currentPermutationIndex);
            if((fp = fopen(fileName, "w")) == NULL) {
                sprintf(ErrStr, "Couldn't open file %s : %s", fileName, strerror(errno));
                FatalError("dumpModelMix_EachModel_HAxHAS()", ErrStr);
            }

            // print header lines
            fprintf(fp, "#For hours after sunrise=%d: model percent by hours ahead and model type\n#siteName=%s,lat=%.2f,lon=%.3f,date span=%s-%s\n#hoursAhead,",
                    hoursAfterSunriseIndex + 1, genProxySiteName(fci), fci->multipleSites ? 999 : fci->thisSite->lat, fci->multipleSites ? 999 : fci->thisSite->lon, fci->startDateStr, fci->endDateStr);
            for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++)
                fprintf(fp, "%s%c", getModelName(fci, modelIndex), modelIndex == fci->numModels - 1 ? '\n' : ',');

            for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex < fci->maxHoursAfterSunrise; hoursAheadIndex++) {
                fprintf(fp, "%d,", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
                for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
                    modelRun = &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex];
                    err = &modelRun->hourlyModelStats[modelIndex];
                    if(isContributingModel(err))
                        fprintf(fp, "%d%c", fci->skipPhase2 ? err->optimizedWeightPhase1 : err->optimizedWeightPhase2, modelIndex == fci->numModels - 1 ? '\n' : ',');
                    else
                        fprintf(fp, "NA%c", modelIndex == fci->numModels - 1 ? '\n' : ',');
                }
            }
            fclose(fp);
        }
    }
}

void printByHour(forecastInputType *fci)
{
    FILE *fp;
    char fileName[1024];
    int modelIndex, hoursAheadIndex;
    modelRunType *modelRun;
    modelStatsType *err;

    for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        modelRun = &fci->hoursAheadGroup[hoursAheadIndex];

        sprintf(fileName, "%s/%s.forecast.error.hoursAhead=%02d.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), modelRun->hoursAhead, fci->modelPermutations.currentPermutationIndex);
        //fprintf(stderr, "hour[%d] hour=%d file=%s\n", hoursAheadIndex, modelRun->hoursAhead, fileName);
        fp = fopen(fileName, "w");

        fprintf(fp, "#siteName=%s,lat=%.2f,lon=%.2f,hours ahead=%d,N=%d,mean measured GHI=%.1f,date span=%s-%s\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->thisSite->lat, fci->multipleSites ? 999 : fci->thisSite->lon,
                modelRun->hoursAhead, modelRun->numValidSamples, modelRun->meanMeasuredGHI, fci->startDateStr, fci->endDateStr);
        fprintf(fp, "#model,sum( model-ground ), sum( abs(model-ground) ), sum( (model-ground)^2 ), mae, mae percent, mbe, mbe percent, rmse, rmse percent\n");

        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            err = &modelRun->hourlyModelStats[modelIndex];
            fprintf(fp, "%s,%.0f,%.0f,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", fci->modelInfo[modelIndex].modelDescription, err->sumModel_Ground, err->sumAbs_Model_Ground, err->sumModel_Ground_2,
                    err->mae, err->maePct * 100, err->mbe, err->mbePct * 100, err->rmse, err->rmsePct * 100);
        }
        fclose(fp);
    }
}

void printByModel(forecastInputType *fci)
{
    FILE *fp;
    char fileName[1024];
    int modelIndex, hoursAheadIndex;
    modelRunType *modelRun;
    modelStatsType *err;

    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        sprintf(fileName, "%s/%s.forecast.error.model=%s.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), getModelName(fci, modelIndex), fci->modelPermutations.currentPermutationIndex);
        fp = fopen(fileName, "w");


        fprintf(fp, "#siteName=%s,lat=%.2f,lon=%.2f,error analysis=%s, date span=%s-%s\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->thisSite->lat, fci->multipleSites ? 999 : fci->thisSite->lon,
                getModelName(fci, modelIndex), fci->startDateStr, fci->endDateStr);
        fprintf(fp, "#model,N,hours ahead, mean measured GHI, sum( model-ground ), sum( abs(model-ground) ), sum( (model-ground)^2 ), mae, mae percent, mbe, mbe percent, rmse, rmse percent\n");

        for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
            modelRun = &fci->hoursAheadGroup[hoursAheadIndex];

            err = &modelRun->hourlyModelStats[modelIndex];
            fprintf(fp, "%s,%d,%d,%.1f,%.0f,%.0f,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", fci->modelInfo[modelIndex].modelDescription,
                    modelRun->numValidSamples, fci->hoursAheadGroup[hoursAheadIndex].hoursAhead, modelRun->meanMeasuredGHI, err->sumModel_Ground, err->sumAbs_Model_Ground, err->sumModel_Ground_2,
                    err->mae, err->maePct * 100, err->mbe, err->mbePct * 100, err->rmse, err->rmsePct * 100);
        }
        fclose(fp);
    }
}

void printByAnalysisType(forecastInputType *fci)
{
    FILE *fp;
    int modelIndex, hoursAheadIndex;
    modelRunType *modelRun;
    modelStatsType *err;

#ifdef PRINT_ALL_ANALYSIS_TYPES
    if(!(fp = openErrorTypeFile(fci, "individualModelError.MAE")))
        return;

    for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        modelRun = &fci->hoursAheadGroup[hoursAheadIndex];
        fprintf(fp, "%d,%d", modelRun->hoursAhead, modelRun->numValidSamples);
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            err = &modelRun->hourlyModelStats[modelIndex];
            fprintf(fp, ",%.1f%%", err->maePct * 100);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);

    if(!(fp = openErrorTypeFile(fci, "individualModelError.MBE")))
        return;

    for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        modelRun = &fci->hoursAheadGroup[hoursAheadIndex];
        fprintf(fp, "%d,%d", modelRun->hoursAhead, modelRun->numValidSamples);
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            err = &modelRun->hourlyModelStats[modelIndex];
            fprintf(fp, ",%.1f%%", err->mbePct * 100);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
#endif
    if(!(fp = openErrorTypeFile(fci, "individualModelError.RMSE")))
        return;

    for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        //modelRun = &fci->hoursAheadGroup[hoursAheadIndex];
        modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][0][0] : &fci->hoursAheadGroup[hoursAheadIndex];

        fprintf(fp, "%d,%d", modelRun->hoursAhead, modelRun->numValidSamples);
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            err = &modelRun->hourlyModelStats[modelIndex];
            fprintf(fp, ",%.1f%%", err->rmsePct * 100);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}

void printRmseTableHour(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex)
{
    FILE *fp;
    int modelIndex;
    modelRunType *modelRun;
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    if(!(fp = openErrorTypeFileHourly(fci, "RMSE", hoursAheadIndex, hoursAfterSunriseIndex, ktIndex)))
        return;
    //fp = stderr;

    //for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
    //fprintf(fp, "hours ahead = %d", modelRun->hoursAhead);
    //if(hoursAfterSunriseIndex >= 0)
    //    fprintf(fp, ", hours after sunrise = %d", modelRun->hoursAfterSunrise);
    fprintf(fp, "\nN = %d\n", modelRun->numValidSamples);
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn)
            fprintf(fp, "%-15s = %.1f%%\n", getModelName(fci, modelIndex), modelRun->hourlyModelStats[modelIndex].rmsePct * 100);
        else if(modelRun->hourlyModelStats[modelIndex].isActive)
            fprintf(fp, "%-15s   [disabled]\n", getModelName(fci, modelIndex));
    }
    fprintf(fp, "\n");
    //}    
    //fclose(fp);   
}

FILE *openErrorTypeFileHourly(forecastInputType *fci, char *analysisType, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex)
{
    char fileName[1024];
    static FILE *fp;
    //    int modelIndex;
    char satGHIerr[1024];
    //    modelRunType *modelRun = &fci->hoursAheadGroup[hoursAheadIndex];
    modelRunType *modelRun;

    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    if(fci->outputDirectory == NULL || fci->thisSite->siteName == NULL) {
        fprintf(stderr, "openErrorTypeFile(): got null outputDirectory or siteName\n");
        return NULL;
    }

    if(strcasecmp(analysisType, "rmse") == 0)
        sprintf(satGHIerr, "sat GHI RMSE=%.1f%%", modelRun->satModelStats.rmsePct * 100);
    else if(strcasecmp(analysisType, "mae") == 0)
        sprintf(satGHIerr, "sat GHI MAE=%.1f%%", modelRun->satModelStats.maePct * 100);
    else
        sprintf(satGHIerr, "sat GHI MBE=%.1f%%", modelRun->satModelStats.mbePct * 100);


    sprintf(fileName, "%s/%s.forecast.analysisType=%s.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), analysisType, fci->modelPermutations.currentPermutationIndex);
    //fp = fopen(fileName, "w");
    fp = stderr;

    fprintf(fp, "siteName=%s\nanalysisType=%s\n%s\n", genProxySiteName(fci), analysisType, satGHIerr);
    /*
        fprintf(fp, "hours ahead,N,");
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++)  {
            if(modelRun->hourlyModelStats[modelIndex].isActive)
                fprintf(fp, "%s,", getModelName(fci, modelIndex));
        }
        fprintf(fp, "\n");
     */

    return fp;
}

FILE *openErrorTypeFile(forecastInputType *fci, char *fileNameStr)
{
    char fileName[1024];
    static FILE *fp;
    int modelIndex;
    char satGHIerr[1024];

    if(fci->outputDirectory == NULL || fci->thisSite->siteName == NULL) {
        fprintf(stderr, "openErrorTypeFile(): got null outputDirectory or siteName\n");
        return NULL;
    }

    if(strcasecmp(fileNameStr, "rmse") == 0)
        sprintf(satGHIerr, "sat GHI RMSE=%.1f%%", fci->hoursAheadGroup[0].satModelStats.rmsePct * 100);
    else if(strcasecmp(fileNameStr, "mae") == 0)
        sprintf(satGHIerr, "sat GHI MAE=%.1f%%", fci->hoursAheadGroup[0].satModelStats.maePct * 100);
    else
        sprintf(satGHIerr, "sat GHI MBE=%.1f%%", fci->hoursAheadGroup[0].satModelStats.mbePct * 100);


    sprintf(fileName, "%s/%s.%s.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), fileNameStr, fci->modelPermutations.currentPermutationIndex);
    fp = fopen(fileName, "w");

    fprintf(fp, "#siteName=%s,lat=%.2f,lon=%.2f,%s,%s,date span=%s-%s\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->thisSite->lat, fci->multipleSites ? 999 : fci->thisSite->lon, fileNameStr, satGHIerr, fci->startDateStr, fci->endDateStr);
    fprintf(fp, "#hours ahead,N,");
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++)
        fprintf(fp, "%s,", getModelName(fci, modelIndex));
    fprintf(fp, "\n");

    return fp;
}

int getMaxHoursAhead(forecastInputType *fci, int modelIndex)
{
    return (fci->modelInfo[modelIndex].maxhoursAhead);
}

char *getModelName(forecastInputType *fci, int modelIndex)
{
    if(modelIndex < 0)
        return ("satellite");
    if(modelIndex >= fci->numModels) {
        fprintf(stderr, "getModelName(): Internal Error: got modelIndex = %d when numModels = %d", modelIndex, fci->numModels);
        exit(1);
    }
    return (fci->modelInfo[modelIndex].modelName);
}

char *getColumnNameByHourModel(forecastInputType *fci, int hrInd, int modInd)
{
    int col;
    static char modelDesc[1024];

    // this should probably be solved in a data structure
    for(col = 0; col < fci->numColumnInfoEntries; col++) {
        if(fci->modelInfo[col].hoursAheadIndex == hrInd && fci->modelInfo[col].modelIndex == modInd) {
            strcpy(modelDesc, fci->modelInfo[col].modelName);
            modelDesc[strlen(modelDesc) - 2] = '\0';
            return modelDesc;
        }
    }

    return NULL;
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
        fprintf(stderr, "Got bad high index to -r flag\n");
        return False;
    }

    fci->startHourLowIndex = atoi(lowStr);
    fci->startHourHighIndex = atoi(highStr);

    if(fci->startHourHighIndex < 0 || fci->startHourHighIndex > 100) {
        fprintf(stderr, "Got bad high index to -r flag\n");
        return False;
    }
    if(fci->startHourLowIndex < 0 || fci->startHourLowIndex > 100) {
        fprintf(stderr, "Got bad low index to -r flag\n");
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
        fprintf(stderr, "Got bad start and/or end dates for -a flag\n");
        return False;
    }

    strncpy(backup, optarg, 256);
    startStr = endStr = backup;

    //    start = &sat->archiverStartDate;
    //    end = &sat->archiverEndDate;

    start = &fci->startDate;
    end = &fci->endDate;
    start->year = start->month = start->hour = start->min = 0; // in case we get a short date
    end->year = end->month = end->hour = end->min = 0; // in case we get a short date

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
    setObsTime(start); // calculate time_t and doy numbers

    if(!dateTimeSanityCheck(start)) {
        fprintf(stderr, "parseDates(): error in start date %s in argument string %s\n", startStr, backup);
        return (False);
    }

    sscanf(endStr, "%4d%02d%02d%02d%02d", &end->year, &end->month, &end->day, &end->hour, &end->min);
    setObsTime(end);

    if(!dateTimeSanityCheck(end)) {
        fprintf(stderr, "parseDates(): error in end date %s in argument string %s\n", endStr, backup);
        return (False);
    }

    fci->startDateStr = strdup(dtToStringDateOnly(&fci->startDate));
    fci->endDateStr = strdup(dtToStringDateOnly(&fci->endDate));


    return True;
}

#define getModelN(modelIndex) (modelIndex < 0 ? (modelRun->satModelStats.N) : (modelRun->hourlyModelStats[modelIndex].N))

void printHourlySummary(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex)
{
    int modelIndex, hoursAhead = fci->hoursAheadGroup[hoursAheadIndex].hoursAhead;
    int hoursAfterSunrise = hoursAfterSunriseIndex + 1;
    modelRunType *modelRun;
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    fprintf(stderr, "HR%d=== Summary for hours ahead %d ", hoursAhead, hoursAhead);
    if(hoursAfterSunriseIndex >= 0)
        fprintf(stderr, ", hours after sunrise %d, ktIndex %d ===\n", hoursAfterSunrise, ktIndex);
    fprintf(stderr, "HR%d\t%-15s = %d\n", hoursAhead, "N for group", modelRun->numValidSamples);
    fprintf(stderr, "HR%d\t%-15s = %d\n", hoursAhead, "ground GHI", modelRun->ground_N);
    for(modelIndex = -1; modelIndex < fci->numModels; modelIndex++) {
        if(modelIndex < 0 || modelRun->hourlyModelStats[modelIndex].maskSwitchOn)
            fprintf(stderr, "HR%d\t%-15s = %d\n", hoursAhead, getModelName(fci, modelIndex), getModelN(modelIndex));
    }
}

void printHoursAheadSummaryCsv(forecastInputType *fci)
{
    int hoursAheadIndex, modelIndex;
    modelRunType *modelRun;
    char modelName[1024], tempFileName[2048];

    //sprintf(filename, "%s/%s.wtRange=%.2f-%.2f_ha=%d-%d.csv", fci->outputDirectory, fci->thisSite->siteName, fci->weightSumLowCutoff, fci->weightSumHighCutoff, fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead);
    sprintf(tempFileName, "%s/forecastSummary.%s.%s-%s.div=%d.hours=%d-%d.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), dtToStringDateOnly(&fci->startDate), dtToStringDateOnly(&fci->endDate), fci->numDivisions, fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead, fci->modelPermutations.currentPermutationIndex);
    fci->summaryFile.fileName = strdup(tempFileName);

    if((fci->summaryFile.fp = fopen(fci->summaryFile.fileName, "w")) == NULL) {
        sprintf(ErrStr, "Couldn't open file %s : %s", fci->summaryFile.fileName, strerror(errno));
        FatalError("printHoursAheadSummaryCsv()", ErrStr);
    }
    // print the header
    fprintf(fci->summaryFile.fp, "#site=%s lat=%.3f lon=%.3f divisions=%d date span=%s-%s\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->thisSite->lat, fci->multipleSites ? 999 : fci->thisSite->lon, fci->numDivisions, fci->startDateStr, fci->endDateStr);
    fprintf(fci->summaryFile.fp, "#hoursAhead,group N,sat RMSE,p1RMSE,p2RMSE,p1SumWts calls,p1RMSE calls,p2SumWts calls,p2RMSE calls");
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        strcpy(modelName, getModelName(fci, modelIndex));
        fprintf(fci->summaryFile.fp, ",%s model, %s status,%s N,%s RMSE,%s %s,%s %s", modelName, modelName, modelName, modelName,
                modelName, WEIGHT_1_STR, modelName, WEIGHT_2_STR);
    }
    fprintf(fci->summaryFile.fp, "\n");

    // generate model results
    for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        modelRun = &fci->hoursAheadGroup[hoursAheadIndex];
        //        modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

        fprintf(fci->summaryFile.fp, "%d,%d,%.2f,%.2f,%.2f,%ld,%ld,%ld,%ld", modelRun->hoursAhead, modelRun->numValidSamples, modelRun->satModelStats.rmsePct * 100,
                modelRun->optimizedPctRMSEphase1 * 100, modelRun->optimizedPctRMSEphase2 * 100, modelRun->phase1SumWeightsCalls, modelRun->phase1RMSEcalls, modelRun->phase2SumWeightsCalls, modelRun->phase2RMSEcalls);
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            //if(modelRun->hourlyModelStats[modelIndex].maskSwitchOn) {
            fprintf(fci->summaryFile.fp, ",%s", modelRun->hourlyModelStats[modelIndex].isReference ? "reference" : "forecast"
                    );
            fprintf(fci->summaryFile.fp, ",%s", modelRun->hourlyModelStats[modelIndex].maskSwitchOn ? "on" : "off");
            fprintf(fci->summaryFile.fp, ",%d", modelRun->hourlyModelStats[modelIndex].maskSwitchOn ? modelRun->hourlyModelStats[modelIndex].N : -999);
            fprintf(fci->summaryFile.fp, ",%.2f", modelRun->hourlyModelStats[modelIndex].maskSwitchOn ? modelRun->hourlyModelStats[modelIndex].rmsePct * 100 : -999);
            fprintf(fci->summaryFile.fp, ",%d", modelRun->hourlyModelStats[modelIndex].maskSwitchOn ? modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase1 : -999);
            fprintf(fci->summaryFile.fp, ",%d", modelRun->hourlyModelStats[modelIndex].maskSwitchOn ? modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase2 : -999);
            //}
        }
        fprintf(fci->summaryFile.fp, "\n");
    }

    fclose(fci->summaryFile.fp);
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
        sprintf(ErrStr, "Couldn't open summary file %s : %s", fci->summaryFile.fileName, strerror(errno));
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
    numFields = split(line, fields, MAX_FIELDS, " "); /* split line */
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
    //fci->thisSite->siteName = strdup(fields[1]);

    // latitude
    numFields = split(latStr, fields, MAX_FIELDS, "=");
    if(numFields != 2) {
        fprintf(stderr, "Error parsing %s: header line 1: can't figure latitude: %s\n", fci->summaryFile.fileName, saveLine);
        exit(1);
    }
    fci->thisSite->lat = atof(fields[1]);
    if(fci->thisSite->lat < -90 || fci->thisSite->lat > 90) {
        fprintf(stderr, "Error parsing %s: header line 1: can't figure latitude: %s\n", fci->summaryFile.fileName, saveLine);
        exit(1);
    }

    // longitude
    numFields = split(lonStr, fields, MAX_FIELDS, "=");
    if(numFields != 2) {
        fprintf(stderr, "Error parsing %s: header line 1: can't figure longitude: %s\n", fci->summaryFile.fileName, saveLine);
        exit(1);
    }
    fci->thisSite->lon = atof(fields[1]);
    if(fci->thisSite->lat < -180 || fci->thisSite->lat > 180) {
        fprintf(stderr, "Error parsing %s: header line 1: can't figure longitude: %s\n", fci->summaryFile.fileName, saveLine);
        exit(1);
    }

    // now we have to parse the column names line (example file: Penn_State_PA.wtRange=0.95-1.05_ha=1-36.csv)
    // #hoursAhead,group N,sat RMSE,phase 1 RMSE,phase 2 RMSE,phase 1 RMSE calls,phase 2 RMSE calls,ncep_RAP_DSWRF model, ncep_RAP_DSWRF status,ncep_RAP_DSWRF N,ncep_RAP_DSWRF RMSE,ncep_RAP_DSWRF Weight 1,ncep_RAP_DSWRF weight 2,persistence model, persistence status,persistence N,persistence RMSE,persistence Weight 1,persistence weight 2,ncep_NAM_hires_DSWRF_inst model, ncep_NAM_hires_DSWRF_inst status,ncep_NAM_hires_DSWRF_inst N,ncep_NAM_hires_DSWRF_inst RMSE,ncep_NAM_hires_DSWRF_inst Weight 1,ncep_NAM_hires_DSWRF_inst weight 2,ncep_NAM_DSWRF model, ncep_NAM_DSWRF status,ncep_NAM_DSWRF N,ncep_NAM_DSWRF RMSE,ncep_NAM_DSWRF Weight 1,ncep_NAM_DSWRF weight 2,ncep_GFS_sfc_DSWRF_surface_avg model, ncep_GFS_sfc_DSWRF_surface_avg status,ncep_GFS_sfc_DSWRF_surface_avg N,ncep_GFS_sfc_DSWRF_surface_avg RMSE,ncep_GFS_sfc_DSWRF_surface_avg Weight 1,ncep_GFS_sfc_DSWRF_surface_avg weight 2,ncep_GFS_sfc_DSWRF_surface_inst model, ncep_GFS_sfc_DSWRF_surface_inst status,ncep_GFS_sfc_DSWRF_surface_inst N,ncep_GFS_sfc_DSWRF_surface_inst RMSE,ncep_GFS_sfc_DSWRF_surface_inst Weight 1,ncep_GFS_sfc_DSWRF_surface_inst weight 2,ncep_GFS_DSWRF model, ncep_GFS_DSWRF status,ncep_GFS_DSWRF N,ncep_GFS_DSWRF RMSE,ncep_GFS_DSWRF Weight 1,ncep_GFS_DSWRF weight 2,NDFD_global model, NDFD_global status,NDFD_global N,NDFD_global RMSE,NDFD_global Weight 1,NDFD_global weight 2,cm model, cm status,cm N,cm RMSE,cm Weight 1,cm weight 2,ecmwf_ghi model, ecmwf_ghi status,ecmwf_ghi N,ecmwf_ghi RMSE,ecmwf_ghi Weight 1,ecmwf_ghi weight 2

    char *forecastHeaderLine = line + 1; // skip # char
    fgets(forecastHeaderLine, LINE_LENGTH, fp);

    numFields = split(line, fields, MAX_FIELDS, ","); /* split line */

    for(i = 0; i < numFields; i++) {


        if(strstr(fields[i], WEIGHT_2_STR) != NULL) {
            fprintf(stderr, "\tgot %s\n", fields[i]);
        }
    }

    fci->forecastLineNumber = 2;

    //modelRunType *modelRun;

    while(fgets(line, LINE_LENGTH, fp)) {
        fci->forecastLineNumber++;

        numFields = split(line, fields, MAX_FIELDS, ","); /* split line */
        if(numFields < 20) {
            sprintf(ErrStr, "Scanning line %d of file %s, got %d columns using delimiter \"%s\" (expecting at least 20).\nEither delimiter flag or -s arg is wrong", fci->forecastLineNumber, fci->forecastTableFile.fileName, numFields, fci->delimiter);
            FatalError("readSummaryFile()", ErrStr);
        }

        //int hoursAhead = atoi(fields[0]);
        //modelRun = &fci->hoursAheadGroup[hoursAheadIndex];

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

                if(!fci->multipleSites && strcasecmp(fldPtr, fci->thisSite->siteName) != 0) {
                    fprintf(fci->warningsFile.fp, "Warning: siteName changed from %s to %s\n", fci->thisSite->siteName, fldPtr);
                }
                
                // lat & lon
                lat = atof(fields[2]);
                lon = atof(fields[3]);

                if(fci->thisSite->lat == -999 || fci->thisSite->lon == -999) {
                    fci->thisSite->lat = lat;
                    fci->thisSite->lon = lon;
                }
                else {
                    if(!fci->multipleSites && fabs(lat - fci->thisSite->lat) > 0.01) {
                        fprintf(fci->warningsFile.fp, "Warning: latitude changed from %.3f to %.3f\n", fci->thisSite->lat, lat);
                    }
                    if(!fci->multipleSites && fabs(lon - fci->thisSite->lon) > 0.01) {
                        fprintf(fci->warningsFile.fp, "Warning: longitude changed from %.3f to %.3f\n", fci->thisSite->lon, lon);
                    }        
                }
        
                (void) readDataFromLine(fci, fields);
        

                if(fci->numValidSamples == 3)
                    return True;
         */

        fci->numInputRecords++;

    }

    return (True);
}

char *genProxySiteName(forecastInputType *fci)
{
    static char returnSite[1024];
    if(fci->multipleSites)
        sprintf(returnSite, "MultiSite-sites=%d", fci->numSites);
    else
        strcpy(returnSite, fci->thisSite->siteName);

    return returnSite;
}

int getHoursAheadIndex(forecastInputType *fci, int hoursAhead)
{
    int hoursAheadIndex;

    for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        if(fci->hoursAheadGroup[hoursAheadIndex].hoursAhead == hoursAhead)
            return hoursAheadIndex;
    }
    return -1;
}

int getHoursAfterSunriseIndex(forecastInputType *fci, int hoursAfterSunrise)
{
    int hoursAfterSunriseIndex;

    for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex <= fci->startHourHighIndex; hoursAfterSunriseIndex++) {
        if(fci->hoursAfterSunriseGroup[0][hoursAfterSunriseIndex][0].hoursAfterSunrise == hoursAfterSunrise)
            return hoursAfterSunriseIndex;
    }
    return -1;
}

int getModelIndexOld(forecastInputType *fci, char *modelName)
{
    int modelIndex;
    char *currName;

    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        currName = getModelName(fci, modelIndex);
        if(strcmp(currName, modelName) == 0)
            return modelIndex;
    }
    return -1;
}

char *parseEquals(char *inString)
{
    char *fields[MAX_FIELDS], saveStr[LINE_LENGTH];
    int numFields;

    strcpy(saveStr, inString);
    numFields = split(inString, fields, MAX_FIELDS, "=");
    if(numFields != 2) {
        fprintf(stderr, "Error parsing header line: can't figure siteName: %s\n", saveStr);
        return NULL;
    }

    return fields[1];
}

void initPermutationSwitches(forecastInputType *fci)
{
    permutationType *perm = &fci->modelPermutations;
    int p;

    perm->numPermutations = pow(2, fci->numModels);

    /*
     for numModels = 5, numPermutations = 32
     bitfields for permutationIndex would look like:
        1 = 0000 0001
        2 = 0000 0010
        3 = 0000 0011
        [...]
       23 = 0001 0111
       24 = 0001 1000
       25 = 0001 1001
       26 = 0001 1010
        [...]
       31 = 0001 1111  <= all models on
     */
    for(p = 0; p < fci->numModels; p++) {
        perm->masks[p] = 1 << p; // shift binary 1 left to form mask
        //        fprintf(stderr, "mask for %d : %X\n", p, perm->masks[p]);
    }

    perm->currentPermutationIndex = perm->numPermutations - 1;
}

// for informational purposes
#define ModelMask1  0x001
#define ModelMask2  0x002
#define ModelMask3  0x004
#define ModelMask4  0x008
#define ModelMask5  0x010
#define ModelMask6  0x020
#define ModelMask7  0x040
#define ModelMask8  0x080
#define ModelMask9  0x100
#define ModelMask10 0x200

void setPermutationSwitches(forecastInputType *fci, int permutationIndex)
{
    int i;
    permutationType *perm = &fci->modelPermutations;

    if(perm->numPermutations == 0)
        initPermutationSwitches(fci);

    if(permutationIndex >= perm->numPermutations || permutationIndex < 1) {
        FatalError("setPermutationSwitches()", "Got permutation index out of range.");
    }

    perm->currentPermutationIndex = permutationIndex;

    for(i = 0; i < fci->numModels; i++) {
        perm->modelSwitches[i] = permutationIndex & perm->masks[i];
    }

    fprintf(stderr, "for permutationIndex %d: ", permutationIndex);
    for(i = fci->numModels - 1; i >= 0; i--) {
        fprintf(stderr, "%c", perm->modelSwitches[i] ? '1' : '0');
    }
    fprintf(stderr, "\n");
}

void setModelSwitches(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int ktIndex, int permutationIndex)
{
    int i, maskInd;
    permutationType *perm = &fci->modelPermutations;
    modelRunType *modelRun;
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex][ktIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
    //#define DUMP_MASK
#ifdef DUMP_MASK
    static int firstTime = 1;
    if(firstTime) {
        firstTime = 0;
        fprintf(stderr, "SMS:#HA,HAS,KTI,permInd");
        for(i = 0, maskInd = 0; i < fci->numModels; i++, maskInd++) {
            fprintf(stderr, ",%sMask", getModelName(fci, i));
            fprintf(stderr, ",%sActive", getModelName(fci, i));
        }
        fprintf(stderr, "\n");
    }
#endif

    perm->currentPermutationIndex = permutationIndex;

#ifdef DUMP_MASK
    fprintf(stderr, "SMS:%d,%d,%d,%d", hoursAheadIndex, hoursAfterSunriseIndex, ktIndex, permutationIndex);
#endif
    for(i = 0, maskInd = 0; i < fci->numModels; i++, maskInd++) {
        // if model is active (i.e., there's data) and not a reference model
        modelRun->hourlyModelStats[i].maskSwitchOn = False;
        if(modelRun->hourlyModelStats[i].isActive) {
            if(modelRun->hourlyModelStats[i].isReference) {
                modelRun->hourlyModelStats[i].maskSwitchOn = True;
                maskInd--; // not a maskable model so keep the maskInd the same
            }
            else {
                modelRun->hourlyModelStats[i].maskSwitchOn = (permutationIndex & perm->masks[maskInd]) ? True : False;
            }
        }
#ifdef DUMP_MASK
        fprintf(stderr, ",%s", modelRun->hourlyModelStats[i].maskSwitchOn ? "on" : "off");
        fprintf(stderr, ",%s", modelRun->hourlyModelStats[i].isActive ? "yes" : "no");
#endif

    }
#ifdef DUMP_MASK
    fprintf(stderr, "\n");
#endif
}

char *validString(validType code)
{
    switch(code) {
        case zenith: return "zenith";
        case groundLow: return "groundLow";
        case satLow: return "satLow";
        case nwpLow: return "nwpLow";
        case notHAS: return "notHAS";
        case notKt: return "notKt";
        case OK: return "OK";
        default: FatalError("validString()", "code out of range");
    }

    return NULL;
}

int codeIsOK(validType code)
{
    return (code == OK);
}