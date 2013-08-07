/* 
 Forecast Optimizer.
 */

#include "satModel.h"
#include "forecastOpt.h"

#define VERSION "1.0"

#define MIN_IRR -25
#define MAX_IRR 1500
#define MIN_GHI_VAL 5
    
void help(void);
void version(void);
void processForecast(char *fileName);
void initForecast(forecastInputType *fci);
void incrementTimeSeries(forecastInputType *fci);
int  readForecastFile(forecastInputType *fci, char *fileName);
int  readDataFromLine(forecastInputType *fci, char *fields[]);
void parseDateTime(dateTimeType *dt, char *dateTimeStr);
void initInputColumns(forecastInputType *fci);
void getColumnNumbers(forecastInputType *fci, char *colNamesLine);
int  parseArgs(int argC, char **argV);
void registerColumnInfo(forecastInputType *fci, char *columnName, char *columnDescription, char isModel);
void runErrorAnalysis(forecastInputType *fci);

char *Progname, *OutputFileName, Verbose=0;
FILE *OutputFp;
char *fileName;
char ErrStr[4096];
int  allocatedModels, allocatedSamples, LineNumber, HashLineNumber;
char *Delimiter;

int main(int argc,char **argv)
{
    
    signal(SIGSEGV, segvhandler); // catch memory reference errors

    Progname = basename(argv[0]);  // strip out path
  
    if (!parseArgs(argc, argv)) {
       exit(1);
    }
 
    processForecast(fileName);

    return(EXIT_SUCCESS);
}
  
int parseArgs(int argC, char **argV)
{
    int c;
    extern char *optarg;
    extern int optind;
    HashLineNumber = 2;
    Delimiter = ",";
    OutputFileName = NULL;
    
    //static char tabDel[32];
    //sprintf(tabDel, "%c", 9);  // ascii 9 = TAB
    
    while ((c=getopt(argC, argV, "cto:s:HhvV")) != EOF) {
        switch (c) {
            case 'c': { Delimiter = ",";
                        break; }

            case 'o': { OutputFileName = strdup(optarg);
                        break; }
            case 's': { HashLineNumber = atoi(optarg);
                        break; }
            case 't': { Delimiter = "\t";
                        break; }

            case 'v': Verbose = True;
                      break;
            case 'h': 
            case 'H': help();
                      return(False);
            case 'V': version();
                      return(False);
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
    printf( "usage: %s forecastFile\n", Progname);
    printf( "where: forecastFile = .csv forecast file\n");
}

void version(void)
{
  fprintf(stderr,"%s %s built [%s %s]\n", Progname, VERSION, __TIME__, __DATE__);
  return;
} 



void processForecast(char *fileName)
{
    forecastInputType forecastInput;
    
    initForecast(&forecastInput);
    initInputColumns(&forecastInput);
    readForecastFile(&forecastInput, fileName);
    runErrorAnalysis(&forecastInput);
     
    return;
}

#define LineLength 1024 * 32
#define MAX_FIELDS 2048

int readForecastFile(forecastInputType *fci, char *fileName)
{
    FILE *fp;
    char line[LineLength];
    int numFields;
    double lat, lon;
    char *fields[MAX_FIELDS], *fldPtr;
    
    timeSeriesType *thisSample;
    
    if((fp = fopen(fileName, "r")) == NULL) {
        FatalError("readForecastFile()", "Couldn't open input file.");
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
                getColumnNumbers(fci, line);
            }
            continue;
        }        
        //fgets(line, LineLength, fp);
        
        incrementTimeSeries(fci);
        thisSample = &(fci->timeSeries[fci->numTotalSamples - 1]);
        
        numFields = split(line, fields, MAX_FIELDS, Delimiter);  /* split line */  
        if(numFields < 100) {
            sprintf(ErrStr, "Scanning line %d of file %s, got %d columns using delimiter \"%s\".  Either delimiter flag or -s arg is wrong", LineNumber, fileName, numFields, Delimiter);
            FatalError("readForecastFile()", ErrStr);
        }
        
        // siteGroup
        fldPtr = fields[0];
        if(fci->siteGroup == NULL) {
            fci->siteGroup = strdup(fldPtr);
        }
        else {
            if(strcasecmp(fldPtr, fci->siteGroup) != 0) {
                fprintf(stderr, "Warning: siteGroup changed from %s to %s\n", fci->siteGroup, fldPtr);
            }
        }
        
        // siteName
        fldPtr = fields[1];
        if(fci->siteName == NULL) {
            fci->siteName = strdup(fldPtr);
        }
        else {
            if(strcasecmp(fldPtr, fci->siteName) != 0) {
                fprintf(stderr, "Warning: siteName changed from %s to %s\n", fci->siteName, fldPtr);
            }
        }
        
        // lat & lon
        lat = atof(fields[2]);
        lon = atof(fields[3]);

        if(fci->lat == -999 || fci->lon == -999) {
            fci->lat = lat;
            fci->lon = lon;
        }
        else {
            if(fabs(lat - fci->lat) > 0.01) {
                fprintf(stderr, "Warning: latitude changed from %.3f to %.3f\n", fci->lat, lat);
            }
            if(fabs(lon - fci->lon) > 0.01) {
                fprintf(stderr, "Warning: longitude changed from %.3f to %.3f\n", fci->lon, lon);
            }        
        }
        
        // date & time
        parseDateTime(&thisSample->dateTime, fields[4]);
               
        (void) readDataFromLine(fci, fields);
        
/*
        if(fci->numValidSamples == 3)
            return True;
*/

    }
    
    return True;
}

