/*************************************************************************
        > File Name: test.c
        > Author: 
        > Mail: 
        > Created Time: Tue 19 May 2015 09:15:42 AM PDT
 ************************************************************************/
#include "receiver.h"
#include "conf.h"
#include "load.h"

#include <stdio.h> 
#include <stdlib.h>
#include <string.h>

int main(int argc, char ** args) {
    
    configure();
    test_loadData();
    initClient();

     testData * data = getData();
    buffer_s* sendDataBuf = fillNetflowData(data);
    while (1) {
        if(runClient(sendDataBuf) == 1){
            data = getData();
            sendDataBuf = fillNetflowData(data);
        }
    }
    return 0;
}
