 // Feb/2013 --Added bilinear interpolation code to replace the nearest-neighbor scheme I
// used to run. 

// meant to work independently from satModel.h stuff so that programs can be
// written that operate on gridded files w/out all the stuff of satelliteType

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include "gridded.h"
#include "ioUtils.h"
#include "timeUtils.h"

extern int errno;
char ErrStr[4096];
int iceil(double x);
int ifloor(double x);


#define FLOAT_TOL 0.0001

int resampleGrid(gridDataType *originalGrid)
{
    gridDataType *resampledGrid = (gridDataType *) originalGrid->resampledGrid;
    
    // first determine if this is a super sample or a subsample
    if(originalGrid->hed.grid_size_deg > originalGrid->resampleRes)
        originalGrid->resampleType = biLinear;
    else
        originalGrid->resampleType = nearestNeighbor;
    
    if(originalGrid->resampleRes < 0 || originalGrid->resampleRes > 10) {
        sprintf(ErrStr, "grid resample size resolution of range: %.3f", originalGrid->resampleRes);
        return False;
    }
       
    // In most cases of interpolation the grid file we're read in is of a lower resolution than the 
    // what we're working at (usually the satellite viz files).  So we need to take sparse data and 
    // fill it out.
    
    
    // int scaleFactor = (int) iround(originalGrid->hed.grid_size_deg/originalGrid->resampleRes);
    
    resampledGrid->hed.grid_size_deg = originalGrid->resampleRes;  // the resolution of resampled grid
    resampledGrid->hed.bytes_element = originalGrid->hed.bytes_element; // Used by check memory to decide whether to allocate grid data

    // now set the number of rows and columns in the grid.
    // satModel note: this should match what's in the satellite viz files exactly
    resampledGrid->rows = resampledGrid->readRows = (int)((resampledGrid->clipUpperLeftLat - resampledGrid->clipLowerRightLat) / originalGrid->resampleRes + 1.5);
    resampledGrid->cols = resampledGrid->readCols = (int)((resampledGrid->clipLowerRightLon - resampledGrid->clipUpperLeftLon) / originalGrid->resampleRes + 1.5);
    
    if(!checkMemory(resampledGrid))
        return False;
    
    // Don't know if lat/lon assignment is really needed in the long run but the memory has been
    // allocated and having them calculated is necessary to figure out if the interpolation is
    // working correctly.

    // lat and lon are reported in "centered" coordinates
    double startLat = resampledGrid->clipUpperLeftLat; // getting these right is important
    double startLon = resampledGrid->clipUpperLeftLon;
    double currLon, currLat = startLat;  // lat changes more slowly than lon
    grid_t *g;
        
    int row, col;
    for (row=0; row<resampledGrid->readRows; row++) {        
        currLon = startLon;  // lon changes quickly
        for (g=resampledGrid->data[row], col=0; col < resampledGrid->readCols; g++, col++) {            
            g->lat = currLat;
            g->lon = currLon;
            g->value = interpBilinear(originalGrid, g);
            /*
            if(originalGrid->resampleType == biLinear) 
                g->value = interpBilinear(originalGrid, g);
            else
                g->value = interpNearNeighbor(originalGrid, g);
            */
            currLon += originalGrid->resampleRes;
        }
        currLat -= originalGrid->resampleRes;
    }
    
//#define DO_RESAMPLE_DUMP
#ifdef DO_RESAMPLE_DUMP
    // this code just dumps the resampled data to a grid file for inspection
    FILE *fp;
    char fname[1024];
    
    char temp[512];
    int numFields;
    char *fields[64];

    strcpy(temp, originalGrid->fileName);
    numFields = split(temp, fields, 64, "/");  /* split line */
    

    sprintf(fname, "/tmp/%s", fields[numFields-1]);
    if((fp = fopen(fname, "w")) == NULL) {
        fprintf(stderr, "Couldn't open resample grid file for dumping: /tmp/%s", fields[numFields-1]);
        perror("Exiting");
        exit(1);
    }
    
    static float **data = NULL;   
    if(data == NULL) {
        data = (float **) malloc(resampledGrid->readRows * sizeof(float *));
        for(col=0; col<resampledGrid->readRows; col++) {
            data[col] = (float *) malloc(resampledGrid->readCols * sizeof(float));
        }
    }
    
    for (row=0; row < resampledGrid->readRows; row++)         
        for (col=0; col < resampledGrid->readCols; col++) {        
            
/*            if(row == 0 && resampledGrid->data[row][col].lon > -111.55)
                fprintf(stderr, "%d, %d, %.03f, %.03f, %.03f\n", row, col, resampledGrid->data[row][col].lat, resampledGrid->data[row][col].lon, resampledGrid->data[row][col].value);
*/
            data[row][col] = resampledGrid->data[row][col].value;
        }
   
    resampledGrid->hed.grid_size_deg = originalGrid->resampleRes;
    resampledGrid->hed.obs_time = originalGrid->hed.obs_time;
    resampledGrid->hed.maxlat = resampledGrid->clipUpperLeftLat + originalGrid->resampleRes/2; 
    resampledGrid->hed.minlon = resampledGrid->clipUpperLeftLon - originalGrid->resampleRes/2;
    resampledGrid->hed.minlat = resampledGrid->clipLowerRightLat - originalGrid->resampleRes/2;
    resampledGrid->hed.maxlon = resampledGrid->clipLowerRightLon + originalGrid->resampleRes/2;
    
    sprintf(resampledGrid->hed.description, "resampled grid file/bilinear");
        
    if(!gridWriteHeaderFloat(fp, &resampledGrid->hed)) {
        fprintf(stderr, "Problem writting to file %s\n", fname);
        exit(1);
    }

    if(!gridWriteDataFloat(fp, data, resampledGrid->readRows, resampledGrid->readCols)) {
        fprintf(stderr, "Problem writting to file %s\n", fname);
        exit(1);
    }

    fclose(fp);

#endif

    return True;
}

double interpBilinear(gridDataType *grid, grid_t *targetRes)
{
    // first do it brute force
    // get NW, NE, SW & SE enclosing corners
    grid_t NW, NE, SW, SE;
    double latDiff, lonDiff, R1, R2, interpVal = -999;
    int NElatpix, NElonpix, NWlatpix, NWlonpix, 
        SElatpix, SElonpix, SWlatpix, SWlonpix;
    
    // lat
    if(targetRes->lat >= grid->maxLat) {
        // check that they're not too far off
        NW.lat = NE.lat = SW.lat = SE.lat = grid->maxLat;
    }
    else if(targetRes->lat <= grid->minLat) {
        NW.lat = NE.lat = SW.lat = SE.lat = grid->minLat;
    }
    else {
        latDiff = grid->maxLat - targetRes->lat;  
        NW.lat = NE.lat = grid->maxLat - (ifloor(latDiff/grid->hed.grid_size_deg) * grid->hed.grid_size_deg);
        SW.lat = SE.lat = grid->maxLat - ( iceil(latDiff/grid->hed.grid_size_deg) * grid->hed.grid_size_deg);
    }
    
    // lon
    if(targetRes->lon >= grid->maxLon) {
        NW.lon = NE.lon = SW.lon = SE.lon = grid->maxLon;
    }
    else if(targetRes->lon <= grid->minLon) {
        NW.lon = NE.lon = SW.lon = SE.lon = grid->minLon;
    }
    else {
        lonDiff = targetRes->lon - grid->minLon;  // will this work with -lon?
        NW.lon = SW.lon = grid->minLon + (ifloor(lonDiff/grid->hed.grid_size_deg) * grid->hed.grid_size_deg);
        NE.lon = SE.lon = grid->minLon + ( iceil(lonDiff/grid->hed.grid_size_deg) * grid->hed.grid_size_deg);
    }    
    
    // int     latlon2GridPix(gridDataType *grid, double lat, double lon, int *latpix, int *lonpix);
    (void) latlon2GridPix(grid, NW.lat, NW.lon, &NWlatpix, &NWlonpix); 
    (void) latlon2GridPix(grid, NE.lat, NE.lon, &NElatpix, &NElonpix);
    (void) latlon2GridPix(grid, SW.lat, SW.lon, &SWlatpix, &SWlonpix);
    (void) latlon2GridPix(grid, SE.lat, SE.lon, &SElatpix, &SElonpix);
    NW.value = grid->data[NWlatpix][NWlonpix].value;
    NE.value = grid->data[NElatpix][NElonpix].value;
    SW.value = grid->data[SWlatpix][SWlonpix].value;
    SE.value = grid->data[SElatpix][SElonpix].value;
    
    if(NW.lat < targetRes->lat || NW.lon > targetRes->lon)
        fprintf(stderr, "NW point is out of whack\n");
    if(NE.lat < targetRes->lat || NE.lon < targetRes->lon)
        fprintf(stderr, "NE point is out of whack\n");
    if(SE.lat > targetRes->lat || SE.lon < targetRes->lon)
        fprintf(stderr, "SE point is out of whack\n");
    if(SW.lat > targetRes->lat || SW.lon > targetRes->lon)
        fprintf(stderr, "SW point is out of whack\n");

    // OK, now we have the bounding points.  The interpolation is pretty straightforward from here.
    /* 
     * first we interpolate in the x direction (longitude) at the top and bottom of the bounding box
     * then we interpolate in the y direction (latitude) to arrive at an estimate.     
     */
    double londist = NE.lon - NW.lon;
    double latdist = NW.lat - SW.lat;
    
    if(latdist <= 0 || londist <= 0) {
        fprintf(stderr, "interpBilinear(): In file %s : internal error: latdist or londist < 0: latdist = %f londist = %f\n", grid->fileName, latdist, londist);
        fprintf(stderr, "grid: \tul = [%.03f,%.03f]\n\tlr = [%.03f,%.03f]\n", grid->maxLat, grid->minLon, grid->minLat, grid->maxLon);
        fprintf(stderr, "target point = [%.03f,%.03f]\n", targetRes->lat, targetRes->lon);
        fprintf(stderr, "file resolution = %.2f\n", grid->hed.grid_size_deg);       
        exit(1);
    }
    
    R1 = SW.value * (SE.lon - targetRes->lon)/londist + SE.value * (targetRes->lon - SW.lon)/londist;
    R2 = NW.value * (NE.lon - targetRes->lon)/londist + NE.value * (targetRes->lon - NW.lon)/londist;
    interpVal = R1 * (NW.lat - targetRes->lat)/latdist + R2 * (targetRes->lat - SW.lat)/latdist;
//#define DEBUG_BILINEAR
#ifdef DEBUG_BILINEAR
    if(0 && targetRes->lon > -111.3) {    
        fprintf(stderr, "%s:\n", grid->fileName);
        fprintf(stderr, "-----\nlow res: \tul = [%.03f,%.03f]\n\t\tlr = [%.03f,%.03f]\n", grid->maxLat, grid->minLon, grid->minLat, grid->maxLon);
        fprintf(stderr, "target point = [%.03f,%.03f]\n", targetRes->lat, targetRes->lon);
        fprintf(stderr, "sub window :\tNW = [%.03f, %.03f] [%d, %d] %.03f\n", NW.lat, NW.lon, NWlatpix, NWlonpix, NW.value);
        fprintf(stderr, "\t\tNE = [%.03f, %.03f] [%d, %d] %.03f\n", NE.lat, NE.lon, NElatpix, NElonpix, NE.value);
        fprintf(stderr, "\t\tSW = [%.03f, %.03f] [%d, %d] %.03f\n", SW.lat, SW.lon, SWlatpix, SWlonpix, SW.value);
        fprintf(stderr, "\t\tSE = [%.03f, %.03f] [%d, %d] %.03f\n", SE.lat, SE.lon, SElatpix, SElonpix, SE.value);
        fprintf(stderr, "\t\tR1 = %.03f\n", R1);
        fprintf(stderr, "\t\tR2 = %.03f\n", R2);
        fprintf(stderr, "\t\tinterpVal = %.03f\n", interpVal);
    }
    if(1) {
        fprintf(stderr, "[%.03f %.03f, %.03f %.03f, %.03f %.03f, %.03f %.03f] [%.02f,%.02f,%.02f,%.02f] %.02f target point=[%.03f,%.03f]\n", NW.lon, NW.lat, NE.lon, NE.lat, SW.lon, SW.lat, SE.lon, SE.lat, NW.value, NE.value, SW.value, SE.value, interpVal, targetRes->lon, targetRes->lat);
    }
#endif
    
    if(isnan(interpVal)) {
        fprintf(stderr, "interpBiLinear(): In file %s : Got NAN at lat=%.3f, lon=%.3f!\nExiting.\n", grid->fileName, targetRes->lat, targetRes->lon);
        exit(1);
    }

    return interpVal; 
}
    
