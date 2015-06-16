/*************************************************************************
	> File Name: utils.c
	> Author: 
	> Mail: 
	> Created Time: Wed 20 May 2015 08:06:54 AM PDT
 ************************************************************************/
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>		//isblank()
#include <time.h>

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
