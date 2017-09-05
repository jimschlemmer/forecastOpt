#include "forecastOpt.h"
#include "forecastOptUtils.h"

char ErrStr[4096];

int allocatedSamples, HashLineNumber = 1;

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

int readNWPforecastData(forecastInputType *fci)
{
    return True;
}

// surface.bondville.2015-2016.GHI.csv

int readSurfaceData(forecastInputType *fci)
{
    return True;
}

// v3.desertRock.2015-2016.GHI.csv

int readV3data(forecastInputType *fci)
{
    return True;
}

// [sunspot]:/home/jim/satmod/auxillaryData/clearsky/north_america_00/GHI
// north_america.20080101.001.000000.clearsky.grid

int readClearskyData(forecastInputType *fci)
{
    return True;
}

int readForecastData(forecastInputType *fci)
{
    char line[1024];
    int numFields;
    char *fields[MAX_FIELDS];
    dateTimeType currDate;
    timeSeriesType *thisSample;
    int hoursAheadIndex, numFiles = 0;
    char pattern[256], **filenames = NULL, tempFilename[1024];

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

    for(hoursAheadIndex=0; hoursAheadIndex < fci->numInputFiles; hoursAheadIndex++) {
        // open input file
        if((fci->inputFiles[hoursAheadIndex].fp = fopen(fci->inputFiles[hoursAheadIndex].fileName, "r")) == NULL) {
            sprintf(ErrStr, "Couldn't open warnings file %s: %s", fci->inputFiles[hoursAheadIndex].fileName, strerror(errno));
            FatalError("readForecastData()", ErrStr);
        }

        // process header lines
        fgets(line, LINE_LENGTH, fci->inputFiles[hoursAheadIndex].fp);
        fci->forecastHeaderLine1 = strdup(line); // save for later       
        fgets(line, LINE_LENGTH, fci->inputFiles[hoursAheadIndex].fp);
        fci->forecastHeaderLine2 = strdup(line); // save for later       
        parseNwpHeaderLine(fci);

        fci->forecastLineNumber = 2;

        while(fgets(line, LINE_LENGTH, fci->inputFiles[hoursAheadIndex].fp)) {
            fci->forecastLineNumber++;

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
            thisSample = getNextTimeSeriesSample(fci, hoursAheadIndex); // &fci->nwpTimeSeries[hoursAheadIndex].timeSeries[numTimeSeriesSamples];
            thisSample->hoursAheadIndex = hoursAheadIndex;
            thisSample->dateTime = currDate;

            //#define USE_ZENITH_INSTEAD_OF_HAS
            // compute hours after sunrise this date/time
            thisSample->sunrise = calculateSunrise(&currDate, fci->lat, fci->lon); // Get the sunrise time for this day
            thisSample->zenith = calculateZenithAngle(&currDate, fci->lat, fci->lon);

            readDataFromLine(fci, thisSample, fields, numFields); // get the forecast data

            if(thisSample->sunIsUp) {
                // it seems as though the sunae zenith angle is running a fraction ahead (~30 sec) the Surfrad zenith angles
                if(thisSample->zenith < 0 || thisSample->zenith > 90) {
                    sprintf(ErrStr, "Got bad zenith angle at line %d: %.2f", fci->forecastLineNumber, thisSample->zenith);
                    FatalError("readForecastData()", ErrStr);
                }

#ifdef USE_ZENITH_INSTEAD_OF_HAS
                thisSample->hoursAfterSunrise = (int) ((90 - thisSample->zenith) / 10) + 1; // 9 buckets of 10 degrees each
#else
                thisSample->hoursAfterSunrise = thisSample->dateTime.hour - thisSample->sunrise.hour;
                if(thisSample->hoursAfterSunrise < 1)
                    thisSample->hoursAfterSunrise += 24;
                if(thisSample->hoursAfterSunrise >= 24)
                    thisSample->hoursAfterSunrise = 1;

#ifdef DEBUG_HAS
                fprintf(stderr, "%s,%.4f,%.4f,time=%s,", fci->siteName, fci->lat, fci->lon, dtToStringDateTime(&thisSample->dateTime));
                fprintf(stderr, "sunrise=%s,hoursAfterSunrise=%d\n", dtToStringDateTime(&thisSample->sunrise), thisSample->hoursAfterSunrise);
#endif

#endif
            }
            else
                thisSample->hoursAfterSunrise = -1;

            fci->numInputRecords++;
        }
    }


    studyData(fci);

    fci->gotForecastFile = True;

    return True;
}

void setSiteInfo(forecastInputType *fci, char *site)
{
    char tempFileName[1024];

    fci->siteName = strdup(site);
    fci->siteGroup = fci->siteName; // for now make these the same
    fprintf(stderr, "=== Setting site to %s ===\n", fci->siteName);

    fci->thisSite = &fci->allSiteInfo[fci->numSites];

    // Now that we know the site name we can open the warnings file
    if(fci->multipleSites)
        sprintf(tempFileName, "%s/MultiSite-site%d-%s.warnings.perm%02d.txt", fci->outputDirectory, fci->numSites + 1, fci->siteName, fci->modelPermutations.currentPermutationIndex);
    else
        sprintf(tempFileName, "%s/%s.warnings.perm%02d.txt", fci->outputDirectory, fci->siteName, fci->modelPermutations.currentPermutationIndex);

    if(fci->warningsFile.fp != NULL)
        fclose(fci->warningsFile.fp);
    fci->warningsFile.fileName = strdup(tempFileName);
    if((fci->warningsFile.fp = fopen(fci->warningsFile.fileName, "w")) == NULL) {
        sprintf(ErrStr, "Couldn't open warnings file %s: %s", fci->warningsFile.fileName, strerror(errno));
        FatalError("setSiteInfo()", ErrStr);
    }

    fci->numSites++;
}

