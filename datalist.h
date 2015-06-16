/*************************************************************************
	> File Name: datalist.h
	> Author: 
	> Mail: 
	> Created Time: Wed 20 May 2015 08:06:45 AM PDT
 ************************************************************************/

#ifndef _DATALIST_H
#define _DATALIST_H

#define MAXLENGTH (10*1024*1024)	// 1M list
#include "load.h"

// for test
typedef struct{
    testData* datalist[MAXLENGTH];		
    int cur_read_idx;
    int cur_write_idx;
    int miss_w_count;
    int miss_r_count;
} DataBuff;

DataBuff dataBuff;

void initDataBuff();
void writeData(testData* data);
testData*  readData();
int isEmpty();
int isFull();
void prinfBufferInfo();
#endif
