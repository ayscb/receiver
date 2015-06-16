/*************************************************************************
	> File Name: test.c
	> Author: 
	> Mail: 
	> Created Time: Tue 19 May 2015 09:15:42 AM PDT
 ************************************************************************/
#include "test.h"

#include "receiver.h"
#include "conf.h"
#include "datalist.h"
#include "load.h"

#include <stdio.h> 
#include <pthread.h>

int main( int argc, char ** args ){

    pthread_t ptid;
    configure();
    test_loadData();
    initClient();

   pthread_create(&ptid, NULL, test_sendData, NULL);
   
    while(1){
     //   struct ether_hdr* hdr = getData();
        testData* hdr = readData();
        struct buffer_s* sendDataBuf = fillNetflowData_test(hdr);
        runClient(sendDataBuf);
    }
    return 0;
}