void parseNwpHeaderLine(forecastInputType *fci)
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
    // /home/jim/forecast/forecastOpt/data/etc/bondville.surface.v3.nwp.HA1.csv

    strcpy(tempLine, fci->forecastHeaderLine1);
    fci->numHeaderFields = split(tempLine + 1, fields, MAX_FIELDS, ","); /* split path */

    if(fci->numHeaderFields != 4) {
        FatalError("parseNwpHeaderLine()", "Too few columns in header line 1 to work with in input file\n");
    }

    setSiteInfo(fci, fields[0]);
    HA = atoi(fields[1]);
    fci->lat = atof(fields[2]);
    fci->lon = atof(fields[3]);

    if(HA < 1 || HA > 300 || fci->lat < -90 || fci->lat > 90 || fci->lon < -180 || fci->lon > 180) {
        FatalError("parseNwpHeaderLine()", "Something wrong with first line of input file.  Expecting site,HA,lat,lon\n");
    }

    strcpy(tempLine, fci->forecastHeaderLine2);
    fci->numHeaderFields = split(tempLine + 1, fields, MAX_FIELDS, ","); /* split path */

    if(fci->numHeaderFields < 10) {
        FatalError("parseNwpHeaderLine()", "Too few columns in header line 2 to work with in input file\n");
    }

    // now register the NWP models
    for(i = fci->startModelsColumnNumber; i < (fci->numHeaderFields - 1); i++) { // assuming clearsky comes last so don't register it
        registerModelInfo(fci, fields[i], fields[i], IsNotReference, IsForecast, HA);
    }

    fci->clearskyGHICol = i; // assumed to be the last
}

// this assumes fci->siteName has already been set by the input file parser

void setSite(forecastInputType *fci)
{
    int siteNum;

    for(siteNum = 0; siteNum < fci->numSites; siteNum++) {
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

    return (dateTimeSanityCheck(dt));
}

#define checkTooLow(X,Y) { if(thisSample->sunIsUp && thisSample->X < 1) { fprintf(fci->warningsFile.fp, "Warning: line %d: %s looks too low: %.1f\n", fci->forecastLineNumber, Y, thisSample->X); }}

void copyHoursAfterData(forecastInputType *fci)
{
    int hoursAheadIndex, hoursAfterSunriseIndex;

    for(hoursAheadIndex = 0; hoursAheadIndex < fci->maxHoursAfterSunrise; hoursAheadIndex++)
        for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
            fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] = fci->hoursAheadGroup[hoursAheadIndex];
            fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex].hoursAfterSunrise = hoursAfterSunriseIndex + 1;
        }
}

int readDataFromLine(forecastInputType *fci, timeSeriesType *thisSample, char *fields[], int numFields)
{

#ifdef DUMP_ALL_DATA
    static char firstTime = True;
    static char *filteredDataFileName = "filteredInputData.csv";
#endif

    //timeSeriesType *thisSample = &(fci->timeSeries[fci->numTotalSamples - 1]);

    //thisSample->zenith = atof(fields[fci->zenithCol]);

    thisSample->siteName = strdup(fci->siteName); // for multiple site runs

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
    //checkTooLow(groundGHI, "groundGHI");
    if(thisSample->sunIsUp && thisSample->groundGHI < 1)
        fprintf(fci->warningsFile.fp, "Warning: line %d: %s looks too low: %.1f\n", fci->forecastLineNumber, "groundGHI", thisSample->groundGHI);

    thisSample->clearskyGHI = atof(fields[fci->clearskyGHICol]);
    if(thisSample->clearskyGHI < MIN_IRR || thisSample->clearskyGHI > MAX_IRR) {
        sprintf(ErrStr, "Got bad clearsky GHI at line %d: %.2f", fci->forecastLineNumber, thisSample->clearskyGHI);
        FatalError("readDataFromLine()", ErrStr);
    }
    checkTooLow(clearskyGHI, "clearskyGHI");

    thisSample->satGHI = atof(fields[fci->satGHICol]);
    checkTooLow(satGHI, "satGHI");

    int modelIndex;
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        if(modelIndex >= numFields) {
            sprintf(ErrStr, "Input error: modelIndex %d exceeds number of fields in line, %d", modelIndex, numFields);
            FatalError("readDataFromLine()", ErrStr);
        }

        if(strlen(fields[modelIndex]) < 1) { // no data present
            thisSample->nwpData[modelIndex] = -999;
        }
        else {
            thisSample->nwpData[modelIndex] = atof(fields[modelIndex + fci->startModelsColumnNumber]);
        }
        //fprintf(stderr, "modelDesc=%s,modelInfoIndex=%d,inputCol=%d,hourInd=%d,modelIndex=%d,scanVal=%s,atofVal=%.1f\n", fci->modelInfo[modelIndex].modelName, modelIndex, fci->modelInfo[modelIndex].inputColumnNumber, 
        //        hoursAheadIndex, modelIndex, fields[fci->modelInfo[modelIndex].inputColumnNumber],thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex]);
        //if(thisSample->modelGHIvalues[modelIndex] < MIN_GHI_VAL)
        //    thisSample->isValid = False;
        //if(thisSample->sunIsUp && thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] < 1) { 
        //    fprintf(stderr, "Warning: line %d: %s looks too low: %.1f\n", fci->forecastLineNumber, fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].modelName, thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex]); 
        //}
    }

#define DUMP_ALL_DATA
#ifdef DUMP_ALL_DATA
    static int firstTime = True;

    if(thisSample->hoursAheadIndex == 15 || thisSample->hoursAheadIndex == 2) {
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
            fprintf(stderr, ",clearsky_GHI\n");
            firstTime = False;
        }
#endif

#ifdef DUMP_ALL_DATA
        fprintf(stderr, "%s,%.0f,%.0f", dtToStringCsv2(&thisSample->dateTime), thisSample->groundGHI, thisSample->satGHI);
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            //hoursAheadIndex = fci->modelInfo[modelIndex].hoursAheadIndex;
            fprintf(stderr, ",%.1f", thisSample->nwpData[modelIndex]);
        }
        fprintf(stderr, ",%.0f\n", thisSample->clearskyGHI);
    }
#endif

    return True;
}

// review the input data for holes