#define DT_FORMAT_1 1
#define DT_FORMAT_2 2

void parseDateTime(dateTimeType *dt, char *dateTimeStr)
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
    static char firstTime = True;
    timeSeriesType *thisSample = &(fci->timeSeries[fci->numTotalSamples - 1]);  
    
    thisSample->isValid = True;
    
    thisSample->zenith = atof(fields[fci->readColumns[fci->zenithCol].inputColumnNumber]);
    if(thisSample->zenith < 0 || thisSample->zenith > 180) {
        sprintf(ErrStr, "Got bad zenith angle at line %d: %.2f\n", LineNumber, thisSample->zenith);
        FatalError("readDataFromLine()", ErrStr);
    }
    
    thisSample->groundGHI = atof(fields[fci->readColumns[fci->groundGHICol].inputColumnNumber]);
    if(thisSample->groundGHI < MIN_IRR || thisSample->groundGHI > MAX_IRR) {
        sprintf(ErrStr, "Got bad surface GHI at line %d: %.2f\n", LineNumber, thisSample->groundGHI);
        FatalError("readDataFromLine()", ErrStr);
    }
    if(thisSample->groundGHI < MIN_GHI_VAL)
        thisSample->isValid = False;
    
    thisSample->groundDNI = atof(fields[fci->readColumns[fci->groundDNICol].inputColumnNumber]);
    if(thisSample->groundDNI < MIN_IRR || thisSample->groundDNI > MAX_IRR) {
        sprintf(ErrStr, "Got bad surface DNI at line %d: %.2f\n", LineNumber, thisSample->groundDNI);
        FatalError("readDataFromLine()", ErrStr);
    }
    
    thisSample->groundDiffuse = atof(fields[fci->readColumns[fci->groundDiffuseCol].inputColumnNumber]);
    if(thisSample->groundDiffuse < MIN_IRR || thisSample->groundDiffuse > MAX_IRR) {
        sprintf(ErrStr, "Got bad surface diffuse at line %d: %.2f\n", LineNumber, thisSample->groundDiffuse);
        FatalError("readDataFromLine()", ErrStr);
    }  
    
    thisSample->clearskyGHI = atof(fields[fci->readColumns[fci->clearskyGHICol].inputColumnNumber]);
    if(thisSample->clearskyGHI < MIN_IRR || thisSample->clearskyGHI > MAX_IRR) {
        sprintf(ErrStr, "Got bad clearsky GHI at line %d: %.2f\n", LineNumber, thisSample->clearskyGHI);
        FatalError("readDataFromLine()", ErrStr);
    }
    //if(thisSample->clearskyGHI < MIN_GHI_VAL)
    //    thisSample->isValid = False;
    
    thisSample->groundTemp = atof(fields[9]);
    if(thisSample->groundTemp < -40 || thisSample->groundTemp > 50) {
        sprintf(ErrStr, "Got bad surface temperature at line %d: %.2f\n", LineNumber, thisSample->groundTemp);
        FatalError("readDataFromLine()", ErrStr);
    }
    
    thisSample->groundWind = atof(fields[10]);
    if(thisSample->groundWind < -90 || thisSample->groundWind > 90) {
        sprintf(ErrStr, "Got bad surface wind at line %d: %.2f\n", LineNumber, thisSample->groundWind);
        FatalError("readDataFromLine()", ErrStr);
    }
    
    thisSample->groundRH = atof(fields[11]);
    if(thisSample->groundRH < -1 || thisSample->groundRH > 110) {
        sprintf(ErrStr, "Got bad surface relative humidity at line %d: %.2f\n", LineNumber, thisSample->groundRH);
        FatalError("readDataFromLine()", ErrStr);
    }
     
    int columnIndex, modelIndex;
    for(modelIndex=0,columnIndex=fci->startModelsColumnNumber; modelIndex<fci->numModels; columnIndex++,modelIndex++) {
        thisSample->modelGHIvalues[modelIndex] = atof(fields[fci->readColumns[columnIndex].inputColumnNumber]);
        //if(thisSample->modelGHIvalues[modelIndex] < MIN_GHI_VAL)
        //    thisSample->isValid = False;
    }
    
    if(firstTime) {
        char headerStr[4096];
        
        if(OutputFileName == NULL || (OutputFp = fopen(OutputFileName, "w")) == NULL) {
            sprintf(ErrStr, "Couldn't open output file %s\n", OutputFileName);
            FatalError("readDataFromLine()", ErrStr);
        }
        
        sprintf(headerStr, "#year,month,day,hour,minute,zenith,groundGHI,groundDNI,groundDiffuse,clearskyGHI");
        for(columnIndex=fci->startModelsColumnNumber; columnIndex<fci->numReadColumns; columnIndex++) {

//
//FIX THIS!!
//


sprintf(headerStr, "%s,%s", headerStr, fci->readColumns[columnIndex].columnName);
        }
        fprintf(OutputFp, "%s\n", headerStr);
        firstTime = False;
    }
    
    if(thisSample->isValid) {
        fci->numValidSamples++;
        fprintf(OutputFp, "%s,%.2f,%.0f,%.0f,%.0f,%.0f,", dtToStringCsv2(&thisSample->dateTime), thisSample->zenith, thisSample->groundGHI, thisSample->groundDNI, thisSample->groundDiffuse, thisSample->clearskyGHI);
        for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
            fprintf(OutputFp, "%.1f%s", thisSample->modelGHIvalues[modelIndex], (modelIndex == (fci->numModels - 1)) ? "\n" : ",");
        }
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

