
/*
 * File:   ioUtils.c
 * Author: root
 *
 * Created on August 10, 2006, 3:11 PM
 */

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "ioUtils.h"
#include <execinfo.h>

/* Compare two strings for qsort().  */
int cmpstringp (const void *p1, const void *p2)
{
  /* The arguments to this function are "pointers to
     pointers to char", but strcmp() arguments are "pointers
     to char", hence the following cast plus dereference */

  return strcmp(*(char **) p1, *(char **) p2);
}

int fileSize(FILE *fp)
{
    struct stat buf;

    fstat(fileno(fp), &buf);

    return (int) buf.st_size;
}

int fileExists(char *path) 
{
    struct stat buf;
    
    if(stat((const char *) path, &buf) != 0)
        return False;

    return True;

/*
    FILE *fp;
    char result = True;

    if((fp = fopen(path, "r")) == NULL) 
        result = False;
    else 
        fclose(fp);

    return result;
*/
}

int directoryExists(char *path) 
{
     DIR *dirPtr;
     int result = TRUE;

     if((dirPtr = opendir(path)) == NULL) 
        result = FALSE;
     else 
        closedir(dirPtr);

     return result;
}

char  **readDirInOrder(char *path, char *pattern, int *numFiles)
{
     DIR *dirPtr;
     struct dirent *gridDirEnt;
     int n=0;
     int listLen = 4096;
     char **fileNames;

    if((dirPtr = opendir(path)) == NULL) {
                    fprintf(stderr, "Couldn't open grid directory %s.\n", path);
                    return NULL;
    }

    if((fileNames = (char **) malloc(listLen * sizeof(char *))) == NULL) {
         fprintf(stderr, "readDirInOrder(): memory error\n");  // initially 1K files
         return NULL;
    }

    //for(i=0;i<listLen;i++) fileNames[i] = (char *) malloc(sizeof(char *));

    while((gridDirEnt = readdir(dirPtr)) != NULL) {

             // ignore files that don't end in .grid
             if(strstr(gridDirEnt->d_name, pattern)) {
                 if(n == listLen-1) {
                    listLen *= 2;
                    if((fileNames = (char **) realloc(fileNames, listLen * sizeof(char *))) == NULL) {   // double size
                        fprintf(stderr, "readDirInOrder(): memory error\n");
                        return NULL;
                    }
                 }
                 fileNames[n] = strdup(gridDirEnt->d_name);
                 n++;
             }
     }

     // qsort(fileNames);
     if (n > 1)
          qsort (fileNames, n, sizeof (char *), cmpstringp);

     *numFiles = n;
     return fileNames;
 }

// kinda klugey, but we allow two patterns here to take the place of a regex
// as in, pattern1 = "afg-pak.2004" pattern2 = "global.grid" instead of
// "afg-pak.2004*global.grid"

char  **readDirInOrder2(char *path, char *pattern1, char *pattern2, int *numFiles)
 {
     DIR *dirPtr;
     struct dirent *gridDirEnt;
     int n=0;
     int listLen = 4096;
     char **fileNames;

    if((dirPtr = opendir(path)) == NULL) {
                    fprintf(stderr, "Couldn't open grid directory %s.\n", path);
                    return NULL;
    }

    if((fileNames = (char **) malloc(listLen * sizeof(char *))) == NULL) {
         fprintf(stderr, "readDirInOrder(): memory error\n");  // initially 1K files
         return NULL;
    }

    //for(i=0;i<listLen;i++) fileNames[i] = (char *) malloc(sizeof(char *));

    while((gridDirEnt = readdir(dirPtr)) != NULL) {

             // ignore files that don't end in .grid
             if(strstr(gridDirEnt->d_name, pattern1) && strstr(gridDirEnt->d_name, pattern2)) {
                 if(n == listLen-1) {
                    listLen *= 2;
                    if((fileNames = (char **) realloc(fileNames, listLen * sizeof(char *))) == NULL) {   // double size
                        fprintf(stderr, "readDirInOrder(): memory error\n");
                        return NULL;
                    }
                 }
                 fileNames[n] = strdup(gridDirEnt->d_name);
                 n++;
             }
     }

     // qsort(fileNames);
     if (n > 1)
          qsort (fileNames, n, sizeof (char *), cmpstringp);

     *numFiles = n;
     return fileNames;
 }

char  **readDirInOrderLimits(char *path, dateTimeType *start, dateTimeType *end, char *pattern, int *numFiles)
 {
     DIR *dirPtr;
     struct dirent *gridDirEnt;
     int n=0;
     int listLen = 4096;
     char **fileNames;

    if((dirPtr = opendir(path)) == NULL) {
                    fprintf(stderr, "Couldn't open grid directory %s.\n", path);
                    return NULL;
    }

    if((fileNames = (char **) malloc(listLen * sizeof(char *))) == NULL) {
         fprintf(stderr, "readDirInOrderLimits(): memory error\n");  // initially 1K files
         return NULL;
    }

    //for(i=0;i<listLen;i++) fileNames[i] = (char *) malloc(sizeof(char *));

    while((gridDirEnt = readdir(dirPtr)) != NULL) {

             // ignore files that don't include pattern
             if(strstr(gridDirEnt->d_name, pattern)) {
                 if(isInDateRange(gridDirEnt->d_name, start, end)) {
                     // the code to add a file to the list
                     if(n == listLen-1) {
                        listLen *= 2;
                        if((fileNames = (char **) realloc(fileNames, listLen * sizeof(char *))) == NULL) {   // double size
                            fprintf(stderr, "readDirInOrderLimits(): memory error\n");
                            return NULL;
                        }
                     }
                     fileNames[n] = strdup(gridDirEnt->d_name);
                     n++;
                 }
             }
     }

     // qsort(fileNames);
     if (n > 1)
          qsort (fileNames, n, sizeof (char *), cmpstringp);

     *numFiles = n;
     return fileNames;
 }

