/*************************************************************************
        > File Name: load.h
        > Author: 
        > Mail: 
        > Created Time: Wed 20 May 2015 08:06:45 AM PDT
 ************************************************************************/

#ifndef _LOAD_H
#define _LOAD_H

#define BASEINC 1000000

typedef struct {
    char data[1500];
    int length;
} testData;

struct {
    testData* datalist;
    int maxNum;
    int totalNum;
    int currId;
    int cycleCount;
} testDataList;

void test_loadData();
testData* getData();

#endif