double interpNearNeighbor(gridDataType *grid, grid_t *loRes)
{
    // first do it brute force
    // get NW, NE, SW & SE enclosing corners
    grid_t NW, NE, SW, SE;
    double latDiff, lonDiff, R1, R2, interpVal = -999;
    int NElatpix, NElonpix, NWlatpix, NWlonpix, 
        SElatpix, SElonpix, SWlatpix, SWlonpix;
    
    // lat
    if(loRes->lat >= grid->maxLat) {
        // check that they're not too far off
        NW.lat = NE.lat = SW.lat = SE.lat = grid->maxLat;
    }
    else if(loRes->lat <= grid->minLat) {
        NW.lat = NE.lat = SW.lat = SE.lat = grid->minLat;
    }
    else {
        latDiff = grid->maxLat - loRes->lat;  
        NW.lat = NE.lat = grid->maxLat - (ifloor(latDiff/grid->hed.grid_size_deg) * grid->hed.grid_size_deg);
        SW.lat = SE.lat = grid->maxLat - ( iceil(latDiff/grid->hed.grid_size_deg) * grid->hed.grid_size_deg);
    }
    
    // lon
    if(loRes->lon >= grid->maxLon) {
        NW.lon = NE.lon = SW.lon = SE.lon = grid->maxLon;
    }
    else if(loRes->lon <= grid->minLon) {
        NW.lon = NE.lon = SW.lon = SE.lon = grid->minLon;
    }
    else {
        lonDiff = loRes->lon - grid->minLon;  // will this work with -lon?
        NW.lon = SW.lon = grid->minLon + (ifloor(lonDiff/grid->hed.grid_size_deg) * grid->hed.grid_size_deg);
        NE.lon = SE.lon = grid->minLon + ( iceil(lonDiff/grid->hed.grid_size_deg) * grid->hed.grid_size_deg);

    }    
        
    // int     latlon2GridPix(gridDataType *grid, double lat, double lon, int *latpix, int *lonpix);
    (void) latlon2GridPix(grid, NW.lat, NW.lon, &NWlatpix, &NWlonpix); 
    (void) latlon2GridPix(grid, NE.lat, NE.lon, &NElatpix, &NElonpix);
    (void) latlon2GridPix(grid, SW.lat, SW.lon, &SWlatpix, &SWlonpix);
    (void) latlon2GridPix(grid, SE.lat, SE.lon, &SElatpix, &SElonpix);
    NW.value = grid->data[NWlatpix][NWlonpix].value;
    NE.value = grid->data[NElatpix][NElonpix].value;
    SW.value = grid->data[SWlatpix][SWlonpix].value;
    SE.value = grid->data[SElatpix][SElonpix].value;
    
    if(NW.lat < loRes->lat || NW.lon > loRes->lon)
        fprintf(stderr, "NW point is out of whack\n");
    if(NE.lat < loRes->lat || NE.lon < loRes->lon)
        fprintf(stderr, "NE point is out of whack\n");
    if(SE.lat > loRes->lat || SE.lon < loRes->lon)
        fprintf(stderr, "SE point is out of whack\n");
    if(SW.lat > loRes->lat || SW.lon > loRes->lon)
        fprintf(stderr, "SW point is out of whack\n");

    // OK, now we have the bounding points.  The interpolation is pretty straightforward from here.
    /* 
     * first we interpolate in the x direction (longitude) at the top and bottom of the bounding box
     * then we interpolate in the y direction (latitude) to arrive at an estimate.     
     */
    double londist = NE.lon - NW.lon;
    double latdist = NW.lat - SW.lat;
    
    if(latdist <= 0 || londist <= 0) {
        fprintf(stderr, "interpBilinear: internal error: latdist or londist < 0: latdist = %f londist = %f\n", latdist, londist);
        exit(1);
    }
    
        // calculate neighbor distances and "sort" them
    double distNW = sqrt(pow(NW.lat - loRes->lat, 2) + pow(NW.lon - loRes->lon, 2));
    double distNE = sqrt(pow(NE.lat - loRes->lat, 2) + pow(NE.lon - loRes->lon, 2));
    double distSW = sqrt(pow(SW.lat - loRes->lat, 2) + pow(SW.lon - loRes->lon, 2));
    double distSE = sqrt(pow(SE.lat - loRes->lat, 2) + pow(SE.lon - loRes->lon, 2));
    double minDist = distNW;
    interpVal = NW.value;
    if(minDist > distNE) { minDist = distNE; interpVal = NE.value; }
    if(minDist > distSW) { minDist = distSW; interpVal = SW.value; }
    if(minDist > distSE) { minDist = distSE; interpVal = SE.value; }

    if(0 && loRes->lon > -111.3) {    
        fprintf(stderr, "%s:\n", grid->fileName);
        fprintf(stderr, "-----\nlow res: \tul = [%.03f,%.03f]\n\t\tlr = [%.03f,%.03f]\n", grid->maxLat, grid->minLon, grid->minLat, grid->maxLon);
        fprintf(stderr, "target point = [%.03f,%.03f]\n", loRes->lat, loRes->lon);
        fprintf(stderr, "sub window :\tNW = [%.03f, %.03f] [%d, %d] %.03f\n", NW.lat, NW.lon, NWlatpix, NWlonpix, NW.value);
        fprintf(stderr, "\t\tNE = [%.03f, %.03f] [%d, %d] %.03f\n", NE.lat, NE.lon, NElatpix, NElonpix, NE.value);
        fprintf(stderr, "\t\tSW = [%.03f, %.03f] [%d, %d] %.03f\n", SW.lat, SW.lon, SWlatpix, SWlonpix, SW.value);
        fprintf(stderr, "\t\tSE = [%.03f, %.03f] [%d, %d] %.03f\n", SE.lat, SE.lon, SElatpix, SElonpix, SE.value);
        fprintf(stderr, "\t\tR1 = %.03f\n", R1);
        fprintf(stderr, "\t\tR2 = %.03f\n", R2);
        fprintf(stderr, "\t\tinterpVal = %.03f\n", interpVal);
    }
    
    if(isnan(interpVal)) 
        fprintf(stderr, "bad val\n");
    return interpVal; 
}

