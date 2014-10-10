#ifndef GRIDDED_H
 
#define GRIDDED_H

#include <time.h>
#include <stdio.h>
#include "carp.h"

#ifndef True
#define True        1
#define False        0
#define        True        1
#define False        0
#endif
// new version, by js  5/12/2006

// typedefs for gridded satellite files
// replaces the old sat-grid.h header file

// static const char        *versions[] = {"Grid0001", NULL};


typedef enum {little, big} endianType;

typedef enum {
    Unsigned_Char,
    Signed_Char,
    Unsigned_Short,
    Signed_Short,
    Unsigned_Int,
    Signed_Int,
    Unsigned_Long,
    Signed_Long,
    Float,
    Double
} data_t;

typedef enum {
    biLinear, nearestNeighbor
} resampleOptions;

#define GRID_HEADER_SIZE  256     // time to standardize this
#define GRID_64BIT_FLAG_VAL 0xDEADBEEF  // used to mark grid files that are from 64bit archs

// much as I hate to do this, here is a special purpose grid header type
// made necessary by the "archived" snow grid files for the old US satellite
// model code
typedef struct { 
                int ullon,
                    ullat,
                    alen,
                    nc;
} archSnowGridHeaderType;

typedef struct {
    char        version[8];
    time_t      obs_time, arch_time;
    double      grid_size_deg, minlat, maxlat, minlon, maxlon;
    data_t      data_type;
    int         bytes_element;
    char        description[64];
    double      is64bitFlag;
} gridHeaderType;

typedef struct {
    double        lat, lon, value;
} grid_t;

// keep track of how much memory has been allocated
// for the grid file data (stored in **data) so that
// a grid struct can be recycled from one grid file 
// open/processing call to the next without the need 
// to free/alloc over and over again.  In most cases
// the grid memory requirements will be identical from
// on grid file call to the next.

typedef struct {
    long    bytesPerRow;
    long    numRows;
    long    totalBytes;
    int     reallocCount;
} memType;

// this is the top level struct for grid processing applications

#define GRID_INIT_KEY  (int) 0xDEADBEEF
#define FILENAME_SIZE 2048

typedef struct {
    char        fileName[FILENAME_SIZE];
    FILE        *fp;
    gridHeaderType    hed;
    grid_t      **data;
    int         rows, cols, gridHeaderSize;
    int         rowSkip, colSkip, readRows, readCols;
    double        minLat, maxLat, minLon, maxLon;
    char        isCentered, isSynthetic;  // not used 
    double      halfGridRes;
    struct tm   tm;            // holds results from gmtime(obs_time) -- handy in satModel
    memType     alloc;          // struct carries info about its memory allocation
    int         initDone;       // check to see if this struct has been initialized
                                // set to "secret" value GRID_INIT_KEY if soe
//    int         altered;       // flag to indicate that the grid has been altered
    carpType    carp;
    // clipping parameters
    char        doClip, allowPtsOutsideBounds, doResampling;
    double      clipUpperLeftLat, clipUpperLeftLon, clipLowerRightLat, clipLowerRightLon;
    endianType  byteOrder;
    char        swapByteOrder;
    double        pixelScalingFactor;
    resampleOptions resampleType;
    double      resampleRes;
    void        *resampledGrid;
} gridDataType;

int     openReadGridfile(gridDataType *grid);
int     openReadHeadGridfile(gridDataType *grid);
int     checkMemory(gridDataType *grid);
void    initGrid(gridDataType *grid);
int     readGrid(gridDataType *grid);
int     latlon2GridPix(gridDataType *grid, double lat, double lon, int *latpix, int *lonpix);
void    dumpGrid(gridDataType *grid, int doCsv, int doPixCol, int quiet);
void    dumpHeader(gridDataType *grid);
void         dumpHeaderFull(gridDataType *grid);
void         freeGridData(gridDataType *grid);
int     gridWriteHeaderChar(FILE *fp, gridHeaderType *gridHead);
int     gridWriteDataChar(FILE *fp, char **data, int rows, int cols);
int     gridWriteHeaderInteger(FILE *fp, gridHeaderType *gridHead);
int     gridWriteDataInteger(FILE *fp, int **data, int rows, int cols);
int     gridWriteHeaderFloat(FILE *fp, gridHeaderType *gridHead);
int     gridWriteDataFloat(FILE *fp, float **data, int rows, int cols);
int     gridWriteDataDouble(FILE *fp, double **data, int rows, int cols);
int     gridWriteHeaderDouble(FILE *fp, gridHeaderType *gridHead);
int     gridWriteHeader(FILE *fp, gridHeaderType *gridHead);
int     setGridSubwindow(gridDataType *grid, char *str);
void    copyGridClipData(gridDataType *toGrid, gridDataType *fromGrid);
void    setHeadToClippedWindow(gridDataType *grid);
int     openReadArchiveSnowGridfile(gridDataType *grid);  // bastardization of the concept
void    convertArchiveSnowGridFile(gridDataType *grid); // more bastardization
void    closeGridFile(gridDataType *grid);
int     checkHeaderSanity(gridDataType *grid);
void    swapHeaderBytes(gridDataType *grid);
void    swapByteOrderLine(unsigned char *line, int length, int size);
void    read32bitHeader(gridDataType *grid);
int     isBigEndian(void);
int     is64bitArch(void);
void    byteSwap(unsigned char * b, int n);
double  gridGetScaledData(gridDataType *grid, int latpix, int lonpix);
int     resampleGrid(gridDataType *grid);
int     openReadGridfile2(gridDataType *grid);
double  interpBilinear(gridDataType *grid, grid_t *hiRes);
double  interpNearNeighbor(gridDataType *grid, grid_t *hiRes);
int     gridWriteHeaderShort(FILE *fp, gridHeaderType *gridHead);
int     gridWriteDataShort(FILE *fp, short **data, int rows, int cols);


#endif /* GRIDDED_H */