void initForecast(forecastInputType *fci)
{
    fci->lat = -999;
    fci->lon = -999;
    fci->siteGroup = NULL;
    fci->siteName = NULL;
    fci->numModels = 0;
    fci->numTotalSamples = 0;
    allocatedSamples = 8670 * MAX_MODELS;
    fci->timeSeries = (timeSeriesType *) malloc(allocatedSamples * sizeof(timeSeriesType));
    fci->numValidSamples = 0;
    fci->numReadColumns = 0;

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
    int i;
    
    // ground and satellite data columns
    fci->zenithCol = fci->numReadColumns;
    registerColumnInfo(fci, "sr_zen", "zenith angle", 0);
    fci->groundGHICol = fci->numReadColumns;
    registerColumnInfo(fci, "sr_global", "ground GHI", 0);   
    fci->groundDNICol = fci->numReadColumns;
    registerColumnInfo(fci, "sr_direct", "ground DNI", 0);   
    fci->groundDiffuseCol = fci->numReadColumns;
    registerColumnInfo(fci, "sr_diffuse", "ground diffuse", 0);   
    fci->satGHICol = fci->numReadColumns;
    registerColumnInfo(fci, "sat_ghi", "sat model GHI", 0);   
    fci->clearskyGHICol = fci->numReadColumns;
    registerColumnInfo(fci, "clear_ghi", "clearsky GHI", 0);   
    
    // this is where the forecast model data starts
    // right now you have to know the model names ahead of time
    registerColumnInfo(fci, "ncep_RAP_DSWRF_1", "NCEP RAP GHI", 1);                      
    //registerColumnInfo(fci, "PERSISTENCE", "PERSISTENCE TO BE ADDED", 1);
    registerColumnInfo(fci, "ncep_NAM_hires_DSWRF_inst_1", "NAM Hi Res Instant GHI", 1);		
    registerColumnInfo(fci, "ncep_NAM_DSWRF_1", "NAM Low Res Instant GHI", 1);	
    registerColumnInfo(fci, "ncep_GFS_sfc_DSWRF_surface_avg_1", "GFS Hi Res Average GHI", 1);		
    registerColumnInfo(fci, "ncep_GFS_sfc_DSWRF_surface_inst_1", "GFS Hi Res Instant GHI", 1);		
    registerColumnInfo(fci, "ncep_GFS_DSWRF_1", "GFS Low res Average GHI", 1);	
    registerColumnInfo(fci, "NDFD_global_1", "NDFD GHI", 1);
  //registerColumnInfo(fci, cm_1", "Cloud motion GHI", 1);
    registerColumnInfo(fci, "ecmwf_ghi_1", "ECMWF average GHI", 1);                             
    
    for(i=0; i<fci->numReadColumns; i++)
        fci->readColumns[i].inputColumnNumber = -1;   // set default
    
}

