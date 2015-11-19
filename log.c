
/*************************************************************************
	> File Name: log.c
	> Author: 
	> Mail: 
	> Created Time: Tue 19 May 2015 02:14:55 AM PDT
 ************************************************************************/
#include "log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>       //mkdir
#include <sys/types.h> 
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>


typedef enum LOGWRITE{NO_CONSOLE_FILE, CONSOLE_ONLY, FILE_ONLY, CONSOLE_FILE} writeType;

typedef struct log{
    char fileName[20];              // current log's time
    char basepath[MAXFILEPATH];     // current log's file path
    FILE *fp;                       // current log file point
}LOG;
 
typedef struct logseting{
    // log configuration file's path
    char confFile[MAXFILEPATH];     
    unsigned int maxfilelen;        // the max limit size of echo log file
    unsigned int curfileIdx;        // current log file's suffix 
    unsigned char loglevelCode;     // current log's level(hot code))
    writeType method;
}LOGSET;


//**********************************************************
//      Persional Variabe Definition
//**********************************************************
LOGSET logsetting;
LOG logs;

//**********************************************************
//      Persional Function Statement
//**********************************************************
static char* codeToStr( const unsigned char code );
static unsigned char getLevelcode(char* value);
static writeType getWriterType(char* value);
static void setConfigurePath(const char* path);
static void loadDefaultSetting();
static void setVariable(char* key, char* value);
static void initLogSetting(void);
static void setCurretnFile(void);
static char* del_both_trim(char * str);
static int isInvalidRow(char* row);

 //**********************************************************
//      Global Function Implement
//**********************************************************

void initLog(const char* configurationFile){
    
    // set configuration log file path
    setConfigurePath(configurationFile);
    initLogSetting();
    
    // mkdir log document
    if( opendir(logs.basepath) == NULL ){
        mkdir(logs.basepath, S_IRUSR | S_IWUSR | S_IXUSR);
    }
}

void LogWrite(const unsigned char loglevel,const char *format, ...){
    
    //
    if(logsetting.method == NO_CONSOLE_FILE) return ;
    if(logsetting.loglevelCode == NONE)    return ;
    if((loglevel & logsetting.loglevelCode ) != loglevel)   return ;
    
    setCurretnFile();
    
    // get current time
    time_t timer=time(NULL);
    char curTime[20];
    strftime(curTime, 20, "%Y-%m-%d %H:%M:%S", localtime(&timer));
     
    if(logsetting.method == FILE_ONLY || logsetting.method == CONSOLE_FILE){
        fprintf(logs.fp,"[%s] [%s]: ",codeToStr(loglevel), curTime);
        va_list argList;
        va_start(argList, format);
        vfprintf(logs.fp, format, argList);
        va_end(argList);      
        fflush(logs.fp);
    }
    
    if(logsetting.method == CONSOLE_ONLY || logsetting.method == CONSOLE_FILE){
        printf("[%s] [%s]: ",codeToStr(loglevel), curTime);
        va_list argList;
        va_start(argList, format);
        vprintf(format, argList);
        va_end(argList);
    } 
}

void logDecollator(){
    LogWrite(INFO,"=================================================");
}

 //**********************************************************
//      Persional Function Implement
//**********************************************************

static char* codeToStr( const unsigned char code ){
    switch( code ){
        case NONE:  return "NONE"; 
        case ERROR: return "ERROR"; 
        case WARN:  return "WARE"; 
        case INFO:  return "INFO"; 
        case DEBUG: return "DEBUG"; 
        default: return "INFO";
    }
}

/*
 * get the log level from configuration file
 * 
 * return: the combined level code
 */
static unsigned char getLevelcode(char* value){
    unsigned char code=255;
    if(strcasecmp("NONE",value)==0)         code = 0x0;   // 0000 0000 -.NONE 0
    else if(strcasecmp ("ERROR",value)==0)  code = 0x01;  // 0000 0001 -.ERROR 1
    else if(strcasecmp ("WARN",value)==0)   code = 0x03;  // 0000 0011 -.WARN 10 || ERROR 1
    else if(strcasecmp ("INFO",value)==0)   code = 0x07;  // 0000 0111 -.INFO 100 || WARN 10 || ERROR 1
    else if(strcasecmp ("DEBUG",value)==0)  code = 0x15;  // 0000 1111 -.DEBUG 1000 || INFO 100 || WARN 10 || ERROR 1
    else if(strcasecmp ("console",value)==0) code = 0x80; // 1000 0000 -.printf
    else code=7;
    return code;
} 