// just keeping this separate until I get all the bugs out, then it can replace the original
int openReadGridfile2(gridDataType *grid)
{

    if(grid->fileName == NULL || grid->fileName[0] == 0) {
        fprintf(stderr, "openReadGridfile2: grid filename is null\n");
        return False;
    }

    // it might be more robust to not do this automatically
    if(grid->initDone != GRID_INIT_KEY)
        initGrid(grid);

    grid->swapByteOrder = False;

    // fprintf(stderr, "GRID_INIT_KEY = %ld, %X, initDone = %ld\n", GRID_INIT_KEY, GRID_INIT_KEY, grid->initDone);

    if ((grid->fp = fopen(grid->fileName, "rb")) == NULL) {
        fprintf(stderr, "openReadGridfile2: can't open '%s' file: %s\n", grid->fileName, strerror(errno));
        return False;
    }

//#define DUMPFIRST512
#ifdef DUMPFIRST512
    fprintf(stderr, "First 512 bytes of %s\n", grid->fileName);
    unsigned char buff[512];
    if (fread(buff, 512, 1, grid->fp) != 1)
        exit(1);

    int i;
    for(i=0;i<512;i++) {
        fprintf(stderr, "%u ", buff[i]);
        if(i && !(i % 16))
            fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");

    fseek(grid->fp, 0, SEEK_SET);
#endif

    // size_t gridHeadSize = sizeof(gridHeaderType);
    if (fread(&grid->hed, sizeof(gridHeaderType), 1, grid->fp) != 1) {
        fprintf(stderr, "openReadGridfile2: Can't read header from '%s'\n", grid->fileName);
        fclose(grid->fp);
        return False;
    }

    // are we trying to read a 32-bit file on a 64-bit machine?
    // note that this is a different issue from byte order
    if(grid->hed.is64bitFlag != GRID_64BIT_FLAG_VAL && is64bitArch()) {
        read32bitHeader(grid);
    }

    // take care of byte swapping in header here
    if(!checkHeaderSanity(grid))
        return False;

    // before reading in grid data, check to see if this grid is going to be resampled
    // as happens when an auxiliary data file is of lower resolution than the satellite
    // grid files.  In this case, we allocate space for the resampled grid and copy over
    // the clipping window in the new resolution, which will be needed for resampling.
    /*
    if(fabs(grid->hed.grid_size_deg - modelRes) > 0.0001 && grid->resampledGrid == NULL ) {
        grid->doResampling = True;  
        grid->resampledGrid = (gridDataType *) malloc(sizeof(gridDataType));  // create object
        initGrid((gridDataType *) grid->resampledGrid); // initialize it
        copyGridClipData((gridDataType *) grid->resampledGrid, grid);  // copy over new res clipping window
    }
    */
    // forget all the version check stuff for right now

    int readSuccess = readGrid(grid);

    // finally, load struct tm
    struct tm *tm = gmtime((time_t *) &grid->hed.obs_time);
    grid->tm = *tm;  // make a permanent copy

    fclose(grid->fp);  // make sure we leave the file closed

    // this is abit kludgy but we embed Synthetic (i.e., interpolated/estimated) in grid file description
    if(strstr(grid->hed.description, "ynthetic") == NULL)
        grid->isSynthetic = False;
    else
        grid->isSynthetic = True;
/*
    if(grid->doResampling && readSuccess) {
        // fprintf(stderr, "Resampling grid, resolution = %lf, modelRes = %lf\n", grid->hed.grid_size_deg, modelRes);
        grid->resampleRes = modelRes;
        if(!resampleGrid(grid))
            return False;
    }
*/
    
    return readSuccess;
}

/*
int openReadGridfile(gridDataType *grid)
{

    if(grid->fileName == NULL || grid->fileName[0] == 0) {
        fprintf(stderr, "openReadGridfile: no grid filename to open.\n");
        return False;
    }

    // it might be more robust to not do this automatically
    if(grid->initDone != GRID_INIT_KEY)
        initGrid(grid);

    grid->swapByteOrder = False;

    // fprintf(stderr, "GRID_INIT_KEY = %ld, %X, initDone = %ld\n", GRID_INIT_KEY, GRID_INIT_KEY, grid->initDone);

    if ((grid->fp = fopen(grid->fileName, "rb")) == NULL) {
        fprintf(stderr, "openReadGridfile: can't open '%s' file\n", grid->fileName);
        return False;
    }

//#define DUMPFIRST512
#ifdef DUMPFIRST512
    fprintf(stderr, "First 512 bytes of %s\n", grid->fileName);
    unsigned char buff[512];
    if (fread(buff, 512, 1, grid->fp) != 1)
        exit(1);

    int i;
    for(i=0;i<512;i++) {
        fprintf(stderr, "%u ", buff[i]);
        if(i && !(i % 16))
            fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");

    fseek(grid->fp, 0, SEEK_SET);
#endif

    // size_t gridHeadSize = sizeof(gridHeaderType);
    if (fread(&grid->hed, sizeof(gridHeaderType), 1, grid->fp) != 1) {
        fprintf(stderr, "openReadGridfile: Can't read header from '%s'\n", grid->fileName);
        fclose(grid->fp);
        return False;
    }

    // are we trying to read a 32-bit file on a 64-bit machine?
    // note that this is a different issue from byte order
    if(grid->hed.is64bitFlag != GRID_64BIT_FLAG_VAL && is64bitArch()) {
        read32bitHeader(grid);
    }

    // take care of byte swapping in header here
    if(!checkHeaderSanity(grid))
        return False;

    // forget all the version check stuff for right now

    int retval = readGrid(grid);

    // finally, load struct tm
    struct tm *tm = gmtime((time_t *) &grid->hed.obs_time);
    grid->tm = *tm;  // make a permanent copy

    fclose(grid->fp);  // make sure we leave the file closed

    // this is abit kludgy but we embed Synthetic (i.e., interpolated/estimated) in grid file description
    if(strstr(grid->hed.description, "ynthetic") == NULL)
        grid->isSynthetic = False;
    else
        grid->isSynthetic = True;
    
    return retval;
}
*/


/* Byte Swapping Code -- used for reading files wrtten on a different architecture
   i.e., sparc => intel
 */
void byteSwap(unsigned char * b, int n)
{
   unsigned char tmp;
   int i = 0;
   int j = n-1;
   while (i<j)
   {
      tmp = b[i];
      b[i] = b[j];
      b[j] = tmp;
      i++, j--;
   }
}

    /* the header is assumed to be in grid->hed already
       we have to make assumptions about the size of 32bit data types here
       I will use what comes back from solaris 10
       Here's what we get:
        char = 1
        short = 2
        int = 4
        long = 4
        time_t = 4
        float = 4
        double = 8
       Now, on a 64bit intel redheat system:
        char = 1
        short = 2
        int = 4
        long = 8
        time_t = 8
        float = 4
        double = 8

    And the header:

    char        version[8];
    time_t        obs_time, arch_time;
    double        grid_size_deg, minlat, maxlat, minlon, maxlon;
    data_t        data_type;
    int                bytes_element;
    char        description[64];
    double      is64bitFlag;
    */

void read32bitHeader(gridDataType *grid)
{
    // the upshot is that only time_t and long have changed size
    unsigned char buff[GRID_HEADER_SIZE], *ptr;
    int i;

    ptr = (unsigned char *) &grid->hed;

    for(i=0;i<sizeof(gridHeaderType); i++) {
        buff[i] = ptr[i];
        ptr[i] = 0;  // backfill with nulls
    }

    int offset=0;
    memcpy(grid->hed.version, buff, 8); offset+=8;
    ptr = (unsigned char *) &grid->hed.obs_time;
    memcpy(ptr, buff+offset, 4);        offset+=4;
    ptr = (unsigned char *) &grid->hed.arch_time;
    memcpy(ptr, buff+offset, 4);        offset+=4;
    ptr = (unsigned char *) &grid->hed.grid_size_deg;
    memcpy(ptr, buff+offset, 8);        offset+=8;
    ptr = (unsigned char *) &grid->hed.minlat;
    memcpy(ptr, buff+offset, 8);        offset+=8;
    ptr = (unsigned char *) &grid->hed.maxlat;
    memcpy(ptr, buff+offset, 8);        offset+=8;
    ptr = (unsigned char *) &grid->hed.minlon;
    memcpy(ptr, buff+offset, 8);        offset+=8;
    ptr = (unsigned char *) &grid->hed.maxlon;
    memcpy(ptr, buff+offset, 8);        offset+=8;
    ptr = (unsigned char *) &grid->hed.data_type;
    memcpy(ptr, buff+offset, 4);        offset+=4;
    ptr = (unsigned char *) &grid->hed.bytes_element;
    memcpy(ptr, buff+offset, 4);        offset+=4;
    memcpy(grid->hed.description, buff+offset, 64);        offset+=64;
    ptr = (unsigned char *) &grid->hed.is64bitFlag;
    memcpy(ptr, buff+offset, 8);

}
int isBigEndian(void)
{
    int i = 1;
    char *p = (char *)&i;

    // under big endian, i => 0x 0x 0x 1x
    // under little end, i => 1x 0x 0x 0x

    if(p[0] == 1)
        return False;

    return True;
}

int is64bitArch(void)
{
    return(sizeof(char *) == 8);
}

/* Byte Swapping Code */

int checkHeaderSanity(gridDataType *grid)
{
    int retVal = True;

    // first we assume that bad header values are due to byte order
    if( (grid->hed.bytes_element < 1 || grid->hed.bytes_element > 16) &&
          (grid->hed.grid_size_deg < 0.001 || grid->hed.grid_size_deg > 100) ) {
        // fprintf(stderr, "SWAPPING BYTE ORDER on file %s!\n", grid->fileName);
        swapHeaderBytes(grid);
        grid->swapByteOrder = True;
    }

    // often double garbage are very small numbers which wouldn't be caught by tests like these
    if(grid->hed.minlat < -90 || grid->hed.minlat > 90) {
        fprintf(stderr, "Header looks funny in %s: minlat = %.2f\n", grid->fileName, grid->hed.minlat);
        retVal = False;
    }
    if(grid->hed.maxlat < -90 || grid->hed.maxlat > 90) {
        fprintf(stderr, "Header looks funny in %s: maxlat = %.2f\n", grid->fileName, grid->hed.maxlat);
        retVal = False;
    }
    if(grid->hed.minlon < -180 || grid->hed.minlon > 180) {
        fprintf(stderr, "Header looks funny in %s: minlon = %.2f\n", grid->fileName, grid->hed.minlon);
        retVal = False;
    }
    if(grid->hed.maxlon < -180 || grid->hed.maxlon > 180) {
        fprintf(stderr, "Header looks funny in %s: maxlon = %.2f\n", grid->fileName, grid->hed.maxlon);
        retVal = False;
    }
    if(grid->hed.grid_size_deg < 0.001 || grid->hed.grid_size_deg > 100) {
        fprintf(stderr, "Header looks funny in %s: grid_size_deg = %.3f\n", grid->fileName, grid->hed.grid_size_deg);
        retVal = False;
    }
    if(grid->hed.bytes_element < 1 || grid->hed.bytes_element > 16) {
        fprintf(stderr, "Header looks funny in %s: bytes_element = %d\n", grid->fileName, grid->hed.bytes_element);
        retVal = False;
    }

    return retVal;
}

    // this swapping requires intimate knowledge of the header
    /*
    char        version[8];
    time_t        obs_time, arch_time;
    double        grid_size_deg, minlat, maxlat, minlon, maxlon;
    data_t        data_type;
    int                bytes_element;
    char        description[64];
     */

void swapHeaderBytes(gridDataType *grid)
{
    byteSwap((unsigned char *) &grid->hed.obs_time, 4);
    byteSwap((unsigned char *) &grid->hed.arch_time, 4);
    byteSwap((unsigned char *) &grid->hed.grid_size_deg, 8);
    byteSwap((unsigned char *) &grid->hed.minlat, 8);
    byteSwap((unsigned char *) &grid->hed.maxlat, 8);
    byteSwap((unsigned char *) &grid->hed.minlon, 8);
    byteSwap((unsigned char *) &grid->hed.maxlon, 8);
    byteSwap((unsigned char *) &grid->hed.data_type, 4);
    byteSwap((unsigned char *) &grid->hed.bytes_element, 4);

}

void swapByteOrderLine(unsigned char *line, int length, int size)
{
    int i;
    for(i=0; i<length; i++)
        byteSwap(line+(i*size), size);
}

// more or less deprecated
int openReadArchiveSnowGridfile(gridDataType *grid)
{
    char         hed[16];
    archSnowGridHeaderType         *v0_hed;

    if(grid->fileName == NULL) {
        fprintf(stderr, "openReadArchiveSnowGridfile: no grid filename to open.\n");
        return False;
    }

    if(grid->initDone != GRID_INIT_KEY) {
        initGrid(grid);
    }

    grid->gridHeaderSize = 16;  // this is just what it is, man

    if ((grid->fp = fopen(grid->fileName, "rb")) == NULL) {
        fprintf(stderr, "openReadArchiveSnowGridfile: can't open '%s' file\n", grid->fileName);
        return False;
    }

//#define DUMPFIRST512
#ifdef DUMPFIRST512
    fprintf(stderr, "First 512 bytes of %s\n", grid->fileName);
    unsigned char buff[512];
    if (fread(buff, 512, 1, grid->fp) != 1)
        exit(1);

    int i;
    for(i=0;i<512;i++) {
        fprintf(stderr, "%u ", buff[i]);
        if(i && !(i % 16))
            fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");

    fseek(grid->fp, 0, SEEK_SET);
#endif

   if (fread(hed, 16, 1, grid->fp) != 1) {
      fprintf(stderr,"openReadArchiveSnowGridfile(): Can't read grid file %s header\n", grid->fileName);
      fclose(grid->fp);
      return False;
   }

   v0_hed = (archSnowGridHeaderType *) hed;

   // first check for 32bit binary read on 64bit machine and swap integer bytes if so
   if(is64bitArch() && (v0_hed->nc < 1 || v0_hed->nc > 2000 || v0_hed->ullat > 900 || v0_hed->ullat < -900)) {
       byteSwap((unsigned char *) &v0_hed->ullon, 4);
       byteSwap((unsigned char *) &v0_hed->ullat, 4);
       byteSwap((unsigned char *) &v0_hed->nc, 4);
       byteSwap((unsigned char *) &v0_hed->alen, 4);
       grid->swapByteOrder = True;  // for snow we don't actually have to do this since it's 1 byte data
   }

   /*
typedef struct {
    char        version[8];
    time_t        obs_time, arch_time;
    double        grid_size_deg, minlat, maxlat, minlon, maxlon;
    data_t        data_type;
    int                bytes_element;
    char        description[64];
} gridHeaderType;
*/

    // I think that the snow data that comes in is converted into grids without regard to the
    // whole shift-to-the-southwest thing.  Thus we compensate for the shift that will take
    // place within readGrid() by adding 0.05 to the maxlat and -0.05 to the minlon.
   grid->hed.maxlat = (double) v0_hed->ullat/10.0  + 0.05;  // gotta go with the hardcoded deals
   grid->hed.minlon = -v0_hed->ullon/10.0  - 0.05;
   // how it used to be:
   // grid->hed.maxlat = (double) v0_hed->ullat/10.0; //  - 0.05;  // gotta go with the hardcoded deals
   // grid->hed.minlon = -v0_hed->ullon/10.0; //  + 0.05;

   grid->cols = v0_hed->nc;
   grid->rows = v0_hed->alen/grid->cols;
   grid->hed.minlat         = grid->hed.maxlat - grid->rows * 0.1;
   grid->hed.maxlon        = grid->hed.minlon + grid->cols * 0.1;
   grid->hed.data_type   = Unsigned_Char;
   grid->hed.bytes_element = 1;
   grid->hed.grid_size_deg = 0.1;
   sprintf(grid->hed.description, "Archive grid file (converted non-standard header)");
   grid->hed.obs_time = 0;

   if(!readGrid(grid)) {
       dumpHeaderFull(grid);
       return False;
   }

   // dumpHeaderFull(grid);

   // finally, load struct tm
   struct tm *tm = gmtime((time_t *) &grid->hed.obs_time);
   grid->tm = *tm;  // make a permanent copy
   fclose(grid->fp);

   return True;
}

// for when we just want to peak at the header data
int openReadHeadGridfile(gridDataType *grid)
{

    if(grid->fileName == NULL) {
        fprintf(stderr, "openReadHeadGridfile: no grid filename to open.\n");
        return False;
    }

    if(grid->initDone != GRID_INIT_KEY)
        initGrid(grid);

    if ((grid->fp = fopen(grid->fileName, "rb")) == NULL) {
        fprintf(stderr, "openReadHeadGridfile: can't open '%s' file\n", grid->fileName);
        return False;
    }

    if (fread(&grid->hed, sizeof(gridHeaderType), 1, grid->fp) != 1) {
        fprintf(stderr, "openReadHeadGridfile: Can't read header from '%s'\n", grid->fileName);
        fclose(grid->fp);
        return False;
    }

     // are we trying to read a 32-bit file on a 64-bit machine?
    // note that this is a different issue from byte order
    if(grid->hed.is64bitFlag != GRID_64BIT_FLAG_VAL && is64bitArch()) {
        read32bitHeader(grid);
    }

    // take care of byte swapping in header here
    if(!checkHeaderSanity(grid))
        return False;

    // finally, load struct tm
    struct tm *tm = gmtime((time_t *) &grid->hed.obs_time);
    grid->tm = *tm;  // make a permanent copy

    grid->rows = (int) ((grid->hed.maxlat - grid->hed.minlat)/grid->hed.grid_size_deg + 0.5);
    grid->cols = (int) ((grid->hed.maxlon - grid->hed.minlon)/grid->hed.grid_size_deg + 0.5);
    grid->halfGridRes = 0.5 * grid->hed.grid_size_deg;  // half grid resolution

    // figure "centered" grid boundaries -- i.e., shift upper left point to the SE, lower right point to the NW
    grid->maxLat = grid->hed.maxlat - grid->halfGridRes;
    grid->minLat = grid->hed.minlat + grid->halfGridRes;
    grid->minLon = grid->hed.minlon + grid->halfGridRes;
    grid->maxLon = grid->hed.maxlon - grid->halfGridRes;

    fclose(grid->fp);
    return True;
}

// function that acutally does the low level read of the grid file, along with all the data
// interpretation that goes on.

// Be smart about memory usage -- we're going to recycle the grid->data section so that it
// merely grows (if necessary) between readings of grid files

int readGrid(gridDataType *grid)
{
    double        currLat, currLon;
    grid_t        *g=NULL;
    static char        *line=NULL;
    int                row,col, data_size;
    unsigned char   *uc;
    signed char     *sc;
    unsigned short  *us;
    signed short    *ss;
    unsigned int    *ui;
    signed int      *si;
    unsigned long   *ul;
    signed long     *sl;
    float           *fl;
    double          *du;
    int pointIsAligned(double latLon, gridDataType *grid);

    /*
    The nature of the satellite grid is a tad odd.
    In effect, the outer edges are shrunk in by half grid->hed.grid_size_deg (also
    called grid->halfResolution).  
    This is due to the fact that a satellite pixel is actually representative of 
    an area and not a point.  So the four corners are brought in like so:
     * 
     * +-----------------------------+
     * |+---------------------------+|
     * ||                           ||-----> grid given by grid->hed.maxlat, etc.
     * ||                           ||
     * ||                           ||
     * ||                           |----> grid aligned on halfResolution
     * ||                           ||     given by grid->maxLat, etc.
     * ||                           ||
     * ||                           ||
     * ||                           ||
     * ||                           ||
     * ||                           ||
     * ||                           ||
     * ||                           ||
     * ||                           ||
     * |+---------------------------+|
     * +-----------------------------+

    Here is a depiction of the upper left grid cell:

     lon + gridsize/2
       |
       |
    X------
    |  |  |
    |  +--|--- lat - gridsize/2
    |     |
    -------

    where X = [lat,lon] as calculated from grid header value [maxLat,minLon]
          + = grid centered location

    Consider what this means for the four corners of a 0.1 degree grid:
        upper left : [maxlat-0.05, minlon+0.05]
        upper right: [maxlat-0.05, maxlon-0.05]
        lower left : [minlat+0.05, minlon+0.05]
        lower right: [minlat+0.05, maxlon-0.05]

    The header values hed.minlat, hed.maxlat, hed.minlon, hed.maxlon contain
    values that are *not* centered -- that is they are like X, above.

    */

    // this should work whether we've done the centered business or not
    grid->rows = (int) ((grid->hed.maxlat - grid->hed.minlat)/grid->hed.grid_size_deg + 0.5);
    grid->cols = (int) ((grid->hed.maxlon - grid->hed.minlon)/grid->hed.grid_size_deg + 0.5);

    // gridRes is the centered distance -- half the cell dimension
    grid->halfGridRes = 0.5 * grid->hed.grid_size_deg;  // half grid resolution

    // figure "centered" grid boundaries -- i.e., shift upper left point to the SE, lower right point to the NW
    grid->maxLat = grid->hed.maxlat - grid->halfGridRes;
    grid->minLat = grid->hed.minlat + grid->halfGridRes;
    grid->minLon = grid->hed.minlon + grid->halfGridRes;
    grid->maxLon = grid->hed.maxlon - grid->halfGridRes;

    // simple check for valid file
    data_size = grid->hed.bytes_element * grid->rows * grid->cols;
    if (fileSize(grid->fp) != (grid->gridHeaderSize + data_size)) {
        fprintf(stderr, "readGrid: file size seems wrong in %s\n\tO/S: %d bytes, file : %d header + %d data = %d total bytes\n",
            grid->fileName, fileSize(grid->fp), grid->gridHeaderSize, data_size, grid->gridHeaderSize + data_size);
        dumpHeader(grid);
        return False;
    }

    // if we're clipping, set the grid->hed parameters now

    /*
    In an effort to move beyond the whole gridRes, centered vs. not crappola, I'm going to require that
    when a sub window is requested that it is in actual centered coordinates.

    Soon, I hope to be rid of the whole centered vs. not stuff altogether.  But that will involve a
    redefined grid file, which for now is not workable.
    */

    if(grid->doClip) {
        int errCount=0;

        // First we check that the subwindow's lat and lon points are properly aligned for this grid's resolution.  
        // We need to do this because the clipping window might be set according to a different resolution
        // than the current grid file's resolution.  We assume that the grid file's lats and lons are correctly
        // aligned.

       
        // To do this we get back to the upper left corner of the cell: lat = lat + halfRes, lon = lon - halfRes
        if(!pointIsAligned(grid->clipUpperLeftLat + grid->halfGridRes, grid)) {
                if(grid->carp > verbose) fprintf(stderr, "readGrid: adjusting clipUpperLeftLat from %.03f to ", grid->clipUpperLeftLat);
                grid->clipUpperLeftLat = iceil(grid->clipUpperLeftLat/grid->hed.grid_size_deg) * grid->hed.grid_size_deg + grid->halfGridRes;
                if(grid->carp > verbose) fprintf(stderr, "%.03f\n", grid->clipUpperLeftLat);
        }
        if(!pointIsAligned(grid->clipLowerRightLon - grid->halfGridRes, grid)) {
                if(grid->carp > verbose) fprintf(stderr, "readGrid: adjusting clipLowerRightLon from %.03f to ", grid->clipLowerRightLon);
                grid->clipLowerRightLon = iceil(grid->clipLowerRightLon/grid->hed.grid_size_deg) * grid->hed.grid_size_deg + grid->halfGridRes;
                if(grid->carp > verbose) fprintf(stderr, "%.03f\n", grid->clipLowerRightLon);
        }
        if(!pointIsAligned(grid->clipLowerRightLat + grid->halfGridRes, grid)) {
                if(grid->carp > verbose) fprintf(stderr, "readGrid: adjusting clipLowerRightLat from %.03f to ", grid->clipLowerRightLat);
                grid->clipLowerRightLat = ifloor(grid->clipLowerRightLat/grid->hed.grid_size_deg) * grid->hed.grid_size_deg - grid->halfGridRes;
                if(grid->carp > verbose) fprintf(stderr, "%.03f\n", grid->clipLowerRightLat);
        }
        if(!pointIsAligned(grid->clipUpperLeftLon - grid->halfGridRes, grid)) {
                if(grid->carp > verbose) fprintf(stderr, "readGrid: adjusting clipUpperLeftLon from %.03f to ", grid->clipUpperLeftLon);
                grid->clipUpperLeftLon = ifloor(grid->clipUpperLeftLon/grid->hed.grid_size_deg) * grid->hed.grid_size_deg - grid->halfGridRes;
                if(grid->carp > verbose) fprintf(stderr, "%.03f\n", grid->clipUpperLeftLon);
        }


        // check bounds and modify hed variables
        if(grid->maxLat - grid->clipUpperLeftLat < -FLOAT_TOL) {
            if(grid->allowPtsOutsideBounds)
                grid->clipUpperLeftLat = grid->maxLat;
            else {
                fprintf(stderr, "readGrid: clipping to upper left lat (%.2f) that is outside grid bounds (%.2f) for file %s\n",
                    grid->clipUpperLeftLat, grid->maxLat, grid->fileName);
                errCount++;
            }
        }

        if(grid->clipLowerRightLat - grid->minLat < -FLOAT_TOL) {  // see comments about missing frontiers, below
            if(grid->allowPtsOutsideBounds)
                grid->clipLowerRightLat = grid->minLat;
            else {
                fprintf(stderr, "readGrid: clipping to lower right lat (%.2f) that is outside grid bounds (%.2f) for file %s\n",
                    grid->clipLowerRightLat, grid->minLat, grid->fileName);
                errCount++;
            }
        }

        if(grid->clipUpperLeftLon - grid->minLon < -FLOAT_TOL) {
            if(grid->allowPtsOutsideBounds)
                grid->clipUpperLeftLon = grid->minLon;
            else {
                fprintf(stderr, "readGrid: clipping to upper left lon (%.2f) that is outside grid bounds (%.2f) for file %s\n",
                    grid->clipUpperLeftLon, grid->minLon, grid->fileName);
                errCount++;
            }
        }

        if(grid->maxLon - grid->clipLowerRightLon < -FLOAT_TOL) { // see comments about missing frontiers, below
            if(grid->allowPtsOutsideBounds)
                grid->clipLowerRightLon = grid->maxLon;
            else {
                fprintf(stderr, "readGrid: clipping to lower right lon (%.2f) that is outside grid bounds (%.2f) for file %s\n",
                    grid->clipLowerRightLon, grid->maxLon, grid->fileName);
                errCount++;
            }
        }

        if(grid->clipLowerRightLon <= 0 && grid->clipLowerRightLon < grid->clipUpperLeftLon) {
            fprintf(stderr, "readGrid: upper left longitude (%.2f) is less than zero but greater than lower right longitude (%.2f) for file %s\n",
                    grid->clipUpperLeftLon, grid->clipLowerRightLon, grid->fileName);
            errCount++;

        }
        if(grid->clipUpperLeftLat >= 0 && grid->clipLowerRightLat > grid->clipUpperLeftLat) {
            fprintf(stderr, "readGrid: upper left latitude (%.2f) is greater than zero but less than lower right latitude (%.2f) for file %s\n",
                    grid->clipUpperLeftLat, grid->clipLowerRightLat, grid->fileName);
            errCount++;

        }
        

        if(errCount) {
            if(grid->carp >= debug) fprintf(stderr, "readGrid: error count = %d, returning false\n", errCount);
            return False;
        }

               
        // set the boundaries to the clipped grid
        grid->minLon = grid->clipUpperLeftLon;
        grid->maxLon = grid->clipLowerRightLon;
        grid->maxLat = grid->clipUpperLeftLat;
        grid->minLat = grid->clipLowerRightLat;
    }

    // need to snap the clip points to actual points that
    // exist in this resolution.  Also, should be inclusive of
    // the original clip window.
    
    /* Here are the transformations:
     * N latitude & E longitude: snapped = iround(latOrLon/grid_size_deg) * grid_size_deg + grid_size_deg/2
     * S latitude & W longitude: snapped = iround(latOrLon/grid_size_deg) * grid_size_deg - grid_size_deg/2
     */


    int bytes = grid->hed.bytes_element;
    // in doing all this index math we have to keep in mind that:
    //  o rows and cols are in terms of grid resolution (divide by grid_size_deg)
    //  o offsets need to be multplied by grid->hed.bytes_element

    double originalMaxlat = grid->hed.maxlat - grid->halfGridRes;
    double originalMinlon = grid->hed.minlon + grid->halfGridRes;

    // if we're not clipping, these should go to zero
    grid->rowSkip =  grid->doClip ? (int)((originalMaxlat - grid->clipUpperLeftLat)/grid->hed.grid_size_deg + 0.5) : 0;
    grid->colSkip =  grid->doClip ? (int)((grid->clipUpperLeftLon - originalMinlon)/grid->hed.grid_size_deg + 0.5) : 0;
    grid->readRows = grid->doClip ? (int)((grid->clipUpperLeftLat - grid->clipLowerRightLat)/grid->hed.grid_size_deg + 1 + 0.5) : grid->rows;
    grid->readCols = grid->doClip ? (int)((grid->clipLowerRightLon - grid->clipUpperLeftLon)/grid->hed.grid_size_deg + 1 + 0.5) : grid->cols;

    // now we have to back these off if they're right on the borders (kludgey?)
    if(grid->readRows > grid->rows)
        grid->readRows = grid->rows;
    if(grid->readCols > grid->cols)
        grid->readCols = grid->cols;

    int colDiffBytes = (grid->cols - grid->readCols) * bytes;


    /*
    double startLon = (grid->doClip ? grid->clipUpperLeftLon - grid->gridRes : grid->hed.minlon);  // + gridRes?
    if(startLon >= grid->hed.maxlon)
        startLon = grid->hed.maxlon - grid->gridRes;

    double startLat = (grid->doClip ? grid->clipUpperLeftLat + grid->gridRes : grid->hed.maxlat);
    if(startLat <= grid->hed.minlat)
        startLat = grid->hed.minlat + grid->gridRes;
    */

    long skipTo = grid->gridHeaderSize + (grid->cols * grid->rowSkip * bytes) + grid->colSkip * bytes;

    // jump to the begining of the data section
    if(fseek(grid->fp, skipTo, SEEK_SET) == -1) {
        fprintf(stderr, "readGrid(): error trying to fseek to %ld in grid file %s\n", skipTo, grid->fileName);
        return False;
    }

    /*
    fprintf(stderr, "cols=%d\nrows=%d\nreadCols=%d\nreadRows=%d\nrowSkip=%d\ncolSkip=%d\ncolDiffBytes=%d\nskipTo=%d\n",
        grid->cols, grid->rows, grid->readCols, grid->readRows, grid->rowSkip, grid->colSkip, colDiffBytes, skipTo);
    */

    // temporary space for reading the current line of a grid file.  From this line
    // the grid rows get filled in according the the data type of the grid file
    if((line = (char *) realloc(line, grid->readCols * bytes)) == NULL) {
        fprintf(stderr, "realloc failed while reading %s: requestiong %d bytes\n", grid->fileName, grid->readCols * bytes);
        return False;
    }

    // start reading
    // we begin in the upper left hand corner.  Ergo, we're at max latitude and min Longitude.
    // Longitude changes more quickly than latitude, so the 2D reference is grid->data[lat][lon]

    // important gridRes point here: the maxlon, minlon type variables are assumed *not* to have
    // the gridRes "centering" factor included.  Weird and confusng but true.  Furthermore, the
    // gridRes is *subtracted* from minlat and maxlat and *added* to minlon and maxlon, thus implying
    // that the center of the gridRes x gridRes square is southeast of the given point.

    // This implies that the eastern and southern fronteirs don't exist.  This is correct.  For example
    // in a grid that goes from upperleft = [40,60] to lowerright = [21,78], there is a mapping from
    // [40,60] southwest to 39.95,60.05 but there is no such mapping for [21,78] since [20.95,78.05]
    // as that would be outside the grid.  Hey, I didn't design it.

    // make sure all the grid->data memory allocation is up to snuff this needs to be done after
    // grid->readCols and grid->readRows are calculated
    if(!checkMemory(grid))
        return False;

    // lat and lon are reported in "centered" coordinates
    double startLat = grid->maxLat;
    double startLon = grid->minLon;

    currLat = startLat;  // lat changes slowly
    for (row=0; row<grid->readRows; row++) {
        // read next row into temp memory
        if (fread(line, bytes, grid->readCols, grid->fp) != grid->readCols) {
            fprintf(stderr, "readGrid(): read failed on line %d of gridded file %s\n", row+1, grid->fileName);
            return False;
        }


        switch (grid->hed.data_type) {
            case Unsigned_Char:     uc = (unsigned char *) (void *) line; break;
            case Signed_Char:       sc = (signed char *) (void *) line; break;
            case Unsigned_Short:    us = (unsigned short *) (void *) line; break;
            case Signed_Short:      ss = (signed short *) (void *) line; break;
            case Unsigned_Int:      ui = (unsigned int *) (void *) line; break;
            case Signed_Int:        si = (signed int *) (void *) line; break;
            case Unsigned_Long:     ul = (unsigned long *) (void *) line; break;
            case Signed_Long:       sl = (signed long *) (void *) line; break;
            case Float:             fl = (float *) (void *) line; break;
            case Double:            du = (double *) (void *) line; break;
        }

        // if we're reading from 32bit machine data, swap byte order
        if(grid->swapByteOrder && grid->hed.bytes_element > 1)
            swapByteOrderLine((unsigned char *) line, grid->readCols, grid->hed.bytes_element);

        // see above note on gridRes
        currLon = startLon;  // lon changes quickly

        for (g=grid->data[row], col=0; col < grid->readCols; g++, col++) {
            switch (grid->hed.data_type) {
                case Unsigned_Char:     g->value = *uc++; break;
                case Signed_Char:        g->value = *sc++; break;
                case Unsigned_Short:    g->value = *us++; break;
                case Signed_Short:        g->value = *ss++; break;
                case Unsigned_Int:        g->value = *ui++; break;
                case Signed_Int:        g->value = *si++; break;
                case Unsigned_Long:        g->value = *ul++; break;
                case Signed_Long:        g->value = *sl++; break;
                case Float:                g->value = *fl++; break;
                case Double:            g->value = *du++; break;
            }

            g->lat = currLat;
            g->lon = currLon;

//            if(row == 50 && col == 50)
//                fprintf(stderr, "%s: pixel[50][50] : lat = %.2f  lon = %.2f  val = %.2f\n", grid->fileName, g->lat, g->lon, g->value);

            currLon += grid->hed.grid_size_deg;
/*
printf("%d %d %.3f %.3f %.2f\n", i, j, g->lat, g->lon,
                        g->value);
*/
        }

        if(colDiffBytes > 0) {  // wrap around to the begining of the next row in the subwindow
            if (fseek(grid->fp, colDiffBytes, SEEK_CUR)) {
                fprintf(stderr, "readGrid(): seek failed, offset = %d\n", colDiffBytes);
                return False;
            }
        }

        currLat -= grid->hed.grid_size_deg;
    }

    return True;
}

int pointIsAligned(double latLon, gridDataType *grid)
{   
    double trunc(double x);
    // Assuming un-centered coordinate values, check that
    // the input latLon is snapped to a legit grid value
    // for the given grid_size_deg.
     
    /* ex: grid_size_deg = 0.04
     * latLon => intVal.decVal  (e.g., 32.120)
     * if fabs(round(decVal/grid_size_deg) - decVal/grid_size_deg) < FLOAT_TOL
     * if fabs(round(0.12/0.04) - 0.12/0.04) < FLOAT
     * => True
     * 
     * ex: grid_size_deg = 0.04
     * latLon => intVal.decVal  (e.g., 32.130)
     * if fabs(round(decVal/grid_size_deg) - decVal/grid_size_deg) < FLOAT_TOL
     * if fabs(round(0.13/0.04) - 0.13/0.04) < FLOAT
     * => False
     */
    double decimalVal = fabs(latLon - trunc(latLon));
    double multiplier = iround(decimalVal/grid->hed.grid_size_deg);
    double diff = fabs(multiplier * grid->hed.grid_size_deg - decimalVal);
    return (diff < FLOAT_TOL);
}
// do all the memory checks:
// either allocate or reallocate the grid->data memory
// Things to consider:
//   - initially, we allocate everything (data == NULL)
//   - if the current (cols * bytes_element) is different from the (alloc.bytesPerRow) we need to realloc
//      the whole thing
//   - otherwise, if the number of rows has changed, realloc only the number of rows

int checkMemory(gridDataType *grid)
{
    int row;
    // first check to see if nothing's changed -- the most common case

    //fprintf(stderr, "checkMemory() : rows = %d, cols = %d, bytesize = %d, totalBytes = %d\n",
    //        grid->rows, grid->cols, grid->hed.bytes_element, grid->alloc.totalBytes);

    // alloc.totalBytes is -1 initially so this fails the first time through
    if(grid->readRows * grid->readCols * grid->hed.bytes_element <= grid->alloc.totalBytes)
           return True;

    // first we have to allocate *data, then sweep through and allocate **data

    // rather than getting clever, we'll just realloc the whole works if anything has
    // changed.

    // if we have to resize, free up first
    if(grid->data != NULL) {
            for (row=0; row<grid->readRows; row++)
                if(grid->data[row] != NULL)
                    free(grid->data[row]);
            free(grid->data);
    }

    if((grid->data = (grid_t **) calloc(grid->readRows, sizeof(grid_t *))) == NULL) {
        fprintf(stderr, "checkMemory: grid->data memory allocation failure while processing file %s\n", grid->fileName);
        return False;
    }

    // alloc all the columns
    for (row=0; row<grid->readRows; row++)
        if ((grid->data[row] = (grid_t *) malloc(sizeof(grid_t) * (grid->readCols))) == NULL) {
            fprintf(stderr, "checkMemory: grid->data[] memory allocation failure while processing file %s\n", grid->fileName);
            return False;
        }

    grid->alloc.totalBytes = grid->readRows * grid->readCols * grid->hed.bytes_element;

    return True;
}

// initialize the grid struct
// this is done automatically via openReadGridfile2()
// and only needs to be done once -- the fileName field
// can be changed and the same struct (and all its allocated memory)
// can be recycled over and over.
void initGrid(gridDataType *grid)
{
    if(grid->initDone == GRID_INIT_KEY && grid->data != NULL) {
        fprintf(stderr, "initGrid(): initializing an already used grid:\n");
        fprintf(stderr, "\tfilename = %s\n", grid->fileName);
        fprintf(stderr, "\talloc.totalBytes = %ld\n", grid->alloc.totalBytes);
    }
    grid->doClip = False;
    //grid->isShifted = False;
    //if(grid->fp != NULL && grid->initDone == GRID_INIT_KEY)
    //        free(grid->fp);
    grid->fp = NULL;
    grid->data = NULL;
    grid->rows = 0;
    grid->cols = 0;
    grid->alloc.totalBytes = -1;  // this keeps the first totalBytes check from passing in checkMemory()
    grid->alloc.bytesPerRow = 0;
    grid->alloc.numRows = 0;
    grid->alloc.reallocCount = 0;
    grid->gridHeaderSize = GRID_HEADER_SIZE;   // the standard grid header size
    grid->byteOrder = little;   // assume linux
    grid->swapByteOrder = False;  // we only need to swap bytes if we're reading from a different arch.
    grid->doResampling = False;  // used when reading a file whose resolution differs from the config file res
    grid->resampledGrid = NULL;

    // don't init grid->carp as this should be set outside this module

    grid->initDone = GRID_INIT_KEY;     // set initDone flag
    grid->carp = False;  // set it elsewhere if you want it on

   // Old comment: we now set this switch so that snow data can be "extended" outside the boundaries of the satellite
   // data.  This was necessary when we discovered that the snow data maps didn't extend far enough south
   // to cover all of Florida.  The actual value for the missing snow data will have to be set elsewhere
   // and the code there will have to handle checking for points outside the snow data bounds.
    grid->allowPtsOutsideBounds = False;

}

#define CHECKGRIDBOUNDS
double gridGetScaledData(gridDataType *inGrid, int latpix, int lonpix)
{        
    gridDataType *grid;
    
    if(inGrid->doResampling)
        grid = (gridDataType *) inGrid->resampledGrid;
    else 
        grid = inGrid;
    
#ifdef CHECKGRIDBOUNDS
    int badLatpix = (latpix < 0 || latpix > grid->readRows);
    int badLonpix = (lonpix < 0 || lonpix > grid->readCols);

    if(badLatpix || badLonpix) {
        if(badLatpix) 
                fprintf(stderr, "Fatal Error: gridGetScaledData(): for grid  file %s, requested latpix %d is out of range [0-%d] at %s line %d",
                grid->fileName, latpix, grid->readRows, __FILE__, __LINE__);
        if(badLonpix)
                fprintf(stderr, "Fatal Error: gridGetScaledData(): for grid  file %s, requested lonpix %d is out of range [0-%d] at %s line %d",
                grid->fileName, lonpix, grid->readCols, __FILE__, __LINE__);
        exit(1);
    }
#endif
    return(grid->data[latpix][lonpix].value);
}

void copyGridClipData(gridDataType *toGrid, gridDataType *fromGrid)
{
    toGrid->doClip = fromGrid->doClip;
    
    toGrid->clipUpperLeftLat = fromGrid->clipUpperLeftLat;
    toGrid->clipUpperLeftLon = fromGrid->clipUpperLeftLon;
    toGrid->clipLowerRightLat = fromGrid->clipLowerRightLat;
    toGrid->clipLowerRightLon = fromGrid->clipLowerRightLon;
}

void setHedWindow(gridDataType *grid)
{
   // if we are clipping make sure output grid headers reflect that
   grid->hed.maxlat = grid->maxLat + grid->halfGridRes;
   grid->hed.minlon = grid->minLon - grid->halfGridRes;
   grid->hed.minlat = grid->minLat - grid->halfGridRes;
   grid->hed.maxlon = grid->maxLon + grid->halfGridRes;
}

// take a grid, lat & lon and look up the pixel offsets
// we assume here an exact match -- maybe this will need to be
// relaxed in the future.
// anyway, return True on success, False if point lies outside the grid
//
// Modified to do clipped regions when doClip is True
int latlon2GridPix(gridDataType *grid, double lat, double lon, int *latpix, int *lonpix)
{
    // we need to remember that the maxlat, minlon, etc. values are not "centered"
    // that is, they haven't had the gridRes added/subtracted from them yet.

    //double minLat,minLon,maxLat,maxLon;

    /*
     * if(grid->doClip) {
        minLon = grid->clipUpperLeftLon;
        maxLon = grid->clipLowerRightLon;
        maxLat = grid->clipUpperLeftLat;
        minLat = grid->clipLowerRightLat;
    }
    else {
        minLon = grid->hed.minlon + grid->gridRes;
        maxLon = grid->hed.maxlon - grid->gridRes;
        maxLat = grid->hed.maxlat - grid->gridRes;
        minLat = grid->hed.minlat + grid->gridRes;
    }
    */

    if(lat < (grid->minLat - FLOAT_TOL) ||
       lat > (grid->maxLat + FLOAT_TOL) ||
       lon < (grid->minLon - FLOAT_TOL) ||
       lon > (grid->maxLon + FLOAT_TOL))  { 
        fprintf(stderr, "latlon2GridPix(): requested point [%.03f,%.03f] outside grid range: ul=[%.03f,%.03f] to lr=[%.03f,%.03f]\n",
                lat, lon, grid->maxLat, grid->minLon, grid->minLat, grid->maxLon);
        exit(1);
    }// outside the range of grid
        

    *lonpix = (int) ((lon - grid->minLon)/grid->hed.grid_size_deg + 0.5);
    *latpix = (int) ((grid->maxLat - lat)/grid->hed.grid_size_deg + 0.5);

#ifdef TEST_LATLON2GRID_DISABLED
#define LATLON_FLOAT_TOL  0.001
    double latDiff = fabs(grid->data[*y][*x].lat - lat);
    double lonDiff = fabs(grid->data[*y][*x].lon - lon);
    if(latDiff > LATLON_FLOAT_TOL || lonDiff > LATLON_FLOAT_TOL)
            fprintf(stderr, "latlon2GridPix(): input lat, lon != calculated pixel's lat, lon: (%.2f,%.2f) vs. (%.2f, %.2f) @ (%d, %d)\n",
                lat, lon, grid->data[*y][*x].lat, grid->data[*y][*x].lon, *x, *y);
#endif

    return True;
}


void dumpHeader(gridDataType *grid)
{
    char timeStr[GRID_HEADER_SIZE];
    double res = grid->halfGridRes;

    // grid->tm = gmtime((time_t *) &grid->hed.obs_time);
    strftime(timeStr, GRID_HEADER_SIZE, "%C", &grid->tm);

    fprintf(stderr, "dumping grid file: %s [%s]\n", grid->fileName, grid->hed.description);
    fprintf(stderr, "\tgrid dimensions = %d lat pts, %d lon pts\n", grid->rows, grid->cols);
    fprintf(stderr, "\tgrid resolution = %.1f\n\tupper left corner:\n\t\tlat = %.2f\n\t\tlon = %.2f\n\tlower right corner:\n\t\tlat = %.2f\n\t\tlon = %.2f\n",
        grid->hed.grid_size_deg, grid->hed.maxlat-res, grid->hed.minlon+res, grid->hed.minlat+res, grid->hed.maxlon-res);
    fprintf(stderr, "\tbytes per element = %d\n", grid->hed.bytes_element);
    if(grid->doClip) {
         fprintf(stderr, "\tclipped region dimensions = %d lat pts, %d lon pts\n", grid->readRows, grid->readCols);
         fprintf(stderr, "\t\tclipped min lat = %.2f\n\t\tclipped max lat = %.2f\n\t\tclipped min lon = %.2f\n\t\tclipped max lon = %.2f\n",
            grid->clipLowerRightLat, grid->clipUpperLeftLat, grid->clipUpperLeftLon, grid->clipLowerRightLon);
    }
    fprintf(stderr, "\tobs_time = %ld [%s]\n", grid->hed.obs_time, timeStr);

}

void dumpHeaderFull(gridDataType *grid)
{
    fprintf(stderr, "dumping full grid file header\nfilename = %s\nfp = %lx\n", grid->fileName, (unsigned long) grid->fp);
    fprintf(stderr, "gredHeaderSize = %d\n", grid->gridHeaderSize);
    fprintf(stderr, "low level header:\n");
    fprintf(stderr, "\tversion = %s\n", grid->hed.version);
    fprintf(stderr, "\tobs_time = %ld [%s]\n", grid->hed.obs_time, strfstr(&grid->hed.obs_time));
    fprintf(stderr, "\tarch_time = %ld [%s]\n", grid->hed.arch_time, strfstr(&grid->hed.arch_time));
    fprintf(stderr, "\tgrid_size_deg = %.2f\n", grid->hed.grid_size_deg);
    fprintf(stderr, "\tminlat = %.2f\n", grid->hed.minlat);
    fprintf(stderr, "\tmaxlat = %.2f\n", grid->hed.maxlat);
    fprintf(stderr, "\tminlon = %.2f\n", grid->hed.minlon);
    fprintf(stderr, "\tmaxlon = %.2f\n", grid->hed.maxlon);
    fprintf(stderr, "\tdata_type = %d\n", grid->hed.data_type);
    fprintf(stderr, "\tbytes_element = %d\n", grid->hed.bytes_element);
    fprintf(stderr, "\tdescription = %s\n", grid->hed.description);
    fprintf(stderr, "\tis64bitFlag = %f\n", grid->hed.is64bitFlag);

    fprintf(stderr, "alloc settings:\n");
    fprintf(stderr, "\tbytesPerRow = %ld\n", grid->alloc.bytesPerRow);
    fprintf(stderr, "\tnumRows = %ld\n", grid->alloc.numRows);
    fprintf(stderr, "\ttotalBytes = %ld\n", grid->alloc.totalBytes);
    fprintf(stderr, "\treallocCount = %d\n", grid->alloc.reallocCount);

    fprintf(stderr, "halfGridRes = %.2f\n", grid->halfGridRes);       // check to see if this struct has been initialized
    fprintf(stderr, "minLat = %.2f\nmaxLat = %.2f\nminLon = %.2f\nmaxLon = %.2f\n", grid->minLat, grid->maxLat, grid->minLon, grid->maxLon);
    fprintf(stderr, "rows = %d\ncols = %d\n", grid->rows, grid->cols);
    fprintf(stderr, "rowSkip = %d\ncolSkip = %d\nreadRows = %d\nreadCols = %d\n", grid->rowSkip, grid->colSkip, grid->readRows, grid->readCols);
    fprintf(stderr, "initDone = %d\n", grid->initDone);       // check to see if this struct has been initialized
    fprintf(stderr, "isCentered = %d\n", grid->isCentered);       // check to see if this struct has been initialized
    fprintf(stderr, "carp = %d\n", grid->carp);       // check to see if this struct has been initialized
    fprintf(stderr, "doClip = %d\n", grid->doClip);       // check to see if this struct has been initialized
    fprintf(stderr, "allowPtsOutsideBounds = %d\n", grid->allowPtsOutsideBounds);       // check to see if this struct has been initialized
    fprintf(stderr, "clipUpperLeftLat = %.2f\n", grid->clipUpperLeftLat);       // check to see if this struct has been initialized
    fprintf(stderr, "clipUpperLeftLon = %.2f\n", grid->clipUpperLeftLon);       // check to see if this struct has been initialized
    fprintf(stderr, "clipLowerRightLat = %.2f\n", grid->clipLowerRightLat);       // check to see if this struct has been initialized
    fprintf(stderr, "clipLowerRightLon = %.2f\n", grid->clipLowerRightLon);       // check to see if this struct has been initialized
    fprintf(stderr, "byteOrder = %s\n", grid->byteOrder == little ? "little" : "big");       // check to see if this struct has been initialized
    fprintf(stderr, "swapByteOrder = %d\n", grid->swapByteOrder);       // check to see if this struct has been initialized
}

void dumpGrid(gridDataType *grid, int doCsv, int doPixCol, int quiet)
{
    int latpix, lonpix;
    char s = doCsv ? ',' : ' ';

    if(!quiet) {
        dumpHeader(grid);
    }

    double lat, lon;
    for(latpix=0; latpix<grid->readRows; latpix++) {
        for(lonpix=0; lonpix<grid->readCols; lonpix++) {
          lat = grid->data[latpix][lonpix].lat - grid->halfGridRes;
          lon = grid->data[latpix][lonpix].lon + grid->halfGridRes;
           if(doPixCol)
            fprintf(stdout, "%d%c%d%c%.2f%c%.2f%c%.3f\n", lonpix, s, latpix, s, lon, s, lat, s, grid->data[latpix][lonpix].value);
          else
            fprintf(stdout, "%.2f%c%.2f%c%.3f\n", lon, s, lat, s, grid->data[latpix][lonpix].value);
        }
    }
}

void closeGridFile(gridDataType *grid)
{
    if(grid != NULL && grid->fp != NULL)
        fclose(grid->fp);
}

void freeGridData(gridDataType *grid)
{
    int i;

    if(grid == NULL || grid->data == NULL)
        return;

    for(i=0;i<grid->readRows;i++)
        free(grid->data[i]);

    free(grid->data);

}

// you have to fill in the gridHeaderType *gridHead on these functions first

int gridWriteHeaderChar(FILE *fp, gridHeaderType *gridHead)
{
   strncpy(gridHead->version, "unCor.v1", 8);
   gridHead->bytes_element = sizeof(char);
   gridHead->data_type = Unsigned_Char;
   return(gridWriteHeader(fp, gridHead));
}

int gridWriteHeaderShort(FILE *fp, gridHeaderType *gridHead)
{
   strncpy(gridHead->version, "unCor.v1", 8);
   gridHead->bytes_element = sizeof(short);
   gridHead->data_type = Signed_Short;
   return(gridWriteHeader(fp, gridHead));
}

int gridWriteHeaderInteger(FILE *fp, gridHeaderType *gridHead)
{
   strncpy(gridHead->version, "unCor.v1", 8);
   gridHead->bytes_element = sizeof(int);
   gridHead->data_type = Signed_Int;
   return(gridWriteHeader(fp, gridHead));
}

int gridWriteHeaderFloat(FILE *fp, gridHeaderType *gridHead)
{
   strncpy(gridHead->version, "unCor.v1", 8);
   gridHead->bytes_element = sizeof(float);
   gridHead->data_type = Float;
   return(gridWriteHeader(fp, gridHead));
}


int gridWriteHeaderDouble(FILE *fp, gridHeaderType *gridHead)
{
   strncpy(gridHead->version, "unCor.v1", 8);
   gridHead->bytes_element = sizeof(double);
   gridHead->data_type = Double;
   return(gridWriteHeader(fp, gridHead));
}

int gridWriteHeader(FILE *fp, gridHeaderType *gridHead)
{
   unsigned char headBuff[GRID_HEADER_SIZE] = {0};

   // set archive time
   gridHead->arch_time = time(NULL);

   // set 64bit flag
   if(is64bitArch())
    gridHead->is64bitFlag = GRID_64BIT_FLAG_VAL;
   else
    gridHead->is64bitFlag = 0;

   // backfill header with nulls
   memcpy(headBuff, (unsigned char *)gridHead, sizeof(gridHeaderType));

   // 256 is a convention for header size, I suppose
   fwrite((void *) headBuff, GRID_HEADER_SIZE, 1 , fp);

#ifdef GRID_WRITE_HEAD_CHECK
   fprintf(stderr, "HED: boundaries=%.2f,%.2f,%.2f,%.2f | grid_size=%.1f | data_size=%d | data_type=%d | obs_time=%ld\n",
        gridHead->maxlat, gridHead->minlon, gridHead->minlat, gridHead->maxlon,
        gridHead->grid_size_deg, gridHead->bytes_element, gridHead->data_type, gridHead->obs_time);
#endif

   return True;
}

int gridWriteDataChar(FILE *fp, char **data, int rows, int cols)
{
 int latInd;

 // fprintf(stderr, "gridWRiteDataFloat(): writing %d rows x %d cols\n", rows, cols);

 for(latInd = 0; latInd < rows; latInd++) { // 0..180, for example
        if(fwrite(data[latInd],sizeof(char),cols,fp) != cols) {
          fprintf(stderr, "Can't write output.\n");
          return False;
        }
 }

 return True;
}

int gridWriteDataInteger(FILE *fp, int **data, int rows, int cols)
{
 int latInd;

 // fprintf(stderr, "gridWRiteDataFloat(): writing %d rows x %d cols\n", rows, cols);

 for(latInd = 0; latInd < rows; latInd++) { // 0..180, for example
        if(fwrite(data[latInd],sizeof(int),cols,fp) != cols) {
          fprintf(stderr, "Can't write output.\n");
          return False;
        }
 }

 return True;
}

int gridWriteDataShort(FILE *fp, short **data, int rows, int cols)
{
 int latInd;

 // fprintf(stderr, "gridWRiteDataFloat(): writing %d rows x %d cols\n", rows, cols);

 for(latInd = 0; latInd < rows; latInd++) { // 0..180, for example
        if(fwrite(data[latInd],sizeof(short),cols,fp) != cols) {
          fprintf(stderr, "Can't write output.\n");
          return False;
        }
 }

 return True;
}


int gridWriteDataFloat(FILE *fp, float **data, int rows, int cols)
{
 int latInd;

 // fprintf(stderr, "gridWRiteDataFloat(): writing %d rows x %d cols\n", rows, cols);

 for(latInd = 0; latInd < rows; latInd++) { // 0..180, for example
        if(fwrite(data[latInd],sizeof(float),cols,fp) != cols) {
          fprintf(stderr, "Can't write output.\n");
          return False;
        }
 }

 return True;
}

int gridWriteDataDouble(FILE *fp, double **data, int rows, int cols)
{
 int latInd;

 for(latInd = 0; latInd < rows; latInd++) { // 0..180, for example
        if(fwrite(data[latInd],sizeof(double),cols,fp) != cols) {
          fprintf(stderr, "Can't write output.\n");
          return False;
        }
 }

 return True;
}

int setGridSubwindow(gridDataType *grid, char *str)
{
 char *p=str,*q=str;
 // assume a string like 25.0,-70,17,-42
 // that is, upperLeftLat,upperLeftLon,lowerRtLat,lowerRtLon

 // upper left lat
 while(*p != ',' && *p) p++;
 *p = '\0';
 grid->clipUpperLeftLat = atof(q);

 // upper left lon
 q = p = p+1;
 while(*p != ',' && *p) p++;
 *p = '\0';
 grid->clipUpperLeftLon = atof(q);

 // lower right lat
 q = p = p+1;
 while(*p != ',' && *p) p++;
 *p = '\0';
 grid->clipLowerRightLat = atof(q);

 // lower right lon
 q = p = p+1;
 while(*p != ',' && *p) p++;
 *p = '\0';
 grid->clipLowerRightLon = atof(q);

 grid->doClip = True;

 // now do checks on sanity
 if(grid->clipUpperLeftLat < grid->clipLowerRightLat) {
     fprintf(stderr, "Got upper left lat [%.2f] < lower right lat [%.2f]\n",
        grid->clipUpperLeftLat, grid->clipLowerRightLat);
     return False;
 }

 if(grid->clipUpperLeftLon > grid->clipLowerRightLon) {
     fprintf(stderr, "Got upper left lon [%.2f] > lower right lon [%.2f]\n",
        grid->clipUpperLeftLon, grid->clipLowerRightLon);
     return False;
 }

 return True;
}

void convertArchiveSnowGridFile(gridDataType *grid)
{
    int latpix, lonpix;

    for(latpix=0; latpix<grid->readRows; latpix++) {
        for(lonpix=0; lonpix<grid->readCols; lonpix++) {
            switch((int) grid->data[latpix][lonpix].value) {
                // mapping between what the new model expects and what the old snow data provides
                case   0  : grid->data[latpix][lonpix].value = 0.0; break;
                case  50  : grid->data[latpix][lonpix].value = 2.0; break;  // land
                case 100  : grid->data[latpix][lonpix].value = 5.0; break;  // clouds => nosnow?
                case 250  : grid->data[latpix][lonpix].value = 4.0; break;  // snow
                default   : fprintf(stderr, "Got snow unknown snow code: %.0f\n", grid->data[latpix][lonpix].value);
            }
        }
    }
}

/* x = ifloor( 5.7) =  5
   x = ifloor(-5.7) = -6
 */
int ifloor(double expr)
{
    int trunc = (int) expr;
    if(expr > 0) 
        return trunc;
    else
        return trunc - 1;
}

/* x = iceil( 5.7) =  6
   x = iceil(-5.7) = -5
 */
int iceil(double expr)
{
    int trunc = (int) expr;
    if(expr > 0)
        return trunc + 1;
    else
        return trunc;
}
