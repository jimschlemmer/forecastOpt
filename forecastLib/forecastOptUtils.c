#include "forecastOpt.h"
#include "forecastOptUtils.h"

char ErrStr[4096];

int  allocatedSamples, HashLineNumber=1;

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
        sprintf(ErrStr, "Couldn't open input file %s : %s", fci->forecastTableFile.fileName, strerror(errno));
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
        
        if(strstr(line, "site")) {
            if(fci->numSites > 0) {
                if(strcasecmp(fci->forecastHeaderLine, line) != 0) {
                    sprintf(ErrStr, "Header line changed from: %s\n\nto\n\n%s\n", fci->forecastHeaderLine, line);
                    FatalError("readForecastFile()", ErrStr);
                }
            }
            else
                fci->forecastHeaderLine = strdup(line);  // save for later
            firstLine = True;
            continue;
        }        

        else if(firstLine) {
            firstLine = False;
            setSiteInfo(fci, line);
            if(fci->numSites == 1)
                scanHeaderLine(fci);
        }
        
        numFields = split(line, fields, MAX_FIELDS, fci->delimiter);  /* split line */  
        if(numFields < 100) {
            sprintf(ErrStr, "Scanning line %d of file %s, got %d columns using delimiter \"%s\".  Either delimiter flag or -s arg is wrong", fci->forecastLineNumber, fci->forecastTableFile.fileName, numFields, fci->delimiter);
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
        else if(strcasecmp(fldPtr, fci->siteGroup) != 0) {
            if(1 || fci->multipleSites) {
                fprintf(fci->warningsFile.fp, "Note: switching site group from [%s] to [%s]\n", fci->siteGroup, fldPtr);
                free(fci->siteGroup);
                fci->siteGroup = strdup(fldPtr);
            }
            else {
                fprintf(stderr, "Warning: siteGroup changed from [%s] to [%s]\n", fci->siteGroup, fldPtr);
            }
        }
        
        // siteName
/*
        fldPtr = fields[1];
        if(strcasecmp(fldPtr, fci->siteName) != 0) {
            if(fci->multipleSites) {
                    fprintf(fci->warningsFile.fp, "Note: switching site from [%s] to [%s]\n", fci->siteName, fldPtr);
                    free(fci->siteName);
                    fci->siteName = strdup(fldPtr);
            }
            else {
                fprintf(stderr, "Warning: siteName changed from [%s] to [%s]\n", fci->siteName, fldPtr);
            }
        }
*/
                
        // lat & lon
        lat = atof(fields[2]);
        lon = atof(fields[3]);

        if(!fci->multipleSites && fci->lat != -999 && fabs(lat - fci->lat) > 0.01) {
            fprintf(fci->warningsFile.fp, "Warning: latitude changed from %.3f to %.3f\n", fci->lat, lat);
        }
        if(!fci->multipleSites && fci->lon != -999 && fabs(lon - fci->lon) > 0.01) {
            fprintf(fci->warningsFile.fp, "Warning: longitude changed from %.3f to %.3f\n", fci->lon, lon);
        }        
        fci->lat = lat;
        fci->lon = lon;
                
        (void) readDataFromLine(fci, fields);
//#define USE_ZENITH_INSTEAD_OF_HAS
        // compute hours after sunrise this date/time
        thisSample->sunrise = calculateSunrise(&currDate, fci->lat, fci->lon);  // Get the sunrise time for this day
        if(thisSample->sunIsUp) {
#ifdef USE_ZENITH_INSTEAD_OF_HAS
            thisSample->hoursAfterSunrise = (int) ((90 - thisSample->zenith)/10) + 1;  // 9 buckets of 10 degrees each
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
    
    studyData(fci);
    
    fci->gotForecast = True;
    
    return True;
}

void setSiteInfo(forecastInputType *fci, char *line)
{
    char *tempLine = strdup(line);
    int numFields;
    char *fields[MAX_FIELDS];
    char tempFileName[2048];
        
    numFields = split(tempLine, fields, MAX_FIELDS, fci->delimiter);  /* split line */
    fci->siteName = strdup(fields[1]);
    fprintf(stderr, "=== Setting site to %s ===\n", fci->siteName);
    fci->thisSite = &fci->allSiteInfo[fci->numSites];
    
    // Now that we know the site name we can open the warnings file
    if(fci->multipleSites)
        sprintf(tempFileName, "%s/MultiSite-site%d-%s.warnings.txt", fci->outputDirectory, fci->numSites+1, fci->siteName);
    else
        sprintf(tempFileName, "%s/%s.warnings.txt", fci->outputDirectory, fci->siteName);
    
    if(fci->warningsFile.fp != NULL)
        fclose(fci->warningsFile.fp);
    fci->warningsFile.fileName = strdup(tempFileName);
    if((fci->warningsFile.fp = fopen(fci->warningsFile.fileName, "w")) == NULL) {
        sprintf(ErrStr, "Couldn't open warnings file %s: %s", fci->warningsFile.fileName, strerror(errno));
        FatalError("setSiteInfo()", ErrStr);
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

#define DateTimeError { sprintf(ErrStr, "Bad date/time string in input, line %d:\n%s\n", fci->forecastLineNumber, origStr); FatalError("parseDateTime()", ErrStr); }
    
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
        int inRange = (dt->obs_time >= fci->startDate.obs_time && dt->obs_time <= fci->endDate.obs_time);
/*
        if(!inRange)
            fprintf(stderr, "%s not in range\n", dtToStringDateTime(dt));
*/
        return(inRange);
    }
    
    return True;
}

#define checkTooLow(X,Y) { if(thisSample->sunIsUp && thisSample->X < 1) { fprintf(fci->warningsFile.fp, "Warning: line %d: %s looks too low: %.1f\n", fci->forecastLineNumber, Y, thisSample->X); }}

void copyHoursAfterData(forecastInputType *fci)
{
    int hoursAheadIndex, hoursAfterSunriseIndex;
    
    for(hoursAheadIndex=0;hoursAheadIndex<fci->maxHoursAfterSunrise;hoursAheadIndex++)
        for(hoursAfterSunriseIndex=0;hoursAfterSunriseIndex<fci->maxHoursAfterSunrise;hoursAfterSunriseIndex++) {
                fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] = fci->hoursAheadGroup[hoursAheadIndex];
                fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex].hoursAfterSunrise = hoursAfterSunriseIndex + 1;
        }
}

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
        sprintf(ErrStr, "Got bad zenith angle at line %d: %.2f", fci->forecastLineNumber, thisSample->zenith);
        FatalError("readDataFromLine()", ErrStr);
    }
    
    thisSample->siteName = strdup(fci->siteName);  // for multiple site runs
    
    thisSample->sunIsUp = (thisSample->zenith < 90);
    if(thisSample->sunIsUp)
        fci->numDaylightRecords++;
       
    thisSample->groundGHI = atof(fields[fci->columnInfo[fci->groundGHICol].inputColumnNumber]);