static writeType getWriterType(char* value){
    if(strcasecmp("NONE",value)==0)          return NO_CONSOLE_FILE;  
    else if(strcasecmp ("CONSOLE",value)==0) return CONSOLE_ONLY;  
    else if(strcasecmp ("FILE",value)==0)    return FILE_ONLY;  
    else if(strcasecmp ("ALL",value)==0)     return CONSOLE_FILE;
    else return FILE_ONLY;
}

 // home/ss/conf/log.conf
static void setConfigurePath(const char* path ){
    if(path != NULL){
        strcpy(logsetting.confFile, path);
    }else{
        getcwd(logsetting.confFile, sizeof(logsetting.confFile));
        strcat(logsetting.confFile,"/conf/log.properties");
    }
}

static void loadDefaultSetting(){
    logsetting.curfileIdx = 0;
    logsetting.maxfilelen = 10 * 1024 * 1024;      // 10MB
    logsetting.loglevelCode = getLevelcode("INFO");
    logsetting.method = CONSOLE_ONLY;

    getcwd(logs.basepath, sizeof(logs.basepath));
    strcat(logs.basepath, "/log/");
}

static void setVariable(char* key, char* value){
    char* _key = key;
    char* _value = value;
    
    _key = del_both_trim(_key);   
    _value = del_both_trim(_value);

    if(strcasecmp(key,"logpath")==0){
        strcpy(logs.basepath, _value);
        strcat(logs.basepath,"/");
    }else if(strcasecmp(key,"level")==0){
        logsetting.loglevelCode = getLevelcode(_value);
    }else if(strcasecmp(key,"method")==0){
        logsetting.method = getWriterType(_value);
    }else if(strcasecmp(key,"maxfilelen")==0){
        logsetting.maxfilelen = atoi(_value);
    }
}

static void initLogSetting(void){
    loadDefaultSetting(&logsetting);
     
    FILE *fp = fopen(logsetting.confFile,"r");
    if(fp == NULL)  {
        printf("Can not open log configuration file %s, "
                "will load default setting value.\n", logsetting.confFile);
        return; 
    }
        
    char line[100]={0}; 
     while(fgets(line, sizeof(line), fp) != NULL){
        if(isInvalidRow(line))   continue; 

        char* equalFlag = strchr(line, '=');
        *equalFlag = '\0';
        
        char* key = line;
        char* value =  equalFlag +1;       
        value[strlen(value)-1] = '\0';    // remove '\n'

        setVariable(key, value);
        memset(line, 0, 100);
    }
}

static void setCurretnFile(void){
    char logFileStr[50];
    time_t timer=time(NULL);
    strftime(logFileStr,11,"%Y-%m-%d",localtime(&timer));
    strcat(logFileStr,".log");
    
    if(logs.fp == NULL || strcasecmp (logFileStr, logs.fileName)!= 0){
        char *logFile = 
            (char*)malloc(strlen(logs.basepath)+strlen(logFileStr)+1);  
        if (logFile == NULL) return;  
  
        strcpy(logFile, logs.basepath);  
        strcat(logFile, logFileStr);  
        if(logs.fp != NULL) fclose(logs.fp);
        
        logs.fp = fopen(logFile,"a+");
        if(logs.fp == NULL)  {
            printf("Can not open current logFile %s.\n ", logFile);
        }
        strcpy(logs.fileName, logFileStr);
        free(logFile);
    }
}
 
/*   delete left&&rigth blank space   */
static char * del_both_trim(char * str) {   
    char *pp = str;
    for (;*pp != '\0' && isblank(*pp) ; ++pp);  // get left consilient position
     
    char *p = pp + strlen(pp) - 1;
    for (; p >= pp && isblank(*p); --p);
    *(++p) = '\0';
    return pp;
}

/*
 * check the row is a correct row
 * 
 * return:
 *  1: true
 *  0: fasle
 */
static int isInvalidRow(char* row){
    char* p = row;
    for (;*p != '\0' && isblank(*p) ; ++p);

    // skip ' '、 \t 、 \r 、 \n 、 \v 、 \f 
    if( isspace(p[0]) ) return 1;
    if( p[0] == '#') return 1;
    
    // skip the row which does not exist "="
    char* equalFlag = strchr(row, '=');
    if( equalFlag == NULL ) 
        return 1;
    else
        return 0;
}


