/*************************************************************************
	> File Name: conf.h
	> Author: 
	> Mail: 
	> Created Time: Tue 19 May 2015 11:03:23 PM PDT
 ************************************************************************/

#ifndef _CONF_H
#define _CONF_H

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

struct {
    struct sockaddr_in * masterIP;
    int masterNum;
} masterList;

struct {
    int singleWaitSecond;

    int totalMaxTryNum;
    int receiverWaitSecond;

    long countIntervalNum;
    int showCount;		// 1 true 0 fasle
}netflowConf;

struct {
    char testLoadData[100];
    char testLoadTemp[100];
    char testLoadMix[100];

    int durationTime;	//s
    int rate; 	// MB/s
}netflowtest;

void configure();
void setAddress( struct sockaddr_in* add, char* ip, int port );
#endif