/*
    if(thisSample->sunIsUp && thisSample->groundGHI < 1)
        fprintf(stderr, "Warning: line %d: groundGHI looks too low: %.1f\n", fci->forecastLineNumber, thisSample->groundGHI);
*/
    
    if(thisSample->groundGHI < MIN_IRR || thisSample->groundGHI > MAX_IRR) {
        sprintf(ErrStr, "Got bad surface GHI at line %d: %.2f", fci->forecastLineNumber, thisSample->groundGHI);
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
        sprintf(ErrStr, "Got bad surface DNI at line %d: %.2f", fci->forecastLineNumber, thisSample->groundDNI);
        FatalError("readDataFromLine()", ErrStr);
    }
    
    thisSample->groundDiffuse = atof(fields[fci->columnInfo[fci->groundDiffuseCol].inputColumnNumber]);
    if(thisSample->groundDiffuse < MIN_IRR || thisSample->groundDiffuse > MAX_IRR) {
        sprintf(ErrStr, "Got bad surface diffuse at line %d: %.2f", fci->forecastLineNumber, thisSample->groundDiffuse);
        FatalError("readDataFromLine()", ErrStr);
    }  
    
    thisSample->clearskyGHI = atof(fields[fci->columnInfo[fci->clearskyGHICol].inputColumnNumber]);
    if(thisSample->clearskyGHI < MIN_IRR || thisSample->clearskyGHI > MAX_IRR) {
        sprintf(ErrStr, "Got bad clearsky GHI at line %d: %.2f", fci->forecastLineNumber, thisSample->clearskyGHI);
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
     
    
    int columnIndex, modelIndex, hoursAheadIndex;
    for(columnIndex=fci->startModelsColumnNumber; columnIndex<fci->numColumnInfoEntries; columnIndex++) {
        hoursAheadIndex = fci->columnInfo[columnIndex].hoursAheadIndex;
        modelIndex = fci->columnInfo[columnIndex].modelIndex;
/*
        if(strcmp(fci->columnInfo[columnIndex].columnName, "ncep_HRRR_DSWRF_1") == 0)
            printf("hi\n");
*/
        if(strlen(fields[fci->columnInfo[columnIndex].inputColumnNumber]) < 1) {  // no data present
            //fprintf(stderr, "%s line %d: input col = %d, data = %s\n", fci->columnInfo[columnIndex].columnName, fci->forecastLineNumber, fci->columnInfo[columnIndex].inputColumnNumber, fields[fci->columnInfo[columnIndex].inputColumnNumber]);
            thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] = -999;
        }
        else {
            thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] = atof(fields[fci->columnInfo[columnIndex].inputColumnNumber]);
        }
        //fprintf(stderr, "modelDesc=%s,columnInfoIndex=%d,inputCol=%d,hourInd=%d,modelIndex=%d,scanVal=%s,atofVal=%.1f\n", fci->columnInfo[columnIndex].columnName, columnIndex, fci->columnInfo[columnIndex].inputColumnNumber, 
        //        hoursAheadIndex, modelIndex, fields[fci->columnInfo[columnIndex].inputColumnNumber],thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex]);
        //if(thisSample->modelGHIvalues[modelIndex] < MIN_GHI_VAL)
        //    thisSample->isValid = False;
        //if(thisSample->sunIsUp && thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] < 1) { 
        //    fprintf(stderr, "Warning: line %d: %s looks too low: %.1f\n", fci->forecastLineNumber, fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].columnName, thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex]); 
        //}
    }