void studyData(forecastInputType *fci)
{
    /*
        timeSeriesType *thisSample;
        modelStatsType *thisModelErr;
        int i, modelIndex, hoursAheadIndex, daylightData = 0;

        for(i = 0; i < fci->numTotalSamples; i++) {
            thisSample = &(fci->timeSeries[i]);
            if(thisSample->sunIsUp) {
                for(modelIndex = fci->startModelsColumnNumber; modelIndex < fci->numColumnInfoEntries; modelIndex++) {
                    daylightData++;
                    hoursAheadIndex = fci->modelInfo[modelIndex].hoursAheadIndex;
                    modelIndex = fci->modelInfo[modelIndex].modelIndex;
                    thisModelErr = &fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex];

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

        fprintf(fci->warningsFile.fp, "======== Scanning Forecast Data For Active Models =========\n");
        for(modelIndex = fci->startModelsColumnNumber; modelIndex < fci->numColumnInfoEntries; modelIndex++) {
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
                    fprintf(fci->warningsFile.fp, "%35s : off\n", fci->modelInfo[modelIndex].modelName); //, hoursAheadIndex = %d\n", fci->modelInfo[modelIndex].modelName, modelIndex, hoursAheadIndex);
                    fprintf(fci->warningsFile.fp, "WARNING: disabling model '%s' : %.0f%% data missing : model index = %d hour index = %d\n", fci->modelInfo[modelIndex].modelName, fci->modelInfo[modelIndex].percentMissing, modelIndex, hoursAheadIndex); //, hoursAheadIndex = %d\n", fci->modelInfo[modelIndex].modelName, modelIndex, hoursAheadIndex);
                    fprintf(stderr, "WARNING: disabling model '%s' : %.0f%% data missing : model index = %d hour index = %d\n", fci->modelInfo[modelIndex].modelName, fci->modelInfo[modelIndex].percentMissing, modelIndex, hoursAheadIndex); //, hoursAheadIndex = %d\n", fci->modelInfo[modelIndex].modelName, modelIndex, hoursAheadIndex);
                }
                else {
                    fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isActive = True;
                    fprintf(fci->warningsFile.fp, "%35s : on\n", fci->modelInfo[modelIndex].modelName); //, hoursAheadIndex = %d\n", fci->modelInfo[modelIndex].modelName, modelIndex, hoursAheadIndex);
                }
            }

            // use isContributingModel (in optimization stage) to signify isActive and not isReference forecast model (such as persistence)
                        fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isContributingModel = 
                                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isActive 
                                && !fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isReference;

            //fprintf(stderr, "%s : column index = %d : model index = %d : hour index = %d : missing = %.0f%%\n", fci->modelInfo[modelIndex].modelName, modelIndex, fci->modelInfo[modelIndex].modelIndex, fci->modelInfo[modelIndex].hoursAheadIndex, fci->modelInfo[modelIndex].percentMissing);

            // disable models if percentMissing < threshold
        }
        fprintf(fci->warningsFile.fp, "===========================================================\n");
     */
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
    int i;
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
    fci->weightSumLowCutoff = 95;
    fci->weightSumHighCutoff = 105;
    fci->startHourLowIndex = -1;
    fci->startHourHighIndex = -1;
    fci->numDaylightRecords = 0;
    fci->numDivisions = 7;
    fci->runWeightedErrorAnalysis = False;
    fci->forecastLineNumber = 0;
    fci->runHoursAfterSunrise = False;
    fci->maxHoursAfterSunrise = MAX_HOURS_AFTER_SUNRISE;
    fci->filterWithSatModel = True;
    fci->lat = -999;
    fci->lon = -999;
    fci->siteGroup = NULL;
    fci->siteName = NULL;
    fci->skipPhase2 = False;
    fci->numModels = 0;
    fci->numContribModels = 0;
    fci->numSites = 0;
    fci->maxModelIndex = 0;
    fci->numTotalSamples = 0;
    
    // allocate space for all model data
    allocatedSamples = 8670 * MAX_MODELS;
    size = allocatedSamples * sizeof (timeSeriesType);
    for(i = 0; i < MAX_HOURS_AHEAD; i++) {
        if((fci->nwpTimeSeries[i].timeSeries = (timeSeriesType *) malloc(size)) == NULL) {
            FatalError("initForecastInfo()", "memory alloc error");
        }
        fci->nwpTimeSeries[i].numGood = 0;
        fci->nwpTimeSeries[i].numMissing = 0;
        fci->nwpTimeSeries[i].numTimeSeriesSamples = 0;
    }
    fci->numColumnInfoEntries = 0;
    fci->numInputRecords = 0;
    fci->numDaylightRecords = 0;
    fci->doModelPermutations = True;
    fci->modelMixDirectory = "modelMixes";

    for(i = 0; i < MAX_HOURS_AHEAD; i++) {
        fci->hoursAheadGroup[i].hoursAhead = -1;
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

    //    #year,month,day,hour,min,surface,zen,v3,CMM-east,ECMWF,GFS,HRRR,NDFD,clearsky_GHI
    fci->groundGHICol = 5;
    //fci->zenithCol = 6;
    fci->satGHICol = 6;
    fci->startModelsColumnNumber = 7;
    fci->numModelRuns = 0;
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

// thisSample = getNextTimeSeriesSample(fci, hoursAheadIndex);
// Previously, the timeseries could just be incremented as we moved through the NWP input file as 
// that files contained all NWPs and HAs on a single line.  Now we have NWPs and HAs in a single file
// so we will have to search for the timeSeriesIndex that matches the current date/time

timeSeriesType *getNextTimeSeriesSample(forecastInputType *fci, int hoursAheadIndex)
{
    int timeSeriesIndex = fci->[hoursAheadIndex].numTimeSeriesSamples;

    fci->nwpTimeSeries[hoursAheadIndex].numTimeSeriesSamples++;
    fci->numTotalSamples++;
    if(fci->numTotalSamples == allocatedSamples) {
        allocatedSamples *= 2;
        fci->nwpTimeSeries[hoursAheadIndex].timeSeries = (timeSeriesType *) realloc(fci->nwpTimeSeries[hoursAheadIndex].timeSeries, allocatedSamples * sizeof (timeSeriesType));
    }

    return (&fci->nwpTimeSeries[hoursAheadIndex].timeSeries[timeSeriesIndex]);
}

timeSeriesType *findTimeSeriesSample(forecastInputType *fci)
{
    int i;
    
    for(i=0; i<)
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
    registerModelInfo(fci, "sr_zen", "zenith angle", IsNotReference, IsNotForecast, 0);
    fci->groundGHICol = fci->numColumnInfoEntries;
    registerModelInfo(fci, "sr_global", "ground GHI", IsReference, IsNotForecast, 0);
    fci->groundDNICol = fci->numColumnInfoEntries;
    registerModelInfo(fci, "sr_direct", "ground DNI", IsNotReference, IsNotForecast, 0);
    fci->groundDiffuseCol = fci->numColumnInfoEntries;
    registerModelInfo(fci, "sr_diffuse", "ground diffuse", IsNotReference, IsNotForecast, 0);
    fci->satGHICol = fci->numColumnInfoEntries;
    registerModelInfo(fci, "sat_ghi", "sat model GHI", IsReference, IsNotForecast, 0);
    fci->clearskyGHICol = fci->numColumnInfoEntries;
    registerModelInfo(fci, "clear_ghi", "clearsky GHI", IsNotReference, IsNotForecast, 0);

    fci->startModelsColumnNumber = fci->numColumnInfoEntries;

    if((fci->configFile.fp = fopen(fci->configFile.fileName, "r")) == NULL) {
        sprintf(ErrStr, "Couldn't open model description file %s : %s", fci->configFile.fileName, strerror(errno));
        FatalError("scanHeaderLine()", ErrStr);
    }

    fci->numModels = 0;


    //
    // Now we open up the models description (config) file
    //

    char line[LINE_LENGTH], saveLine[LINE_LENGTH];
    char *fields[MAX_FIELDS], *modelName, *modelDesc;
    int i, numFields, isReference, maxHoursAhead, hoursAheadColMap[64], thisHour;
    double weight;
    int modelStartColumn = 3;

    fci->configFile.lineNumber = 0;

    // Need to find a way to generate this file too
    while(fgets(line, LINE_LENGTH, fci->configFile.fp)) {
        fci->configFile.lineNumber++;
        //fprintf(stderr, "line %d:%s\n", fci->configFile.lineNumber, line);
        if(fci->configFile.lineNumber == 1) // this is just an informational line
            continue;
        if(fci->configFile.lineNumber == 2) { // this is the header line with (possibly) hours ahead info in it
            strcpy(saveLine, line);
            // #Model description file for forecastOpt
            // #modelName           modelDesc                       isReferenceModel	HA_1 weights	HA_2 weights	HA_12 weights
            // ncep_NAM_DSWRF_	 "NAM Low Res Instant GHI"	 0                      0.1             0.2             0.1
            numFields = split(line, fields, MAX_FIELDS, ",\t"); /* split line */
            if(numFields > modelStartColumn) {
                fci->runOptimizer = False; // we want to use a weighting scheme that's been provided
                for(i = 3; i < numFields; i++) {
                    hoursAheadColMap[i] = parseNumberFromString(fields[i]); // HA_1 weights => 1
                    if(hoursAheadColMap[i] < 1 || hoursAheadColMap[i] > MAX_HOURS_AHEAD) {
                        sprintf(ErrStr, "Something's fishy about header line %d in %s: can't parse hours ahead number out of column %d in header line.\nline = %s\n",
                                fci->configFile.lineNumber, fci->configFile.fileName, i + 1, saveLine);
                        FatalError("scanHeaderLine()", ErrStr);
                    }
                }
            }
            else
                fci->runOptimizer = True; // only model names, desc, isRef were provided -- fire up the optimizer
        }
        else {
            maxHoursAhead = isReference = 0;

            stripComment(line);
            // split by space then by =
            numFields = split(line, fields, MAX_FIELDS, ","); /* split line */
            if(numFields == 0)
                continue;
            if((modelName = stripQuotes(fields[0])) == NULL)
                FatalError("scanHeaderLine()", ErrStr);
            if((modelDesc = stripQuotes(fields[1])) == NULL)
                FatalError("scanHeaderLine()", ErrStr);
            isReference = atoi(fields[2]);
            if(numFields > 3)
                maxHoursAhead = atoi(fields[3]);

            registerModelInfo(fci, modelName, modelDesc, isReference, IsForecast, maxHoursAhead); // register this model as one we want to use
            fci->numModels++;
            if(!isReference)
                fci->numContribModels++;

            if(numFields > 1000) {
                for(i = modelStartColumn; i < numFields; i++) {
                    thisHour = hoursAheadColMap[i];
                    if(thisHour < 1 || thisHour > 500) {
                        sprintf(ErrStr, "Internal error keeping track of current hour ahead while parsing model config file %s, line %d: hours ahead for column %d = %d",
                                fci->configFile.fileName, fci->configFile.lineNumber, i + 1, thisHour);
                        FatalError("scanHeaderLine()", ErrStr);
                    }
                    weight = atof(fields[i]);
                    if(weight < -0.5 || weight > 1.5) {
                        sprintf(ErrStr, "Bad weight while parsing model config file %s, line %d: hours ahead for column %d, weight = %f",
                                fci->configFile.fileName, fci->configFile.lineNumber, i + 1, weight);
                        FatalError("scanHeaderLine()", ErrStr);
                    }
                    // now we need to set the weight and isContributingModel flags for the current modelIndex and hoursAheadIndex
                    // but this is best done when we're finished reading in the forecast table
                    //fci->hoursAheadGroup[i].hourlyModelStats[modelIndex].optimizedWeightPhase2 = weight;

                }
            }
        }
    }

    if(fci->startHourLowIndex == -1) {
        fci->startHourLowIndex = 0;
        fci->startHourHighIndex = fci->maxModelIndex;
    }

    fclose(fci->configFile.fp);
    fci->gotConfigFile = True;
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

int getModelIndex(forecastInputType *fci, char *modelName)
{
    int i;
    for(i = 0; i < fci->numModels; i++) {
        if(strcmp(modelName, fci->modelInfo[i].modelName) == 0)
            return i;
    }

    return fci->numModels++; // new model 
}

void registerModelInfo(forecastInputType *fci, char *modelName, char *modelDescription, int isReference, int isForecast, int hoursAhead)
{
    // We want to search the forecastHeaderLine for modelName and see how far out it goes
    // note that just because a measurement goes out to N hours ahead doesn't guarantee that there's 
    // good data out there.  This may be a sticky issue.
    static int numFields = -1;
    static char *fields[MAX_FIELDS];
    int i, modelIndex;
    static int hoursAheadIndex = 0, lastHoursAhead = -1;
    char tempColDesc[1024];

    if(lastHoursAhead > 0 && lastHoursAhead != hoursAhead)
        hoursAheadIndex++;

    if(isForecast) {
        modelIndex = getModelIndex(fci, modelName);
        fci->modelInfo[fci->numModelRuns].modelIndex = modelIndex;
        fci->modelInfo[fci->numModelRuns].hoursAheadIndex = hoursAheadIndex;
        fci->modelInfo[fci->numModelRuns].modelName = strdup(modelName); //"ncep_RAP_DSWRF_1";  
        /*
                        if(strstr(fci->modelInfo[fci->numColumnInfoEntries].modelName, "HRRR") != NULL) {
                            fci->doHrrrGhiRecalc = True;
                    
                        }
         */
        sprintf(tempColDesc, "%s +%d hours", modelDescription, hoursAhead); //fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
        fci->modelInfo[fci->numModelRuns].modelDescription = strdup(tempColDesc); //"NCEP RAP GHI";  
        fci->modelInfo[fci->numModelRuns].maxhoursAhead = MAX(hoursAhead, fci->modelInfo[fci->numModelRuns].maxhoursAhead); // this will keep increasing

        fprintf(stderr, "registering %s HA=%d, HAindex=%d, modelIndex=%d\n", modelName, hoursAhead, hoursAheadIndex, fci->modelInfo[fci->numModelRuns].modelIndex);

        /*
                        if(fci->hoursAheadGroup[hoursAheadIndex].hoursAhead != -1) {
                            if(fci->hoursAheadGroup[hoursAheadIndex].hoursAhead != hoursAhead) {
                                sprintf(ErrStr, "found inconsistency in number of hours ahead: for hours ahead index %d: it changed from %d hours to %d hours\nmodelName=%s\ncolumnDesc=%s,maxHoursAhead=%d",
                                        hoursAheadIndex, fci->hoursAheadGroup[hoursAheadIndex].hoursAhead, hoursAhead, modelName, modelDescription, maxHoursAhead);
                                FatalError("registerModelInfo()", ErrStr);
                            }
                        }
                        else
         */
        fci->hoursAheadGroup[hoursAheadIndex].hoursAhead = hoursAhead;

        // make hourErrorGroup links
        // turn off isActive if this HA is > than that specified in config file
        fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isActive = True;
        fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isReference = isReference;
        fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].tooMuchDataMissing = False;
        fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].modelInfoIndex = fci->numColumnInfoEntries;
        fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].modelName = fci->modelInfo[fci->numColumnInfoEntries].modelName;
        fci->maxModelIndex = MAX(fci->maxModelIndex, hoursAheadIndex); // the max hoursAheadIndex is the number of hour groups
        /*
        fprintf(stderr, "[%d] registering %s, data col index = %d, input col = %d, hour index = %d,", fci->numModels, fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].modelName,
                fci->numColumnInfoEntries-1, i, hoursAheadIndex);
        fprintf(stderr, "isActive=%d, isReference=%d, maxHoursAhead=%d\n", 
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].isActive,
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].isReference,
                fci->modelInfo[fci->numColumnInfoEntries].maxhoursAhead);
         */
        fci->numModelRuns++;
    }
        // not a forecast but we still need to set a few things
    else {
        for(i = 0; i < numFields; i++) {
            if(strncasecmp(fields[i], modelName, strlen(modelName)) == 0) { /* got a hit */
                fci->modelInfo[fci->numModels].modelName = strdup(modelName); // ;                      
                fci->modelInfo[fci->numModels].modelDescription = strdup(modelDescription); //"NCEP RAP GHI";  
                fci->modelInfo[fci->numModels].maxhoursAhead = 0;
                fci->modelInfo[fci->numModels].hoursAheadIndex = -1;
                fci->modelInfo[fci->numModels].modelIndex = -1;
                fci->modelInfo[fci->numModels].inputColumnNumber = i; // this tells us the column numner of the input forecast table
                /*
                                fci->hoursAheadGroup[0].hourlyModelStats[fci->numModels].isReference = isReference;
                                fci->hoursAheadGroup[0].hourlyModelStats[fci->numModels].modelInfoIndex = fci->numColumnInfoEntries;
                                fci->hoursAheadGroup[0].hourlyModelStats[fci->numModels].modelName = fci->modelInfo[fci->numColumnInfoEntries].modelName;
                 */
                fci->numColumnInfoEntries++;
                //fprintf(stderr, "registering non-model %s, data index = %d, hour index = %d, input col = %d\n", fields[i], fci->numColumnInfoEntries-1, hoursAheadIndex, i);
            }
        }
    }

    //fci->numModels++;
    lastHoursAhead = hoursAhead;
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

    fprintf(stderr, "Note: model %s not turned on for site %s\n", modelName, fci->siteName);
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
    int hoursAheadIndex, hoursAfterSunriseIndex;
    modelRunType *modelRun;

    // print header
    fprintf(stderr, "\n\n#hours ahead");
    for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++)
        fprintf(stderr, "\tHAS=%02d", hoursAfterSunriseIndex + 1);
    fprintf(stderr, "\n");

    for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex < fci->maxHoursAfterSunrise; hoursAheadIndex++) {
        fprintf(stderr, "%d", fci->hoursAfterSunriseGroup[hoursAheadIndex][0].hoursAhead);
        for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
            modelRun = &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex];
            fprintf(stderr, "\t%.1f", modelRun->optimizedRMSEphase2 * 100);
        }
        fprintf(stderr, "\n");
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
    int modelIndex, hoursAheadIndex, hoursAfterSunriseIndex, emptyOutput;
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
                getGenericModelName(fci, modelIndex), genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, fci->startDateStr, fci->endDateStr);
        for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++)
            buffPtr += sprintf(buffPtr, "HAS=%d%c", hoursAfterSunriseIndex + 1, hoursAfterSunriseIndex == fci->maxHoursAfterSunrise - 1 ? '\n' : ',');

        for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex < fci->maxHoursAfterSunrise; hoursAheadIndex++) {
            if(fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isOn) {
                buffPtr += sprintf(buffPtr, "%d,", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
                for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
                    modelRun = &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex];
                    err = &modelRun->hourlyModelStats[modelIndex];
                    if(isContributingModel(err)) {
                        buffPtr += sprintf(buffPtr, "%d%c", fci->skipPhase2 ? err->optimizedWeightPhase1 : err->optimizedWeightPhase2, hoursAfterSunriseIndex == fci->maxHoursAfterSunrise - 1 ? '\n' : ',');
                        emptyOutput = False;
                    }
                }
            }
        }
        if(!emptyOutput) {
            sprintf(fileName, "%s/%s.ModelMixBy.HA_HAS.%s.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), getGenericModelName(fci, modelIndex), fci->modelPermutations.currentPermutationIndex);
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
    // 1) isOn is set
    // 2) it's not a reference model (such as persistence)
    // 3) it hasn't been shut off because of too much missing data
    return (model->isOn && !model->isReference);
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
    int modelIndex, hoursAheadIndex, hoursAfterSunriseIndex;
    //int hoursAhead, hoursAfterSunrise;
    modelRunType *modelRun;
    modelStatsType *err;

    if(fci->verbose)
        fprintf(stderr, "\nGenerating model mix percentage files by hours after sunrise...\n");

    for(hoursAfterSunriseIndex = 0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
        sprintf(fileName, "%s/%s.percentByHAS.HA_Model.HAS=%d.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), hoursAfterSunriseIndex + 1, fci->modelPermutations.currentPermutationIndex);
        if((fp = fopen(fileName, "w")) == NULL) {
            sprintf(ErrStr, "Couldn't open file %s : %s", fileName, strerror(errno));
            FatalError("dumpModelMix_EachModel_HAxHAS()", ErrStr);
        }

        // print header lines
        fprintf(fp, "#For hours after sunrise=%d: model percent by hours ahead and model type\n#siteName=%s,lat=%.2f,lon=%.3f,date span=%s-%s\n#hoursAhead,",
                hoursAfterSunriseIndex + 1, genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, fci->startDateStr, fci->endDateStr);
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++)
            fprintf(fp, "%s%c", getGenericModelName(fci, modelIndex), modelIndex == fci->numModels - 1 ? '\n' : ',');

        for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex < fci->maxHoursAfterSunrise; hoursAheadIndex++) {
            fprintf(fp, "%d,", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
            for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
                modelRun = &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex];
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

        fprintf(fp, "#siteName=%s,lat=%.2f,lon=%.2f,hours ahead=%d,N=%d,mean measured GHI=%.1f,date span=%s-%s\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon,
                modelRun->hoursAhead, modelRun->numValidSamples, modelRun->meanMeasuredGHI, fci->startDateStr, fci->endDateStr);
        fprintf(fp, "#model,sum( model-ground ), sum( abs(model-ground) ), sum( (model-ground)^2 ), mae, mae percent, mbe, mbe percent, rmse, rmse percent\n");

        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            err = &modelRun->hourlyModelStats[modelIndex];
            fprintf(fp, "%s,%.0f,%.0f,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", fci->modelInfo[err->modelInfoIndex].modelDescription, err->sumModel_Ground, err->sumAbs_Model_Ground, err->sumModel_Ground_2,
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
        sprintf(fileName, "%s/%s.forecast.error.model=%s.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), getGenericModelName(fci, modelIndex), fci->modelPermutations.currentPermutationIndex);
        fp = fopen(fileName, "w");


        fprintf(fp, "#siteName=%s,lat=%.2f,lon=%.2f,error analysis=%s, date span=%s-%s\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon,
                getGenericModelName(fci, modelIndex), fci->startDateStr, fci->endDateStr);
        fprintf(fp, "#model,N,hours ahead, mean measured GHI, sum( model-ground ), sum( abs(model-ground) ), sum( (model-ground)^2 ), mae, mae percent, mbe, mbe percent, rmse, rmse percent\n");

        for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
            modelRun = &fci->hoursAheadGroup[hoursAheadIndex];

            err = &modelRun->hourlyModelStats[modelIndex];
            fprintf(fp, "%s,%d,%d,%.1f,%.0f,%.0f,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", fci->modelInfo[err->modelInfoIndex].modelDescription,
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
        modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][0] : &fci->hoursAheadGroup[hoursAheadIndex];

        fprintf(fp, "%d,%d", modelRun->hoursAhead, modelRun->numValidSamples);
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            err = &modelRun->hourlyModelStats[modelIndex];
            fprintf(fp, ",%.1f%%", err->rmsePct * 100);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}

void printRmseTableHour(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    FILE *fp;
    int modelIndex;
    modelRunType *modelRun;
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    if(!(fp = openErrorTypeFileHourly(fci, "RMSE", hoursAheadIndex, hoursAfterSunriseIndex)))
        return;
    //fp = stderr;

    //for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
    fprintf(fp, "hours ahead = %d", modelRun->hoursAhead);
    if(hoursAfterSunriseIndex >= 0)
        fprintf(fp, ", hours after sunrise = %d", modelRun->hoursAfterSunrise);
    fprintf(fp, "\nN = %d\n", modelRun->numValidSamples);
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        if(modelRun->hourlyModelStats[modelIndex].isActive)
            fprintf(fp, "%-35s = %.1f%%\n", getGenericModelName(fci, modelIndex), modelRun->hourlyModelStats[modelIndex].rmsePct * 100);
    }
    fprintf(fp, "\n");
    //}    
    //fclose(fp);   
}

FILE *openErrorTypeFileHourly(forecastInputType *fci, char *analysisType, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    char fileName[1024];
    static FILE *fp;
    //    int modelIndex;
    char satGHIerr[1024];
    //    modelRunType *modelRun = &fci->hoursAheadGroup[hoursAheadIndex];
    modelRunType *modelRun;
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    if(fci->outputDirectory == NULL || fci->siteName == NULL) {
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

    fprintf(fp, "siteName=%s\nlat=%.2f\nlon=%.2f\nanalysisType=%s\n%s\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, analysisType, satGHIerr);
    /*
        fprintf(fp, "hours ahead,N,");
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++)  {
            if(modelRun->hourlyModelStats[modelIndex].isActive)
                fprintf(fp, "%s,", getGenericModelName(fci, modelIndex));
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

    if(fci->outputDirectory == NULL || fci->siteName == NULL) {
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

    fprintf(fp, "#siteName=%s,lat=%.2f,lon=%.2f,%s,%s,date span=%s-%s\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, fileNameStr, satGHIerr, fci->startDateStr, fci->endDateStr);
    fprintf(fp, "#hours ahead,N,");
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++)
        fprintf(fp, "%s,", getGenericModelName(fci, modelIndex));
    fprintf(fp, "\n");

    return fp;
}

int getMaxHoursAhead(forecastInputType *fci, int modelIndex)
{
    //int modelIndex = fci->hoursAheadGroup[0].hourlyModelStats[modelIndex].modelInfoIndex;
    return (fci->modelInfo[modelIndex].maxhoursAhead);
}

char *getGenericModelName(forecastInputType *fci, int modelIndex)
{
    static char modelDesc[1024];

    if(modelIndex < 0)
        return ("satellite");
    if(modelIndex >= fci->numModels) {
        fprintf(stderr, "getGenericModelName(): Internal Error: got modelIndex = %d when numModels = %d", modelIndex, fci->numModels);
        exit(1);
    }
    strcpy(modelDesc, fci->hoursAheadGroup[0].hourlyModelStats[modelIndex].modelName); // should be something like ncep_RAP_DSWRF_1
    modelDesc[strlen(modelDesc) - 2] = '\0'; // lop off the _1 => ncep_RAP_DSWRF

    return (modelDesc);
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

// this automatically parses out the hours ahead from the column names line
// assumes uniformity in the number of hours ahead -- that is, that all models have the same number of 
// hours ahead columns

void getNumberOfHoursAhead(forecastInputType *fci, char *origLine)
{
    char *p, *q, *currPtr, copyLine[1024 * 1024], measName[1024];
    int lineLength = strlen(origLine);
    int hour;

    //copyLine = (char *) malloc(1024 * 16);
    strcpy(copyLine, origLine); // this function is string destructive
    currPtr = copyLine;

    q = strstr(copyLine, "_1"); // this assumes the first instance of _1 will be a model entry...
    if(q == NULL) {
        FatalError("getNumberOfHoursAhead()", "problem getting number of hours ahead");
    }

    p = q;
    while(*p != *fci->delimiter && p > currPtr) p--;
    p++;
    strncpy(measName, p, q - p + 1); // include the _
    measName[q - p + 1] = '\0';


    while((q = strstr(currPtr, measName)) != NULL) {
        while(*q != *fci->delimiter && q < (copyLine + lineLength)) q++;
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

        currPtr = q + 1;
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

void printHourlySummary(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int modelIndex, hoursAhead = fci->hoursAheadGroup[hoursAheadIndex].hoursAhead;
    int hoursAfterSunrise = hoursAfterSunriseIndex + 1;
    modelRunType *modelRun;
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    fprintf(stderr, "\nHR%d=== Summary for hours ahead %d ", hoursAhead, hoursAhead);
    if(hoursAfterSunriseIndex >= 0)
        fprintf(stderr, ", hours after sunrise %d ", hoursAfterSunrise);
    fprintf(stderr, "(number of good samples) ===\n");
    fprintf(stderr, "HR%d\t%-40s : %d\n", hoursAhead, "N for group", modelRun->numValidSamples);
    fprintf(stderr, "HR%d\t%-40s : %d\n", hoursAhead, "ground GHI", modelRun->ground_N);
    for(modelIndex = -1; modelIndex < fci->numModels; modelIndex++) {
        if(modelIndex < 0 || modelRun->hourlyModelStats[modelIndex].isOn)
            fprintf(stderr, "HR%d\t%-40s : %d\n", hoursAhead, getGenericModelName(fci, modelIndex), getModelN(modelIndex));
    }
}

void printHoursAheadSummaryCsv(forecastInputType *fci)
{
    int hoursAheadIndex, modelIndex;
    modelRunType *modelRun;
    char modelName[1024], tempFileName[2048];

    //sprintf(filename, "%s/%s.wtRange=%.2f-%.2f_ha=%d-%d.csv", fci->outputDirectory, fci->siteName, fci->weightSumLowCutoff, fci->weightSumHighCutoff, fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead);
    sprintf(tempFileName, "%s/forecastSummary.%s.%s-%s.div=%d.hours=%d-%d.perm%02d.csv", fci->outputDirectory, genProxySiteName(fci), dtToStringDateOnly(&fci->startDate), dtToStringDateOnly(&fci->endDate), fci->numDivisions, fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead, fci->modelPermutations.currentPermutationIndex);
    fci->summaryFile.fileName = strdup(tempFileName);

    if((fci->summaryFile.fp = fopen(fci->summaryFile.fileName, "w")) == NULL) {
        sprintf(ErrStr, "Couldn't open file %s : %s", fci->summaryFile.fileName, strerror(errno));
        FatalError("printHoursAheadSummaryCsv()", ErrStr);
    }
    // print the header
    fprintf(fci->summaryFile.fp, "#site=%s lat=%.3f lon=%.3f divisions=%d date span=%s-%s\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, fci->numDivisions, fci->startDateStr, fci->endDateStr);
    fprintf(fci->summaryFile.fp, "#hoursAhead,group N,sat RMSE,p1RMSE,p2RMSE,p1SumWts calls,p1RMSE calls,p2SumWts calls,p2RMSE calls");
    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        strcpy(modelName, getGenericModelName(fci, modelIndex));
        fprintf(fci->summaryFile.fp, ",%s model, %s status,%s N,%s RMSE,%s %s,%s %s", modelName, modelName, modelName, modelName,
                modelName, WEIGHT_1_STR, modelName, WEIGHT_2_STR);
    }
    fprintf(fci->summaryFile.fp, "\n");

    // generate model results
    for(hoursAheadIndex = fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        modelRun = &fci->hoursAheadGroup[hoursAheadIndex];
        //        modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

        fprintf(fci->summaryFile.fp, "%d,%d,%.2f,%.2f,%.2f,%ld,%ld,%ld,%ld", modelRun->hoursAhead, modelRun->numValidSamples, modelRun->satModelStats.rmsePct * 100,
                modelRun->optimizedRMSEphase1 * 100, modelRun->optimizedRMSEphase2 * 100, modelRun->phase1SumWeightsCalls, modelRun->phase1RMSEcalls, modelRun->phase2SumWeightsCalls, modelRun->phase2RMSEcalls);
        for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
            //if(modelRun->hourlyModelStats[modelIndex].isOn) {
            fprintf(fci->summaryFile.fp, ",%s", modelRun->hourlyModelStats[modelIndex].isReference ? "reference" : "forecast"
                    );
            fprintf(fci->summaryFile.fp, ",%s", modelRun->hourlyModelStats[modelIndex].isOn ? "on" : "off");
            fprintf(fci->summaryFile.fp, ",%d", modelRun->hourlyModelStats[modelIndex].isOn ? modelRun->hourlyModelStats[modelIndex].N : -999);
            fprintf(fci->summaryFile.fp, ",%.2f", modelRun->hourlyModelStats[modelIndex].isOn ? modelRun->hourlyModelStats[modelIndex].rmsePct * 100 : -999);
            fprintf(fci->summaryFile.fp, ",%d", modelRun->hourlyModelStats[modelIndex].isOn ? modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase1 : -999);
            fprintf(fci->summaryFile.fp, ",%d", modelRun->hourlyModelStats[modelIndex].isOn ? modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase2 : -999);
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

    return (True);
}

void dumpWeightedTimeSeries(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex)
{
    int sampleInd, modelIndex;
    double weight, weightTotal;
    timeSeriesType *thisSample;
    modelStatsType *thisModelErr;
    modelRunType *modelRun;
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

    /*
      if(!fci->weightedTimeSeriesFp)
        return;
     */

    for(sampleInd = 0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->nwpTimeSeries[hoursAheadIndex].timeSeries[sampleInd];
        if(thisSample->isValid) {
            weightTotal = 0;
            thisSample->optimizedGHI = 0;
            for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
                thisModelErr = &modelRun->hourlyModelStats[modelIndex];

                if(modelRun->hourlyModelStats[modelIndex].isOn) {
                    weight = thisModelErr->optimizedWeightPhase2;
                    thisSample->optimizedGHI += (thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] * weight);
                    weightTotal += weight;
                }
            }
        }

        //fprintf(stderr, "DWTS: %s,%d,%f,%f,%f\n",dtToStringCsv2(&thisSample->dateTime),fci->hoursAheadGroup[hoursAheadIndex].hoursAhead,thisSample->optimizedGHI/weightTotal,thisSample->optimizedGHI,weightTotal);
    }
}

char *genProxySiteName(forecastInputType *fci)
{
    static char returnSite[1024];
    if(fci->multipleSites)
        sprintf(returnSite, "MultiSite-sites=%d", fci->numSites);
    else
        strcpy(returnSite, fci->siteName);

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
        if(fci->hoursAfterSunriseGroup[0][hoursAfterSunriseIndex].hoursAfterSunrise == hoursAfterSunrise)
            return hoursAfterSunriseIndex;
    }
    return -1;
}

int getModelIndexOld(forecastInputType *fci, char *modelName)
{
    int modelIndex;
    char *currName;

    for(modelIndex = 0; modelIndex < fci->numModels; modelIndex++) {
        currName = getGenericModelName(fci, modelIndex);
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

    perm->numPermutations = pow(2, fci->numContribModels);

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
    for(p = 0; p < fci->numContribModels; p++) {
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

void setModelSwitches(forecastInputType *fci, int hoursAheadIndex, int hoursAfterSunriseIndex, int permutationIndex)
{
    int i, maskInd;
    permutationType *perm = &fci->modelPermutations;
    modelRunType *modelRun;
    modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];
    int hoursAhead = fci->hoursAheadGroup[hoursAheadIndex].hoursAhead;

    perm->currentPermutationIndex = permutationIndex;

    /*
    4,9,13,-999,-999,100,-999,1243,50.6
    4,9,14,-999,-999,100,-999,737,47.5
    4,9,15,-999,-999,100,-999,302,51.4
    4,12,1,-999,-999,-999,100,939,48.0  <-- when switching HA from 9 to 12 model switches (shouldn't)
    4,12,2,-999,-999,-999,100,2035,38.3
    4,12,3,-999,-999,-999,100,2055,32.2
    4,12,4,-999,-999,-999,100,2099,28.2
     */

    fprintf(stderr, "\nMASK=== Setting model permutation switches for %d, hoursAhead=%d, hoursAfterSunRise=%d ===\n", permutationIndex, hoursAhead, hoursAfterSunriseIndex + 1);
    for(i = 0, maskInd = 0; i < fci->numModels; i++, maskInd++) {
        // if model is active (i.e., there's data) and not a reference model
        modelRun->hourlyModelStats[i].isOn = False;
        if(modelRun->hourlyModelStats[i].isActive) {
            if(modelRun->hourlyModelStats[i].isReference) {
                fprintf(stderr, "model %s is reference\n", getGenericModelName(fci, i));
                modelRun->hourlyModelStats[i].isOn = True;
                maskInd--; // not a maskable model so keep the maskInd the same
            }
            else {
                modelRun->hourlyModelStats[i].isOn = (permutationIndex & perm->masks[maskInd]);
                fprintf(stderr, "MASKmodel %s is %s (maskInd=%d, permIndex=%d & mask=%d)\n", getGenericModelName(fci, i),
                        modelRun->hourlyModelStats[i].isOn ? "on" : "off", maskInd, permutationIndex, perm->masks[maskInd]);
            }
        }
    }
    fprintf(stderr, "MASK=== done ===\n");
}