void getColumnNumbers(forecastInputType *fci, char *colNamesLine)
{
    int numFields;
    char *fields[MAX_FIELDS];
    int i, j, matches=0;
    
    fprintf(stderr, "Line:%s\n", colNamesLine);
    
    numFields = split(colNamesLine, fields, MAX_FIELDS, Delimiter);  /* split line */   
    
    for(i=0; i<numFields; i++) {
        //fprintf(stderr, "Checking column name %s...", fields[i]);
        for(j=0; j<fci->numReadColumns; j++) {
            if(strcasecmp(fields[i], fci->readColumns[j].columnName) == 0) { // || strcasecmp(fields[i], fci->readColumns[j].columnDescription) == 0) {
                fci->readColumns[j].inputColumnNumber = i;
                //fprintf(stderr, "%s\n", fci->readColumns[j].columnName);
                matches++;
                break;
            }
        }
        //if(j == fci->numReadColumns) fprintf(stderr, "no match\n");
    }

    for(i=0; i<fci->numReadColumns; i++)
        fprintf(stderr, "[%d] col=%d, label=%s desc=\"%s\"\n", i, fci->readColumns[i].inputColumnNumber, fci->readColumns[i].columnName, fci->readColumns[i].columnDescription);
    
    for(i=0; i<fci->numReadColumns; i++) {
        if(fci->readColumns[i].inputColumnNumber < 0)
            fprintf(stderr, "No match for expected column %s\n", fci->readColumns[i].columnName);
        else
            fprintf(stderr, "Got column %s\n", fci->readColumns[i].columnName);
    }

    if(matches < fci->numReadColumns) {
        sprintf(ErrStr, "Scanning line %d, only got %d out of %d matches with expected fields", LineNumber, matches, fci->numReadColumns);
        FatalError("getColumnNumbers()", ErrStr);
    }
    
    //for(i=)
    fci->startModelsColumnNumber = fci->numReadColumns - fci->numModels;
}

void registerColumnInfo(forecastInputType *fci, char *columnName, char *columnDescription, char isModel) 
{
    //modelColumn = fci->numReadColumns;   
    fci->readColumns[fci->numReadColumns].columnName = columnName; //"ncep_RAP_DSWRF_1";                      
    fci->readColumns[fci->numReadColumns].columnDescription = columnDescription; //"NCEP RAP GHI";   
    
    if(isModel) {
        fci->models[fci->numModels].modelName = fci->readColumns[fci->numReadColumns].columnDescription;
        fci->numModels++;
    }
    fci->numReadColumns++;
}

void runErrorAnalysis(forecastInputType *fci) 
{
    int modelIndex;
    modelErrorType *err;

    doErrorAnalysis(fci);
    
    fprintf(stderr, "#siteName=%s,lat=%.2f,lon=%.3f,N=%d,mean measured GHI=%.1f\n",fci->siteName, fci->lat, fci->lon, fci->numValidSamples, fci->meanMeasuredGHI);
    fprintf(stderr, "#model,sum( model-ground ), sum( abs(model-ground) ), sum( (model-ground)^2 ), mae, mae percent, mbe, mbe percent, rmse, rmse percent\n");
    
    for(modelIndex=0; modelIndex < fci->numModels; modelIndex++) {
        err = &fci->models[modelIndex].modelError;
        fprintf(stderr, "%s,%.0f,%.0f,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", fci->models[modelIndex].modelName, err->sumModel_Ground, err->sumAbs_Model_Ground, err->sumModel_Ground_2,
                        err->mae, err->maePct*100, err->mbe, err->mbePct*100, err->rmse, err->rmsePct*100);
    }

}
