/*************************************************************************
	> File Name: receiver.h
	> Author: 
	> Mail: 
	> Created Time: Tue 19 May 2015 09:15:25 AM PDT
 ************************************************************************/

#ifndef _RECEIVER_H
#define _RECEIVER_H
#include "load.h"

typedef struct buffer_s{
    char* buff;
    int bufflen;
    int buffMaxLen;
} buffer_s;

void initClient();
void runClient(struct buffer_s* data);
//struct buffer_s* fillNetflowData(struct ether_hdr* eth_hdr) ;
struct buffer_s*  fillNetflowData_test(testData*  eth_hdr);

#endif

