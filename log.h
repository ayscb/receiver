/*************************************************************************
	> File Name: log.h
	> Author: 
	> Mail: 
	> Created Time: Tue 19 May 2015 02:14:28 AM PDT
 ************************************************************************/

#ifndef _LOG_H
#define _LOG_H

#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include <time.h>
#include "stdarg.h"
#include <unistd.h>
 
#define MAXLEN (2048)
#define MAXFILEPATH (512)
#define MAXFILENAME (50)
 
 // for program
typedef enum{
    NONE=0,
    ERROR=1,
    WARN=2,
    INFO=4,
    DEBUG=8
}LOGLEVEL;

 
typedef struct log{
    char logtime[20];
    char filepath[MAXFILEPATH];
    FILE *logfile;
}LOG;
 
typedef struct logseting{
    char filepath[MAXFILEPATH];
    unsigned int maxfilelen;
    unsigned char loglevel;
}LOGSET;
 
int LogWrite(unsigned char loglevel,char *fromat,...);

void setLogConfPath( char* path ); // path==NULL -->current dir
void logDecollator();
#endif
