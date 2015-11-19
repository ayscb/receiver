/*************************************************************************
        > File Name: log.h
        > Author: 
        > Mail: 
        > Created Time: Wed 20 May 2015 08:06:45 AM PDT
 ************************************************************************/

#ifndef LOG_H
#define	LOG_H

#define MAXLEN (2048)
#define MAXFILEPATH (512)
#define MAXFILENAME (50)
 
 // for program
typedef enum{ NONE=0, ERROR=1, WARN=2, INFO=4, DEBUG=8 }LOGLEVEL;

void initLog(const char* configurationFile);  // path==NULL -->current dir
void LogWrite(const unsigned char loglevel,const char *format, ...);
void logDecollator();

#endif	/* LOG_H */

