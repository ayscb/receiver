/*************************************************************************
	> File Name: log.c
	> Author: 
	> Mail: 
	> Created Time: Tue 19 May 2015 02:14:55 AM PDT
 ************************************************************************/

#include "log.h"
#define MAXLEVELNUM (3)

LOGSET logsetting;
LOG loging;
 
static char confFile[512]={0x0};

const static char LogLevelText[5][8]={"NONE","ERROR","WARN","INFO","DEBUG"};

static char * getdate(char *date);
 
static int menuToidx( int code ){
    switch( code ){
        case 0: return 0; 
        case 1: return 1; 
        case 2: return 2; 
        case 4: return 3; 
        case 8: return 4; 
    }
}

static unsigned char getLevelcode( char* path ){
    unsigned char code=255;
    if(strcmp("NONE",path)==0)
        code=0;     // 0  -->NONE 0
    else if(strcasecmp ("ERROR",path)==0)
        code=1;     // 1  -->ERROR 1
    else if(strcasecmp ("WARN",path)==0)
        code=3;     // 11   -->WARN 10 || ERROR 1
    else if(strcasecmp ("INFO",path)==0)
        code=7;     // 111  --->INFO 100 || WARN 10 || ERROR 1
    else if(strcasecmp ("DEBUG",path)==0)
        code=15;    // 1110 --->DEBUG 1000 || INFO 100 || WARN 10 || ERROR 1
    else
        code=7;
    return code;
} 
 
static unsigned char ReadConfig(char *path){
    char value[512]={0x0};
    char data[50]={0x0};
 
    FILE *fpath=fopen(path,"r");
    if(fpath==NULL)
        return -1;
    fscanf(fpath,"path=%s\n",value);
    strcat(value,"/");
    strcpy(logsetting.filepath,value);
 
    fscanf(fpath,"level=%s\n",value);
    logsetting.loglevel=getLevelcode(value);
    fclose(fpath);
    return 0;
}
// home/ss/conf/log.conf
void setLogConfPath( char* path ){
    if( path!=NULL ){
        strcpy(confFile, path);
    }else{
        getcwd(confFile,sizeof(confFile));    
        strcat(confFile,"/log.conf");
    }
}

/*日志设置信息*/
static void getlogset(){
    if( strlen(confFile)==0 )
        setLogConfPath(NULL);

        // !=0 ==> error
        if(access(confFile,F_OK)!=0 || ReadConfig(confFile)!=0){
            getcwd(logsetting.filepath,sizeof(logsetting.filepath));
            strcat(logsetting.filepath,"/");
            logsetting.loglevel=getLevelcode("INFO");
        }
}
 
/* 获取日期 */
static char * getdate(char *date){
    time_t timer=time(NULL);
    strftime(date,11,"%Y-%m-%d",localtime(&timer));
    return date;
}
 
/* 获取时间 */
static void settime(){
    time_t timer=time(NULL);
    strftime(loging.logtime,20,"%Y-%m-%d %H:%M:%S",localtime(&timer));
}
 
/* 不定参打印 */
static void PrintfLog(char * fromat,va_list args){
    int d;
    char c,*s;
    while(*fromat)
    {
        switch(*fromat){
            case 's':{
            	if( *(fromat-1) == '%'){
                        s = va_arg(args, char *);
                        fprintf(loging.logfile,"%s",s);
            	}else{
                        fprintf(loging.logfile,"%c",*fromat);
            	}               
                break;}
            case 'd':{
            	if( *(fromat-1) == '%'){
                        d = va_arg(args, int);
                        fprintf(loging.logfile,"%d",d);
                }else{
                        fprintf(loging.logfile,"%c",*fromat);
                }
                break;}
            case 'c':{
            	if( *(fromat-1) == '%'){
                        c = (char)va_arg(args, int);
                        fprintf(loging.logfile,"%c",c);
                }else{
                        fprintf(loging.logfile,"%c",*fromat);
                }
                break;}

            default:{
                if(*fromat!='%'&&*fromat!='\n')
                    fprintf(loging.logfile,"%c",*fromat);
                break;}
        }
        fromat++;
    }
    fprintf(loging.logfile,"%s","]\n");
}
 
static int initlog(unsigned char loglevel){
    char strdate[30]={0x0};

    //获取日志配置信息
    if( strlen(logsetting.filepath) == 0 ){
        getlogset();
    }
    
    // 级别打印
    if((loglevel&(logsetting.loglevel))!=loglevel || logsetting.loglevel == 0)
        return -1;
 
    memset(&loging,0,sizeof(LOG));
    //获取日志时间
    settime();

    // 获取要写的文件名
    getdate(strdate);
    strcat(strdate,".log");
    memcpy(loging.filepath,logsetting.filepath,MAXFILEPATH);
    strcat(loging.filepath,strdate);

    //打开日志文件
    if(loging.logfile==NULL)
        loging.logfile=fopen(loging.filepath,"a+");
    if(loging.logfile==NULL){
        perror("Open Log File Fail!");
        return -1;
    }
    //写入日志级别，日志时间
    fprintf(loging.logfile,"[%s] [%s]:[",LogLevelText[menuToidx(loglevel)],loging.logtime);
    return 0;
}
 
/*
 *日志写入
 * */
int LogWrite(unsigned char loglevel,char *fromat,...){
    va_list args;

    //初始化日志
    if(initlog(loglevel)!=0)
        return -1;

    //打印日志信息
    va_start(args,fromat);
    PrintfLog(fromat,args);
    va_end(args);

    //文件刷出
    fflush(loging.logfile);

    //日志关闭
    if(loging.logfile!=NULL)
        fclose(loging.logfile);

    loging.logfile=NULL;
    return 0;
}

void logDecollator(){
    LogWrite(INFO,"=======================================================");
}
