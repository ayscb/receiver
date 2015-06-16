/*************************************************************************
	> File Name: load.c
	> Author: 
	> Mail: 
	> Created Time: Wed 20 May 2015 08:06:54 AM PDT
 ************************************************************************/
#include "load.h"
#include "conf.h"
#include "log.h"
#include "datalist.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h> // usleep
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>

//***************************************************
static void test_start();
static void test_end();
static void checkSpace();
static void initList();
static void settime(char* timeStr, int len);

//***************************************************
//      Global function
//***************************************************

void test_loadData(){
    initList();
    char* files[3]; 
    files[0] = netflowtest.testLoadTemp;    //load template first
    files[1] = netflowtest.testLoadMix;
    files[2] = netflowtest.testLoadData;

    int i = 0;
    short length = 0;
    for(; i<3; i++){
        if(strlen(files[i]) == 0){
            continue;
        }
        FILE* fp = fopen(files[i],"rb");
        if(fp == NULL){
            LogWrite(WARN,"Can not open file %s, %s",files[i],strerror(errno));
            continue;
        }
		
        while(feof(fp)==0){
            // read the data
            checkSpace();

            long pos = ftell(fp);
            fread(&length, sizeof(short), 1, fp);
            length = ntohs(length) + 2;			// length(2B)+ length
            if(fseek(fp, pos, SEEK_SET)!=0){
                    LogWrite(WARN,"Read %s file, seek_set error.",files[i]);
            }
            if( length > 1500 || length <= 0){
                // skip the data
                continue;
            } 

            testData* p = testDataList.datalist + testDataList.totalNum;
            fread(p->data, sizeof(char), length, fp);
            p->length = length;
            testDataList.totalNum ++;
        }
        fclose(fp);
    }

    // vertify
    if(testDataList.totalNum == 0){
        LogWrite(ERROR,"Can not find test data file, please check again!");
        exit(-1);
    }
}

void* test_sendData(void* arg){
    LogWrite(INFO,"Start thread!");
    printf("start thread!");
    test_start();
    int curT = time((time_t*)NULL);
    while(time((time_t*)NULL) < testDataList.durationTime){
        usleep(testDataList.usleep);
        testData* data = testDataList.datalist + testDataList.currId;
		
     //   writeData(data->data);
        writeData(data);
        testDataList.currId++;
        if(testDataList.currId == testDataList.totalNum){
            testDataList.cycleCount ++;
            testDataList.currId = 0;
        }
    }
    while(!isEmpty()){};    // wait until read over
    test_end();
}
//***************************************************
//      personal function
//***************************************************
static void test_start(){
    memset(testDataList.startTime,sizeof(testDataList.startTime),1);
    memset(testDataList.endTime,sizeof(testDataList.endTime),1);
    testDataList.cycleCount = 0;
    settime(testDataList.startTime, sizeof(testDataList.startTime));

    if(netflowtest.durationTime == 0){
        LogWrite(WARN,"durationTime will set 60s.");
        testDataList.durationTime = time((time_t*)NULL) + 60;
    }else{
        testDataList.durationTime = time((time_t*)NULL) + netflowtest.durationTime;
    }
}

static void test_end(){
    settime(testDataList.endTime, sizeof(testDataList.endTime));
    LogWrite(INFO,"---------- Test result ---------------");
    LogWrite(INFO,"start time %s, end time %s, total time(s) %d",
        testDataList.startTime, 
        testDataList.endTime,
        testDataList.durationTime);
    LogWrite(INFO,"total send data num is %d",
        testDataList.cycleCount * testDataList.totalNum + testDataList.currId);
    prinfBufferInfo();
    LogWrite(INFO,"---------- Test result ---------------");
}

static void checkSpace(){
    if(testDataList.totalNum == testDataList.maxNum){
        testDataList.datalist = 
        (testData*)realloc(testDataList.datalist,
            testDataList.maxNum + sizeof(testData)*BASEINC);
        if(testDataList.datalist != NULL){
            testDataList.maxNum += BASEINC;
        }
    }
}

static void initList(){
    testDataList.datalist = (testData*)malloc(sizeof(testData)*BASEINC);
    if(testDataList.datalist == NULL){
        LogWrite(ERROR,"Malloc %d space for test fail!",sizeof(testData)*BASEINC);
        exit(-1);
    }

    if(netflowtest.rate == 0){
        netflowtest.rate = 1;
    }
    testDataList.usleep = sizeof(testData) * US / (netflowtest.rate * MB);
}

static void settime(char* timeStr, int len){
    time_t timer=time(NULL);
    strftime(timeStr, len,"%Y-%m-%d %H:%M:%S",localtime(&timer));
}