#ifdef DUMP_ALL_DATA
    if(firstTime) {        
        if(filteredDataFileName == NULL || (FilteredDataFp = fopen(filteredDataFileName, "w")) == NULL) {
            sprintf(ErrStr, "Couldn't open output file %s : %s", filteredDataFileName, strerror(errno));
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
            hoursAheadIndex = fci->columnInfo[columnIndex].hoursAheadIndex;
            modelIndex = fci->columnInfo[columnIndex].modelIndex;
            fprintf(FilteredDataFp, ",%.1f", thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex]);
    }
    fprintf(FilteredDataFp, "\n");
#endif
    
    return True;
}

// review the input data for holes
void studyData(forecastInputType *fci)
{
    timeSeriesType *thisSample;
    modelStatsType *thisModelErr;
    int i, columnIndex, modelIndex, hoursAheadIndex, daylightData=0;
    
    for(i=0; i<fci->numTotalSamples; i++) {
        thisSample = &(fci->timeSeries[i]);
        if(thisSample->sunIsUp) {
            for(columnIndex=fci->startModelsColumnNumber; columnIndex<fci->numColumnInfoEntries; columnIndex++) {            
                    daylightData++;
                    hoursAheadIndex = fci->columnInfo[columnIndex].hoursAheadIndex;
                    modelIndex = fci->columnInfo[columnIndex].modelIndex;
                    thisModelErr = &fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex];

                    if(thisSample->forecastData[hoursAheadIndex].modelGHI[modelIndex] == -999) {
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
        hoursAheadIndex = fci->columnInfo[columnIndex].hoursAheadIndex;
        modelIndex = fci->columnInfo[columnIndex].modelIndex;
/*
        if(fci->columnInfo[columnIndex].numGood < 1)
            fci->columnInfo[columnIndex].percentMissing = 100;
        else
*/
            fci->columnInfo[columnIndex].percentMissing = fci->columnInfo[columnIndex].numMissing/(fci->columnInfo[columnIndex].numMissing + fci->columnInfo[columnIndex].numGood) * 100.0;

            if(fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isActive) {  // hasn't been disabled yet
                if(fci->columnInfo[columnIndex].percentMissing < 100 && fci->columnInfo[columnIndex].percentMissing > 0) {
                    fprintf(stderr, "Warning: %s is neither completely on nor off in the input forecast data (%.0f%% missing)\n", fci->columnInfo[columnIndex].columnName, fci->columnInfo[columnIndex].percentMissing);
                }
                // deactivate this model on account of too much missing data
                if(fci->columnInfo[columnIndex].percentMissing > 90) {
                    fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isActive = False;
                    fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].tooMuchDataMissing = True;
                    fprintf(fci->warningsFile.fp, "%35s : off\n", fci->columnInfo[columnIndex].columnName); //, hoursAheadIndex = %d\n", fci->columnInfo[columnIndex].columnName, modelIndex, hoursAheadIndex);
                    fprintf(fci->warningsFile.fp, "WARNING: disabling model '%s' : %.0f%% data missing : model index = %d hour index = %d\n", fci->columnInfo[columnIndex].columnName, fci->columnInfo[columnIndex].percentMissing, modelIndex, hoursAheadIndex); //, hoursAheadIndex = %d\n", fci->columnInfo[columnIndex].columnName, modelIndex, hoursAheadIndex);
                    fprintf(stderr,               "WARNING: disabling model '%s' : %.0f%% data missing : model index = %d hour index = %d\n", fci->columnInfo[columnIndex].columnName, fci->columnInfo[columnIndex].percentMissing, modelIndex, hoursAheadIndex); //, hoursAheadIndex = %d\n", fci->columnInfo[columnIndex].columnName, modelIndex, hoursAheadIndex);
                }
                else {
                    fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isActive = True;
                    fprintf(fci->warningsFile.fp, "%35s : on\n", fci->columnInfo[columnIndex].columnName); //, hoursAheadIndex = %d\n", fci->columnInfo[columnIndex].columnName, modelIndex, hoursAheadIndex);
                }
            }
            
            // use isContributingModel (in optimization stage) to signify isActive and not isReference forecast model (such as persistence)
            fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isContributingModel = 
                    fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isActive 
                    && !fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isReference;

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
    
    fci->delimiter = "\t";
    fci->startDate.year = -1;
    fci->endDate.year = -1;
    fci->outputDirectory = "./";
    fci->verbose = False;
    fci->multipleSites = False;
    fci->gotConfig = False;
    fci->gotForecast = False;
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
    fci->genModelMixPermutations = True;
    fci->modelMixDirectory = "modelMixes";

    for(i=0; i<MAX_HOURS_AHEAD; i++) {
        fci->hoursAheadGroup[i].hoursAhead = -1;       
    }
    
    fci->descriptionFile.fileName = NULL;
    fci->warningsFile.fileName = NULL;
    fci->weightsFile.fileName = NULL;
    fci->modelMixFileOutput.fileName = NULL;
    fci->modelMixFileInput.fileName = NULL;
    fci->warningsFile.fp = NULL;
    fci->descriptionFile.fp = NULL;
    fci->weightsFile.fp = NULL;
    fci->modelMixFileOutput.fp = NULL;
    fci->modelMixFileInput.fp = NULL;
    fci->correctionStatsFile.fp = NULL;
    
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
    registerColumnInfo(fci, "sr_zen", "zenith angle", IsNotReference, IsNotForecast, 0);
    fci->groundGHICol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "sr_global", "ground GHI", IsReference, IsNotForecast, 0);   
    fci->groundDNICol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "sr_direct", "ground DNI", IsNotReference, IsNotForecast, 0);   
    fci->groundDiffuseCol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "sr_diffuse", "ground diffuse", IsNotReference, IsNotForecast, 0);   
    fci->satGHICol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "sat_ghi", "sat model GHI", IsReference, IsNotForecast, 0);   
    fci->clearskyGHICol = fci->numColumnInfoEntries;
    registerColumnInfo(fci, "clear_ghi", "clearsky GHI", IsNotReference, IsNotForecast, 0);  
    
    fci->startModelsColumnNumber = fci->numColumnInfoEntries;

    if((fci->descriptionFile.fp = fopen(fci->descriptionFile.fileName, "r")) == NULL) {
        sprintf(ErrStr, "Couldn't open model description file %s : %s", fci->descriptionFile.fileName, strerror(errno));
        FatalError("scanHeaderLine()", ErrStr);
    }

    fci->numModels = 0;
    
    
    //
    // Now we open up the models description (config) file
    //
    
    char line[LINE_LENGTH], saveLine[LINE_LENGTH];
    char *fields[MAX_FIELDS], *modelName, *modelDesc;
    int i, numFields, isReference, maxHoursAhead, hoursAheadColMap[64],thisHour;
    double weight;
    int modelStartColumn = 3; 
            
    fci->descriptionFile.lineNumber = 0;
    
    // Need to find a way to generate this file too
    while(fgets(line, LINE_LENGTH, fci->descriptionFile.fp)) {
        fci->descriptionFile.lineNumber++;
        //fprintf(stderr, "line %d:%s\n", fci->descriptionFile.lineNumber, line);
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
            maxHoursAhead = isReference = 0;
            
            stripComment(line);
            // split by space then by =
            numFields = split(line, fields, MAX_FIELDS, ",");  /* split line */
            if(numFields == 0)
                continue;
            if((modelName = stripQuotes(fields[0])) == NULL )
                FatalError("scanHeaderLine()", ErrStr);
            if((modelDesc = stripQuotes(fields[1])) == NULL) 
                FatalError("scanHeaderLine()", ErrStr);
            isReference = atoi(fields[2]);
            if(numFields > 3)
                maxHoursAhead = atoi(fields[3]);
            
            registerColumnInfo(fci, modelName, modelDesc, isReference, IsForecast, maxHoursAhead);    // register this model as one we want to use

            if(numFields > 1000) {
                    for(i=modelStartColumn; i<numFields; i++) {
                        thisHour = hoursAheadColMap[i];
                        if(thisHour < 1 || thisHour > 500) {
                            sprintf(ErrStr, "Internal error keeping track of current hour ahead while parsing model config file %s, line %d: hours ahead for column %d = %d",
                                    fci->descriptionFile.fileName, fci->descriptionFile.lineNumber, i+1, thisHour);
                            FatalError("scanHeaderLine()", ErrStr);
                        }
                        weight = atof(fields[i]);
                        if(weight < -0.5 || weight > 1.5) {
                            sprintf(ErrStr, "Bad weight while parsing model config file %s, line %d: hours ahead for column %d, weight = %f", 
                                    fci->descriptionFile.fileName, fci->descriptionFile.lineNumber, i+1, weight);
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

    fclose(fci->descriptionFile.fp);
    fci->gotConfig = True;
}

char *stripQuotes(char *str)
{
    char *q=NULL, *p = str;
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
    char *q=NULL, *p = str;
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


void registerColumnInfo(forecastInputType *fci, char *columnName, char *columnDescription, int isReference, int isForecast, int maxHoursAhead) 
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
        numFields = split(tempHeader, fields, MAX_FIELDS, fci->delimiter);  /* split line just once */   
    }

    if(isForecast) {
        for(i=0; i<numFields; i++) {
            if(strncasecmp(fields[i], columnName, strlen(columnName)) == 0) { /* got a hit */
                p = fields[i] + strlen(fields[i]) - 1;
                while(*p != '_' && p > fields[i]) p--;
                p++;
                hoursAhead = atoi(p);
                hoursAheadIndex++;
                fci->columnInfo[fci->numColumnInfoEntries].hoursAheadIndex = hoursAheadIndex;   
                fci->columnInfo[fci->numColumnInfoEntries].modelIndex = fci->numModels;
                sprintf(tempColDesc, "%s +%d hours", columnDescription, hoursAhead); //fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
                fci->columnInfo[fci->numColumnInfoEntries].columnName = strdup(fields[i]); //"ncep_RAP_DSWRF_1";                      
                fci->columnInfo[fci->numColumnInfoEntries].columnDescription = strdup(tempColDesc); //"NCEP RAP GHI";  
                fci->columnInfo[fci->numColumnInfoEntries].maxhoursAhead = maxHoursAhead <= 0 ? hoursAhead : MIN(hoursAhead, maxHoursAhead);  // this will keep increasing
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
                // turn off isActive if this HA is > than that specified in config file
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].isActive = (maxHoursAhead <= 0 || hoursAhead <= maxHoursAhead);
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].isReference = isReference;
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].tooMuchDataMissing = False;
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].columnInfoIndex = fci->numColumnInfoEntries;
                fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].columnName = fci->columnInfo[fci->numColumnInfoEntries].columnName;
                fci->maxModelIndex = MAX(fci->maxModelIndex, hoursAheadIndex);  // the max hoursAheadIndex is the number of hour groups
                /*
                fprintf(stderr, "[%d] registering %s, data col index = %d, input col = %d, hour index = %d,", fci->numModels, fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].columnName,
                        fci->numColumnInfoEntries-1, i, hoursAheadIndex);
                fprintf(stderr, "isActive=%d, isReference=%d, maxHoursAhead=%d\n", 
                        fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].isActive,
                        fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[fci->numModels].isReference,
                        fci->columnInfo[fci->numColumnInfoEntries].maxhoursAhead);
                */
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
    for(hoursAfterSunriseIndex=0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) 
        fprintf(stderr, "\tHAS=%02d", hoursAfterSunriseIndex+1);
    fprintf(stderr, "\n");
    
    for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex < fci->maxHoursAfterSunrise; hoursAheadIndex++) {
        fprintf(stderr, "%d", fci->hoursAfterSunriseGroup[hoursAheadIndex][0].hoursAhead);
        for(hoursAfterSunriseIndex=0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
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
    int modelIndex, hoursAheadIndex, hoursAfterSunriseIndex;
    //int hoursAhead, hoursAfterSunrise;
    modelRunType *modelRun;
    modelStatsType *err;
         
    if(fci->verbose) 
        fprintf(stderr, "\nGenerating model mix percentage files by model...\n");
    
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        sprintf(fileName, "%s/%s.ModelMixBy.HA_HAS.%s.csv", fci->outputDirectory, genProxySiteName(fci), getGenericModelName(fci, modelIndex));
        if((fp = fopen(fileName, "w")) == NULL) {
            sprintf(ErrStr, "Couldn't open file %s : %s", fileName, strerror(errno));
            FatalError("dumpModelMix_EachModel_HAxHAS()", ErrStr);
        }

        // print header
        fprintf(fp, "#For model %s: model percent by hours ahead (HA) and hours after sunrise (HAS)\n#siteName=%s,lat=%.2f,lon=%.3f,date span=%s\n#HA,",
                getGenericModelName(fci, modelIndex), genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, fci->timeSpanStr);
        for(hoursAfterSunriseIndex=0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) 
            fprintf(fp, "HAS=%d%c", hoursAfterSunriseIndex+1, hoursAfterSunriseIndex == fci->maxHoursAfterSunrise-1 ? '\n' : ',');
        
        for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex < fci->maxHoursAfterSunrise; hoursAheadIndex++) {            
            if(fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelIndex].isActive) {
                fprintf(fp, "%d,", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
                for(hoursAfterSunriseIndex=0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
                    modelRun = &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex];
                    err = &modelRun->hourlyModelStats[modelIndex];
                    if(err->isContributingModel)
                        fprintf(fp, "%d%c", fci->skipPhase2 ? err->optimizedWeightPhase1 : err->optimizedWeightPhase2, hoursAfterSunriseIndex == fci->maxHoursAfterSunrise-1 ? '\n' : ',');
                }
            }
        }
        fclose(fp);
    }
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
    
    for(hoursAfterSunriseIndex=0; hoursAfterSunriseIndex < fci->maxHoursAfterSunrise; hoursAfterSunriseIndex++) {
        sprintf(fileName, "%s/%s.percentByHAS.HA_Model.HAS=%d.csv", fci->outputDirectory, genProxySiteName(fci), hoursAfterSunriseIndex+1);
        if((fp = fopen(fileName, "w")) == NULL) {
            sprintf(ErrStr, "Couldn't open file %s : %s", fileName, strerror(errno));
            FatalError("dumpModelMix_EachModel_HAxHAS()", ErrStr);
        }

        // print header lines
        fprintf(fp, "#For hours after sunrise=%d: model percent by hours ahead and model type\n#siteName=%s,lat=%.2f,lon=%.3f,date span=%s\n#hoursAhead,",
                hoursAfterSunriseIndex+1, genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, fci->timeSpanStr);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) 
            fprintf(fp, "%s%c", getGenericModelName(fci, modelIndex), modelIndex == fci->numModels-1 ? '\n' : ',');
        
        for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex < fci->maxHoursAfterSunrise; hoursAheadIndex++) {
            fprintf(fp, "%d,", fci->hoursAheadGroup[hoursAheadIndex].hoursAhead);
            for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                modelRun = &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex];
                err = &modelRun->hourlyModelStats[modelIndex];
                if(err->isContributingModel)
                    fprintf(fp, "%d%c", fci->skipPhase2 ? err->optimizedWeightPhase1 : err->optimizedWeightPhase2, modelIndex == fci->numModels-1 ? '\n' : ',');
                else
                    fprintf(fp, "NA%c", modelIndex == fci->numModels-1 ? '\n' : ',');
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
         
    for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        modelRun = &fci->hoursAheadGroup[hoursAheadIndex];

        sprintf(fileName, "%s/%s.forecast.error.hoursAhead=%02d.csv", fci->outputDirectory, genProxySiteName(fci), modelRun->hoursAhead);
        //fprintf(stderr, "hour[%d] hour=%d file=%s\n", hoursAheadIndex, modelRun->hoursAhead, fileName);
        fp = fopen(fileName, "w");

        fprintf(fp, "#siteName=%s,lat=%.2f,lon=%.2f,hours ahead=%d,N=%d,mean measured GHI=%.1f\n",genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, modelRun->hoursAhead, modelRun->numValidSamples, modelRun->meanMeasuredGHI);
        fprintf(fp, "#model,sum( model-ground ), sum( abs(model-ground) ), sum( (model-ground)^2 ), mae, mae percent, mbe, mbe percent, rmse, rmse percent\n");

        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
            err = &modelRun->hourlyModelStats[modelIndex];
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
    int modelIndex, hoursAheadIndex;
    modelRunType *modelRun;
    modelStatsType *err;
    
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {   
        sprintf(fileName, "%s/%s.forecast.error.model=%s.csv", fci->outputDirectory, genProxySiteName(fci), getGenericModelName(fci, modelIndex));
        fp = fopen(fileName, "w");


        fprintf(fp, "#siteName=%s,lat=%.2f,lon=%.2f,error analysis=%s\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, getGenericModelName(fci, modelIndex));
        fprintf(fp, "#model,N,hours ahead, mean measured GHI, sum( model-ground ), sum( abs(model-ground) ), sum( (model-ground)^2 ), mae, mae percent, mbe, mbe percent, rmse, rmse percent\n");

            for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
                modelRun = &fci->hoursAheadGroup[hoursAheadIndex];

                err = &modelRun->hourlyModelStats[modelIndex];
                fprintf(fp, "%s,%d,%d,%.1f,%.0f,%.0f,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", fci->columnInfo[err->columnInfoIndex].columnDescription, 
                    modelRun->numValidSamples,fci->hoursAheadGroup[hoursAheadIndex].hoursAhead,modelRun->meanMeasuredGHI,err->sumModel_Ground, err->sumAbs_Model_Ground, err->sumModel_Ground_2,
                    err->mae, err->maePct*100, err->mbe, err->mbePct*100, err->rmse, err->rmsePct*100);
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
    
    for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        modelRun = &fci->hoursAheadGroup[hoursAheadIndex];
        fprintf(fp, "%d,%d", modelRun->hoursAhead, modelRun->numValidSamples);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {          
            err = &modelRun->hourlyModelStats[modelIndex];
            fprintf(fp, ",%.1f%%", err->maePct*100);
        }
        fprintf(fp, "\n");
    }    
    fclose(fp);   

    if(!(fp = openErrorTypeFile(fci, "individualModelError.MBE")))
        return;
    
    for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        modelRun = &fci->hoursAheadGroup[hoursAheadIndex];
        fprintf(fp, "%d,%d", modelRun->hoursAhead, modelRun->numValidSamples);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {          
            err = &modelRun->hourlyModelStats[modelIndex];
            fprintf(fp, ",%.1f%%", err->mbePct*100);
        }
        fprintf(fp, "\n");
    }    
    fclose(fp);   