// quick 'n dirty file filter based on fileName (not real robust) and a set
// of begin,end dateTimeType's
int isInDateRange(char *fileName, dateTimeType *start, dateTimeType *end)
{
    char temp[512];
    int numFields;
    char *fields[64];
    dateTimeType dt;

    dt.hour = dt.min = 0;
  // Expecting fileName like us-west.20060421.*

    strcpy(temp, fileName);
    numFields = split(temp, fields, 64, ".");  /* split line */
    sscanf(fields[1], "%04d%02d%02d", &dt.year, &dt.month, &dt.day);
    sscanf(fields[2], "%02d%02d", &dt.hour, &dt.min);

    if(!dateTimeSanityCheck(&dt)) {
            return(False);
    }

    setObsTime(&dt);

    return(dt.obs_time >= start->obs_time && dt.obs_time <= end->obs_time);
}

// doesn't do negatives
int iround(double expr)
{
    int trunc = (int) expr;
    double diff = expr - trunc;
    if(diff > 0.50001)  // this will force .5 to round down, which is important sometimes
        return(trunc + 1);
    else
        return(trunc);
}


/*
 *
 */
//#define DOMALLINFO
void printMemUse(char *str)
{
#ifdef DOMALLINFO
    struct mallinfo *mi;

    mi = mallinfo();
    fprintf(stderr, "%s : Mem use : %d\n", str, mi->arena);
#endif
}


/*
 * This came off the net, from somewhere, long ago.
 *
 * split - divide a string into fields, like awk split()
 *
 * char *string;
 * char *fields[];                list is not NULL-terminated
 * int nfields;                        number of entries available in fields[]
 * char *sep;                        "" white, "c" single char, "ab" [ab]+
 */
int split(char *string, char *fields[], int nfields, char *sep)
{
    char           *p = string, c, sepc = sep[0], sepc2, **fp = fields, *sepp;
    int             fn, trimtrail;

    /* white space */
    if (sepc == '\0') {
        while ((c = *p++) == ' ' || c == '\t')
            continue;
        p--;
        trimtrail = 1;
        sep = " \t";                /* note, code below knows this is 2 long */
        sepc = ' ';
    } else
        trimtrail = 0;
    sepc2 = sep[1];                /* now we can safely pick this up */

    /* catch empties */
    if (*p == '\0')
        return (0);

    /* single separator */
    if (sepc2 == '\0') {
        fn = nfields;
        for (;;) {
            *fp++ = p;
            fn--;
            if (fn == 0)
                break;
            while ((c = *p++) != sepc)
                if (c == '\0')
                    return (nfields - fn);
            *(p - 1) = '\0';
        }
        /* we have overflowed the fields vector -- just count them */
        fn = nfields;
        for (;;) {
            while ((c = *p++) != sepc)
                if (c == '\0')
                    return (fn);
            fn++;
        }
        /* not reached */
    }


    /* two separators */
    if (sep[2] == '\0') {
        fn = nfields;
        for (;;) {
            *fp++ = p;
            fn--;
            while ((c = *p++) != sepc && c != sepc2)
                if (c == '\0') {
                    if (trimtrail && **(fp - 1) == '\0')
                        fn++;
                    return (nfields - fn);
                }
            if (fn == 0)
                break;
            *(p - 1) = '\0';
            while ((c = *p++) == sepc || c == sepc2)
                continue;
            p--;
        }
        /* we have overflowed the fields vector -- just count them */
        fn = nfields;
        while (c != '\0') {
            while ((c = *p++) == sepc || c == sepc2)
                continue;
            p--;
            fn++;
            while ((c = *p++) != '\0' && c != sepc && c != sepc2)
                continue;
        }
        /* might have to trim trailing white space */
        if (trimtrail) {
            p--;
            while ((c = *--p) == sepc || c == sepc2)
                continue;
            p++;
            if (*p != '\0') {
                if (fn == nfields + 1)
                    *p = '\0';
                fn--;
            }
        }
        return (fn);
    }


    /* n separators */
    fn = 0;
    for (;;) {
        if (fn < nfields)
            *fp++ = p;
        fn++;
        for (;;) {
            c = *p++;
            if (c == '\0')
                return (fn);
            sepp = sep;
            while ((sepc = *sepp++) != '\0' && sepc != c)
                continue;
            if (sepc != '\0')        /* it was a separator */
                break;
        }
        if (fn < nfields)
            *(p - 1) = '\0';
        for (;;) {
            c = *p++;
            sepp = sep;
            while ((sepc = *sepp++) != '\0' && sepc != c)
                continue;
            if (sepc == '\0')        /* it wasn't a separator */
                break;
        }
        p--;
    }

    /* not reached */
}

int checkExists(char *fileName)
{
  FILE *fp;

  if((fp = fopen(fileName, "r")) == NULL)     {
      return False;
  }

  fclose(fp);
  return True;
}


//
// Handle segmentation violation
// Print stack backtrace and exit with failure code
//
void segvhandler(int sig) {
  void *array[200];
  size_t size;

  // get void*'s for all entries on the stack
  size = backtrace(array, 200);

  // print out all the frames to stderr
  fprintf(stderr, "\n\n----------------------------------------\n\n");
  fprintf(stderr, "Segmentation Violation: signal = %d:\n\n", sig);
  backtrace_symbols_fd(array, size, 2);

  exit(1);
}
