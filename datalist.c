/*************************************************************************
	> File Name: datalist.c
	> Author: 
	> Mail: 
	> Created Time: Wed 20 May 2015 08:06:54 AM PDT
 ************************************************************************/
#include "datalist.h"
#include "log.h"
static int incrNum = 10000;

void writeData(testData* data){

    incrNum -- ;
    if( isFull()) return; 
    if(data == NULL) return; 
    dataBuff.datalist[dataBuff.cur_write_idx] = data;
    dataBuff.cur_write_idx = (dataBuff.cur_write_idx + 1)%MAXLENGTH;
}

testData* readData(){
    if( isEmpty())  return NULL;
    int idx = dataBuff.cur_read_idx;
    dataBuff.cur_read_idx = (dataBuff.cur_read_idx+1)%MAXLENGTH;
    return (testData* )(dataBuff.datalist[idx] );
}

void prinfBufferInfo(){
    LogWrite(INFO,"---------- buffer Info ---------------");
    LogWrite(INFO,"current write idx %d, and read idx is %d",
            dataBuff.cur_write_idx, dataBuff.cur_read_idx);
    LogWrite(INFO,"write data miss number %d, read data miss number %d",
        dataBuff.miss_w_count,dataBuff.miss_r_count);
    LogWrite(INFO,"---------- buffer Info ---------------");
}

int isEmpty(){
    if(dataBuff.cur_read_idx == dataBuff.cur_write_idx){
        dataBuff.miss_r_count++;
        return 1;
    }else{
        return 0;
    }
}

int isFull(){
      if( (dataBuff.cur_write_idx+1)%MAXLENGTH == dataBuff.cur_read_idx){
          dataBuff.miss_w_count ++;
          return 1;
      }else{
          return 0;
      }
}

