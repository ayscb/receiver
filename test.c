/*************************************************************************
	> File Name: test.c
	> Author: 
	> Mail: 
	> Created Time: Tue 19 May 2015 09:15:42 AM PDT
 ************************************************************************/
#include "receiver.h"
#include "conf.h"

#include <stdio.h> 
#include <stdlib.h>
#include <string.h>

int main( int argc, char ** args ){

    configure();
    initClient();
    
   testData hdr;
   memset(hdr.data,0, sizeof(hdr.data));
   hdr.data[0] = 9;
   hdr.data[1] = 11;
   hdr.data[1] = 11;
   hdr.length = sizeof(hdr.data);
    
    while(1){
     //   struct ether_hdr* hdr = getData();
        struct buffer_s* sendDataBuf = fillNetflowData(&hdr);
        runClient(sendDataBuf);
    }
    return 0;
}
