/*************************************************************************
	> File Name: load.h
	> Author: 
	> Mail: 
	> Created Time: Wed 20 May 2015 08:06:45 AM PDT
 ************************************************************************/

#ifndef _LOAD_H
#define _LOAD_H
	
#define KB (1024*1024)
#define MB (1204*KB)

// time
#define S (1)
#define US (1000*1000*S)

#define BASEINC 1000000

typedef struct{
    char data[1500];
    int length;
}testData;

struct {
    testData* datalist;
    int maxNum;
    int totalNum;

    int usleep;		// us      20MB/s =>   1500B / ( 20 * 1024 * 1024)
    int currId;

    // Statistics
    char startTime[20];
    char endTime[20];
    int stopTime;

    int cycleCount;	
}testDataList;

void test_loadData();
void* test_sendData(void* arg);

#endif
