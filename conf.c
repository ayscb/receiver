/*************************************************************************
	> File Name: conf.c
	> Author: 
	> Mail: 
	> Created Time: Tue 19 May 2015 11:04:34 PM PDT
 ************************************************************************/
#include "conf.h"
#include "log.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>		//isblank()
#include <errno.h>

// filePath[0] ---> netflow.conf   NETFLOW_CONF_HOME
// filePath[1] ---> master
static char filePath[2][100];

//***************************************************

static void getfilePath();
static int isSkipRow(char* line );
static int getRealRowsNum( FILE* fp );
static void defaultValue();
static void readConfFile();
static void readMasterFile();

//***************************************************
//		Global function
//***************************************************
void configure(){
    getfilePath();
    readConfFile();
    readMasterFile();
    logDecollator();
}

//***************************************************
//		personal function
//***************************************************
static void getfilePath(){
    // get netflow.conf file
    char confFile[512]={0x0};
    char* file = getenv("NETFLOW_CONF_HOME");
    if( file == NULL ){
            getcwd(confFile, sizeof(confFile));
            strcat(confFile,"/conf");
    }else{
            strncpy(confFile, file, sizeof(confFile));
    }

    // get netflow.conf
    strncpy(filePath[0], confFile, sizeof(filePath[0]));      
    strcat(filePath[0],"/netflow.conf");

    // get master file
    strncpy(filePath[1], confFile, sizeof(filePath[1])); 
    strcat(filePath[1],"/master");

    // get log.conf
    strcat(confFile,"/log.conf");
    setLogConfPath( confFile);
}

static int isSkipRow(char* line ){
    char* p = line;
    p = del_left_trim(p);

    // skip ' '、 \t 、 \r 、 \n 、 \v 、 \f 
    if( isspace(p[0]) )
            return 1;
    if( p[0] == '#')
            return 1;
    return 0;
}

static int getRealRowsNum( FILE* fp ){
    int rows = 0;
    long filePos = ftell(fp);
    char tmp[200];
    while(fgets( tmp, 200, fp )!= NULL ){
        if( isSkipRow(tmp))
                continue;
        rows ++;
        memset( tmp, 0, 200 );
    }
    fseek(fp, filePos, SEEK_SET );
    return rows;
}

static void defaultValue(){
    if(netflowConf.singleWaitSecond == 0)
            netflowConf.singleWaitSecond = 40;		
    if(netflowConf.totalMaxTryNum==0)
            netflowConf.totalMaxTryNum = 5;	
    if(netflowConf.receiverWaitSecond==0)
            netflowConf.receiverWaitSecond = 20;
    if(netflowConf.countIntervalNum==0)
            netflowConf.countIntervalNum = 100000;
    if(netflowConf.showCount==0)
            netflowConf.showCount = 1;
}

static void setVariable(char* key, char* value){
    char* _key = key;
    _key = del_both_trim(_key);
    char* _value = value;
    _value = del_both_trim(_value);

    if(strcasecmp(key,"singleWaitSecond")==0){
        netflowConf.singleWaitSecond = atoi(value);
    }else if(strcasecmp(key,"totalMaxTryNum")==0){
        netflowConf.totalMaxTryNum = atoi(value);
    }else if(strcasecmp(key,"receiverWaitSecond")==0){
        netflowConf.receiverWaitSecond = atoi(value);
    }else if(strcasecmp(key,"countIntervalNum")==0){
        netflowConf.countIntervalNum = atol(value);
    }else if(strcasecmp(key,"showCount")==0){
        char * val = del_both_trim(value);
        if(strcasecmp(val,"true")==0){
            netflowConf.showCount =1;
        }else{
            netflowConf.showCount =-1;
        }
    }

    // for test
    if(strcasecmp(key,"testLoadData")==0){
        strcpy(netflowtest.testLoadData,value);
    }else if(strcasecmp(key,"testLoadTemp")==0){
        strcpy(netflowtest.testLoadTemp,value);
    }else if(strcasecmp(key,"testLoadMix")==0){
        strcpy(netflowtest.testLoadMix,value);
    }else if(strcasecmp(key,"rate")==0){
        netflowtest.rate = atoi(value);
    }else if(strcasecmp(key,"durationTime")==0){
        netflowtest.durationTime = atoi(value);
    }
}

static void readConfFile(){
    //LogWrite(INFO,"Try to read conf file %s",filePath[0]);

    //load default value
    defaultValue();

    // if file does not exist
    if( !access(filePath[0],F_OK)==0) 
        return ;

    // file exist
    FILE* fp =fopen(filePath[0],"r");
    if( fp == NULL ){
        //LogWrite(ERROR,"Open %s Fail! %s",strerror(errno));
        exit (-1) ;
    }

    // read data
    char line[100]={0}; 
    while(fgets(line, sizeof(line), fp)!= NULL){
        if( isSkipRow(line) )
            continue; 

        char* pos = strchr(line, '=');
        if( pos == NULL ) continue;

        char* key = line;
        char* value = pos+1;
        *pos = '\0';
        value[strlen(value)-1] = '\0';    // remove '\n'

        setVariable(key,value);
        memset(line, 0, 100);
    }
    fclose(fp);
    //LogWrite(INFO,"Read %s file finished!",filePath[0]);
}

static void readMasterFile(){
    //LogWrite(INFO,"Try to read master file %s",filePath[1]);
    if( !access(filePath[1],F_OK)==0) {
        //LogWrite(ERROR,"Read file %s %s!",filePath[1], strerror(errno));
        return;
    }

    FILE* fp =fopen(filePath[1],"r");
    if( fp == NULL ){
        //LogWrite(ERROR,"Open %s Fail! %s",filePath[1],strerror(errno));
        exit (-1) ;
    }
    
    int rows = getRealRowsNum(fp);
    if( rows == 0 ){
        //LogWrite(ERROR,"No master address ! Please check !\n");
        exit (-1) ;
    }
    //LogWrite(INFO, "Total master's number is: %d", rows);

    // malloc masterList
    masterList.masterIP = (struct sockaddr_in *)malloc(rows * sizeof(struct sockaddr_in));
    masterList.masterNum = 0;

    // read data
    char line[100]={0}; 
    while(fgets(line, sizeof(line), fp)!= NULL){
        if( isSkipRow(line) ){
            continue;  
        }
     
        char* pos = strchr(line, ':');
        if( pos == NULL ) continue;

        int port = atoi( pos + 1);
        *pos='\0';
        char *ip = line;
        ip = del_both_trim(ip);
        
        setAddress( masterList.masterIP + masterList.masterNum, ip, port );
        //LogWrite(INFO, "Master number %d : %s:%d",masterList.masterNum, ip, port);
        masterList.masterNum ++ ;
        
        memset(line, 0, 100);
    }
    fclose(fp);
    //LogWrite(INFO,"Read %s file finished!",filePath[1]);
}


