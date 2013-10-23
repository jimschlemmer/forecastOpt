/* 
 * File:   ioUtils.h
 * Author: root
 *
 * Created on August 10, 2006, 3:12 PM
 */

#ifndef _ioUtils_H
#define _ioUtils_H

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <dirent.h>
#include "gridDateTime.h"

int directoryExists(char *path);
int fileExists(char *path);
char **readDirInOrder(char *path, char *pattern, int *n);
char **readDirInOrder2(char *path, char *pattern1, char *pattern2, int *numFiles);
char  **readDirInOrderLimits(char *path, dateTimeType *start, dateTimeType *end, char *pattern, int *numFiles);
int isInDateRange(char *path, dateTimeType *start, dateTimeType *end);
int fileSize(FILE *fp);
int split(char *string, char *fields[], int nfields, char *sep);
void printMemUse(char *str);
int cmpstringp (const void *p1, const void *p2);
int iround(double expr);
int checkExists(char *fileName);
void segvhandler(int sig);

#endif        /* _ioUtils_H */

