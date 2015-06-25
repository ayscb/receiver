/*************************************************************************
	> File Name: utils.h
	> Author: 
	> Mail: 
	> Created Time: Wed 20 May 2015 08:06:45 AM PDT
 ************************************************************************/

#ifndef _UTILS_H
#define _UTILS_H

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
	
char * del_left_trim(char *str);
char * del_both_trim(char * str);
void setAddress(struct sockaddr_in* add, char* ip, int port);
char* getLongTime(char* timeBuff, int len);		// %Y-%m-%d %H:%M:%S
char* getShortTime(char* timeBuff, int len);		// %H:%M:%S

int* getGroupDataPos(char* data, int* num);
int* getInnerDataPos(char* data, int* num);
void requestWorkerIPs(char* removeIP_port, char* data, unsigned short  * len);
enum type{ workerlistMsg=1,ruleMsg, reqIps };
#endif
