/*************************************************************************
	> File Name: utils.c
	> Author: 
	> Mail: 
	> Created Time: Wed 20 May 2015 08:06:54 AM PDT
 ************************************************************************/
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>		//isblank()
#include <time.h>

static struct command_t {
    char msg_prefix[3];     // $$           返回的结果为 $$+2&192.1.1.1:1000&1.2.2.2.2:334
    
    const char inner_delim[2];    // ;
    const char outer_delim[2];    // &
} command = {  "$$", ";", "&" };

/*   删除左边的空格   */
char * del_left_trim(char *str) {
    for (;*str != '\0' && isblank(*str) ; ++str);
    return str;
}

/*   删除两边的空格   */
char * del_both_trim(char * str) {
    char *p;
    char * szOutput;
    szOutput = del_left_trim(str);
    for (p = szOutput + strlen(szOutput) - 1; p >= szOutput && isblank(*p);
            --p);
    *(++p) = '\0';
    return szOutput;
}

void setAddress( struct sockaddr_in* add, char* ip, int port ){
    bzero(add, sizeof(struct sockaddr_in));
    add->sin_family = AF_INET;
    add->sin_port = htons(port);
    inet_pton(AF_INET, ip, &add->sin_addr);
    bzero(&add->sin_zero, sizeof(add->sin_zero));
}

char* getLongTime(char* timeBuff, int len){
    time_t timer=time(NULL);
    strftime(timeBuff,len,"%Y-%m-%d %H:%M:%S",localtime(&timer));
    return timeBuff;
}

char* getShortTime(char* timeBuff, int len){
    time_t timer=time(NULL);
    strftime(timeBuff,len,"%H:%M:%S",localtime(&timer));
    return timeBuff;
}

int* getGroupDataPos(char* data, int* num){
    int idx = 0;
    int totalNum = 1;
    char *p = data;
    int len = strlen(data);
    if(strncasecmp(p, command.msg_prefix, strlen(command.msg_prefix)) != 0){
        return NULL;
    }
    
     for(; (p - data) <= len;){
        if( strncmp(p, command.outer_delim, strlen(command.outer_delim)) == 0 ){
            totalNum ++;
        }
        p += strlen(command.outer_delim);
    }
    int* pos = (int*) malloc(sizeof(int) * totalNum);

    p = data + strlen(command.msg_prefix);    // skip $$
    pos[idx++] = (int)(p - data);
    strtok(p, command.outer_delim);
    while(( p = strtok(NULL, command.outer_delim)) != NULL){
        pos[idx++] = (int)(p - data);
    }
    *num = idx;
    return pos;
}

int* getInnerDataPos(char* data, int* num){
    int idx = 0;
    int totalNum = 1;
    char *p = data;
    int len = strlen(p);

    for(; (p - data) <= len;){
        if( strncmp(p, command.inner_delim, strlen(command.inner_delim)) == 0 ){
            totalNum ++;
        }
        p += strlen(command.inner_delim);
    }
    
    int * pos = (int*) malloc(sizeof(int) * totalNum);
    p = data;
    pos[idx++] = 0;
    strtok(p, command.inner_delim);
    while((p = strtok(NULL,command.inner_delim)) != NULL && (p-data) <= len){
        pos[idx++] = (int)(p - data);
    }
    *num = idx;
    return pos;
}

/**
 *  format: 
 *      $$3
 *      $$3&ip1:port
 * @param removeIP_port
 * @param data
 */
void requestWorkerIPs(char* removeIP_port, char* data, unsigned short  * len){
    char* p = data;
     unsigned short offset = sizeof (unsigned short); //skip length itself
    strcpy(p + offset, command.msg_prefix);
    offset += (unsigned short) strlen(command.msg_prefix);  // $$
    data[offset] = (char)reqIps;                                                   // 3
    offset ++;
    
    if (removeIP_port != NULL) {
        strcpy(data+offset, command.outer_delim);       // &
        offset += strlen(command.outer_delim);
        
        strcpy(data+offset, removeIP_port);
        offset += strlen(removeIP_port);
        printf("Request worker list from master, except %s\n", removeIP_port);
    } else {
        printf("Request worker list from master.\n");
    }
    *len = offset;
}