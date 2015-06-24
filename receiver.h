/*************************************************************************
	> File Name: receiver.h
	> Author: 
	> Mail: 
	> Created Time: Tue 19 May 2015 09:15:25 AM PDT
 ************************************************************************/

#ifndef _RECEIVER_H
#define _RECEIVER_H

#define TEST

#ifndef TEST
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#endif

typedef struct buffer_s{
    char* buff;
    int bufflen;
    int buffMaxLen;
} buffer_s;

void initClient();
void runClient(struct buffer_s* data);

#ifdef TEST
typedef struct{
    char data[1500];
    int length;
}testData;

struct buffer_s* fillNetflowData(testData* eth_hdr);

#else
 struct buffer_s* fillNetflowData(struct ether_hdr* eth_hdr);
#endif

#endif