#endif
    if(!(fp = openErrorTypeFile(fci, "individualModelError.RMSE")))
        return;
    
    for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        //modelRun = &fci->hoursAheadGroup[hoursAheadIndex];
        modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][0] : &fci->hoursAheadGroup[hoursAheadIndex];

        fprintf(fp, "%d,%d", modelRun->hoursAhead, modelRun->numValidSamples);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {          
            err = &modelRun->hourlyModelStats[modelIndex];
            fprintf(fp, ",%.1f%%", err->rmsePct*100);
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
        fprintf(fp, "\nN = %d\n",  modelRun->numValidSamples);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {          
            if(modelRun->hourlyModelStats[modelIndex].isActive)
                fprintf(fp, "%-35s = %.1f%%\n", getGenericModelName(fci, modelIndex), modelRun->hourlyModelStats[modelIndex].rmsePct*100);
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

    
    sprintf(fileName, "%s/%s.forecast.analysisType=%s.csv", fci->outputDirectory, genProxySiteName(fci), analysisType);
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

    
    sprintf(fileName, "%s/%s.%s.csv", fci->outputDirectory, genProxySiteName(fci), fileNameStr);
    fp = fopen(fileName, "w");
    
    fprintf(fp, "#siteName=%s,lat=%.2f,lon=%.2f,%s,%s\n", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, fileNameStr, satGHIerr);
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
    while(*p != *fci->delimiter && p > currPtr) p--;  
    p++;
    strncpy(measName, p, q-p+1);  // include the _
    measName[q-p+1] = '\0';
    
    
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
            
    // set this beign-end string for output into csv files
    sprintf(fci->timeSpanStr, "%s-%s", startStr, endStr);
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
    for(modelIndex=-1; modelIndex < fci->numModels; modelIndex++) {
        if(modelIndex < 0 || modelRun->hourlyModelStats[modelIndex].isActive)
            fprintf(stderr, "HR%d\t%-40s : %d\n", hoursAhead, getGenericModelName(fci, modelIndex), getModelN(modelIndex));
    }
}
  
void printHoursAheadSummaryCsv(forecastInputType *fci)
{
    int hoursAheadIndex, modelIndex;
    modelRunType *modelRun;
    char modelName[1024], tempFileName[2048];
    
    //sprintf(filename, "%s/%s.wtRange=%.2f-%.2f_ha=%d-%d.csv", fci->outputDirectory, fci->siteName, fci->weightSumLowCutoff, fci->weightSumHighCutoff, fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead);
    sprintf(tempFileName, "%s/forecastSummary.%s.%s-%s.div=%d.hours=%d-%d.csv", fci->outputDirectory, genProxySiteName(fci), dtToStringDateOnly(&fci->startDate), dtToStringDateOnly(&fci->endDate), fci->numDivisions, fci->hoursAheadGroup[fci->startHourLowIndex].hoursAhead, fci->hoursAheadGroup[fci->startHourHighIndex].hoursAhead);
    fci->summaryFile.fileName = strdup(tempFileName);
    
    if((fci->summaryFile.fp = fopen(fci->summaryFile.fileName, "w")) == NULL) {
        sprintf(ErrStr, "Couldn't open file %s : %s", fci->summaryFile.fileName, strerror(errno));
        FatalError("printHoursAheadSummaryCsv()", ErrStr);
    }
    // print the header
    fprintf(fci->summaryFile.fp, "#site=%s lat=%.3f lon=%.3f divisions=%d start date=%s ", genProxySiteName(fci), fci->multipleSites ? 999 : fci->lat, fci->multipleSites ? 999 : fci->lon, fci->numDivisions, dtToStringFilename(&fci->startDate));
    fprintf(fci->summaryFile.fp, "end date=%s\n", dtToStringFilename(&fci->endDate));
    fprintf(fci->summaryFile.fp, "#hoursAhead,group N,sat RMSE,p1RMSE,p2RMSE,p1SumWts calls,p1RMSE calls,p2SumWts calls,p2RMSE calls");
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        strcpy(modelName, getGenericModelName(fci, modelIndex));
        fprintf(fci->summaryFile.fp, ",%s model, %s status,%s N,%s RMSE,%s %s,%s %s", modelName, modelName, modelName, modelName,
 modelName, WEIGHT_1_STR, modelName, WEIGHT_2_STR);
    }
    fprintf(fci->summaryFile.fp, "\n");

    // generate model results
    for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex <= fci->startHourHighIndex; hoursAheadIndex++) {
        modelRun = &fci->hoursAheadGroup[hoursAheadIndex];
//        modelRun = fci->runHoursAfterSunrise ? &fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex] : &fci->hoursAheadGroup[hoursAheadIndex];

        fprintf(fci->summaryFile.fp, "%d,%d,%.2f,%.2f,%.2f,%ld,%ld,%ld,%ld", modelRun->hoursAhead, modelRun->numValidSamples, modelRun->satModelStats.rmsePct * 100,
                modelRun->optimizedRMSEphase1 * 100, modelRun->optimizedRMSEphase2 * 100, modelRun->phase1SumWeightsCalls, modelRun->phase1RMSEcalls, modelRun->phase2SumWeightsCalls, modelRun->phase2RMSEcalls);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {           
            //if(modelRun->hourlyModelStats[modelIndex].isActive) {
                fprintf(fci->summaryFile.fp, ",%s", modelRun->hourlyModelStats[modelIndex].isReference ? "reference" : "forecast"
);
                fprintf(fci->summaryFile.fp, ",%s", modelRun->hourlyModelStats[modelIndex].isActive ? "active" : "inactive");
                fprintf(fci->summaryFile.fp, ",%d", modelRun->hourlyModelStats[modelIndex].isActive ? modelRun->hourlyModelStats[modelIndex].N : -999);
                fprintf(fci->summaryFile.fp, ",%.2f", modelRun->hourlyModelStats[modelIndex].isActive ? modelRun->hourlyModelStats[modelIndex].rmsePct * 100 : -999);
                fprintf(fci->summaryFile.fp, ",%d", modelRun->hourlyModelStats[modelIndex].isActive ? modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase1 : -999);   
                fprintf(fci->summaryFile.fp, ",%d", modelRun->hourlyModelStats[modelIndex].isActive ? modelRun->hourlyModelStats[modelIndex].optimizedWeightPhase2 : -999);   
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

    //modelRunType *modelRun;
    
    while(fgets(line, LINE_LENGTH, fp)) {
        fci->forecastLineNumber++; 
        
        numFields = split(line, fields, MAX_FIELDS, ",");  /* split line */  
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
    
    return(True);
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

    for(sampleInd=0; sampleInd < fci->numTotalSamples; sampleInd++) {
        thisSample = &fci->timeSeries[sampleInd];
        if(thisSample->isValid) {
            weightTotal = 0;
            thisSample->optimizedGHI = 0;
            for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
                thisModelErr = &modelRun->hourlyModelStats[modelIndex];
                            
                if(modelRun->hourlyModelStats[modelIndex].isActive) {
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
    
    for(hoursAheadIndex=fci->startHourLowIndex; hoursAheadIndex<=fci->startHourHighIndex; hoursAheadIndex++) {
        if(fci->hoursAheadGroup[hoursAheadIndex].hoursAhead == hoursAhead)
             return hoursAheadIndex;
    }
    return -1;
}

int getHoursAfterSunriseIndex(forecastInputType *fci, int hoursAfterSunrise)
{
    int hoursAfterSunriseIndex;
    
    for(hoursAfterSunriseIndex=0; hoursAfterSunriseIndex<=fci->startHourHighIndex; hoursAfterSunriseIndex++) {
        if(fci->hoursAfterSunriseGroup[0][hoursAfterSunriseIndex].hoursAfterSunrise == hoursAfterSunrise)
             return hoursAfterSunriseIndex;
    }
    return -1;
}

int getModelIndex(forecastInputType *fci, char *modelName)
{
    int modelIndex;
    char *currName;
    
    for(modelIndex=0; modelIndex<=fci->numModels; modelIndex++) {
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

/*
The forecast optimizer will assume a fixed set of input models.  However, in the real
world, not all models will be available at all times.  If, for example, the CM model
is down and has a non-zero weight for some forecast horizons, optimizer GHI's will be off.  So
we need to have model mix sets for all the scenarios so that the weights of active channels
add up to 1.
*/
        
// We create a permutation matrix that represents on/off switches for all the input 
// models.  Something, like:
/*
1 1 1 1 1 1  <= all models active
1 1 1 1 1 0
1 1 1 1 0 1
1 1 1 1 0 0
1 1 1 0 1 1
1 1 1 0 1 0  <= 4 on, 2 off
[...]
*/
// should be 2^numModels permutations minus the empty case.  So for five forecast
// models we'd expect 31 permutations, including the 5 trivial cases.  

#define PERM_DEBUG

void setOnOffSwitches(int position, int number, forecastInputType *fci)
{
    int currentPow2 = pow(2, position);
    
    if(position < 0) {
#ifdef PERM_DEBUG
        int j;
        for(j=0;j<fci->numModels;j++)
            fprintf(stderr, "%d", (int) fci->perm.switches[fci->perm.numPermutations][j]);
        fprintf(stderr, "\n");
#endif
        fci->perm.numPermutations++;
        return;
    }
    
    if(number >= currentPow2) {
        fci->perm.switches[fci->perm.numPermutations][position] = 1;
        number -= currentPow2;
    }
    
    else
        fci->perm.switches[fci->perm.numPermutations][position] = 0;
    
    setOnOffSwitches(position-1, number, fci);
}

void genPermutationMatrix(forecastInputType *fci)
{
    int i;
    
    if(fci->numModels < 2) {
        FatalError("genPermutationMatrix()", "got numModels < 2!");
    }
    
    fci->perm.maxPermutations = pow(2, fci->numModels);
    fci->perm.numPermutations = 0;
//    fci->perm.switches = (char **) calloc(fci->perm.maxPermutations, fci->numModels * sizeof(char *));

    fci->perm.switches = (char **) malloc(fci->perm.maxPermutations * sizeof(char *));
    for(i=0; i<fci->perm.maxPermutations; i++) {
        fci->perm.switches[i] = (char *) malloc(fci->numModels * sizeof(char *));
    }
    
    for(i=0;i<fci->perm.maxPermutations;i++) {
        setOnOffSwitches(fci->numModels, i, fci);
    }
}

void setPermutation(forecastInputType *fci, int permNumber)
{
    int modelNum, hoursAheadIndex, hoursAfterSunriseIndex;
    
#ifdef PERM_DEBUG
    fprintf(stderr, "setPermutation [%d]: setting forecast models according to switches :", permNumber);
    for(modelNum=0; modelNum<=fci->numModels; modelNum++) {
        fprintf(stderr, "%d", (int) fci->perm.switches[permNumber][modelNum]);
    }
    fprintf(stderr, "\n");
#endif
    
    // set all model run instances to the appropriate 'isActive' state
    for(modelNum=0; modelNum<fci->numModels; modelNum++) {
        int onOff = fci->perm.switches[permNumber][modelNum] && 
                    !fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelNum].tooMuchDataMissing &&
                    !fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelNum].isReference;
    
        for(hoursAheadIndex=0;hoursAheadIndex<fci->maxHoursAfterSunrise;hoursAheadIndex++) {
            for(hoursAfterSunriseIndex=0;hoursAfterSunriseIndex<fci->maxHoursAfterSunrise;hoursAfterSunriseIndex++) {
                fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex].hourlyModelStats[modelNum].isActive = onOff;
                fci->hoursAfterSunriseGroup[hoursAheadIndex][hoursAfterSunriseIndex].hourlyModelStats[modelNum].isContributingModel = onOff;
                    
            }
            fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelNum].isActive = onOff;
            fci->hoursAheadGroup[hoursAheadIndex].hourlyModelStats[modelNum].isContributingModel = onOff;
#ifdef PERM_DEBUG
            fprintf(stderr, "%s : %s\n", getGenericModelName(fci, modelNum), onOff ? "on" : "off");
#endif
        }
    }

}