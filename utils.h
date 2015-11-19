/*************************************************************************
	> File Name: utils.h
	> Author: 
	> Mail: 
	> Created Time: Wed 20 May 2015 08:06:45 AM PDT
 ************************************************************************/

#ifndef _UTILS_H
#define _UTILS_H

#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
	
enum type{ workerlistMsg = 1,ruleMsg, reqIps };

typedef struct msg_header{
    char prefix[2];
    uint16_t length;
}msg_header_t;

char * del_left_trim(char *str);
char * del_both_trim(char * str);
void setAddress(struct sockaddr_in* add, char* ip, uint16_t port);
char* getLongTime(char* timeBuff, uint8_t buffLen);		// %Y-%m-%d %H:%M:%S
char* getShortTime(char* timeBuff, uint8_t buffLen);		// %H:%M:%S

void requestWorkerIPs(char* removeIP_port, char* out_data, uint16_t * out_dataLen);
uint8_t vaildMasterMessage(char* data);
uint16_t vaildAndGetMsgLen(msg_header_t header);
uint16_t* getSplitPosArray(uint8_t* data, uint16_t dataLen, uint8_t* delim, uint32_t* out_garrayNum);

uint16_t* getGroupDataPos(char* data, uint16_t dataLen, uint32_t* out_groupNum);
uint16_t* getInnerDataPos(char* data, uint32_t* out_groupNum);

#endif
