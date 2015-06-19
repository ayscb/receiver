/*************************************************************************
	> File Name: receiver.c
	> Author: 
	> Mail: 
	> Created Time: Thu 14 May 2015 06:44:40 AM PDT
 ************************************************************************/
#include  "receiver.h"

#include "log.h"
#include "conf.h"
#include "utils.h"
#include "datalist.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <memory.h>
#include <netinet/tcp.h> 
#include <signal.h>  

#define DEFAULT_BUFFER 1600
#define DEFAULT_WORKER_BASE_NUM 5
#define DEFAULT_MAX_ZERO_COUNT 10

typedef enum{ connected, connecting, closed, retry, failed, fd_error} m_status;

//**********************************************************
//      Persional Variabe Definition
//**********************************************************

static struct command_t{
    char req_worklist[6];       // $req$   请求worker的格式 
    char delim[2];                  // &
    char req_report[9];         // $report$
    char res_prefix[3];         // $$           返回的结果为 $$+2&192.1.1.1:1000&1.2.2.2.2:334
}command;

static struct worker_list_t{
    int baseIncNum;

    char** workerList;                          //  IP string , like "1.2.3.4:10010"
    int* c_clientfd ;                               // the point to client fd ( map to workerList one by one )
    struct sockaddr_in* workerIP;      // ip Address
    m_status* workersStatus;             // worker status  
    int maxNum;                                  // the size of workerList we have malloc
    
    int activeNum;          // the number of the workers that is active
    int* active_to_idx;    // the idx map to active c_clentfd
}worker_list;

struct masterStatus_t{
    int m_clientfd;
    char recvBuff[1024];
    char sendBuff[1024];
    int sendLen;
    int retryMasteridx;
    int retryTimes;             // retry times
    m_status status;
    enum {off,on} report;
}masterStatus;

struct statistic_t{
    unsigned int TB;
    unsigned int totalPackageCount;	// total package numss
    unsigned int totalPackageT;		// total package level 1T,2T......
    unsigned int totalMissCount;		// total missing package num
    unsigned int totalMissT;		// total missing level 1T,2T......

    char startT[10]; 
    unsigned int intervalPackageCount;		
    unsigned int intervalPackageMiss;
}statistic;

static buffer_s sendbuffer;

static fd_set readSet;
static fd_set writeSet;
struct timeval select_tm ;

static long t_i = 0;
//**********************************************************
//      Persional Function Statement
//**********************************************************

static void initCmd();
static void initMasterClient();
static void initWorkerList();
static m_status tryConnectMaster();
static m_status trySingleMasterConnect(struct sockaddr_in* ip);
static int setTcpKeepAlive(int fd, int start, int interval, int count);

static void requestMaster(char* removeIP_port);
static void updateMaxfd(int currfd);
static int resetFdset(struct buffer_s* data);
static int resetMasterFdSet(int curfd);
static int resetWorkerFdset(int curfd, struct buffer_s* data);
static void dealWithMasterSet();
static void dealWithWorkerSet();
static void dealWithMasterData(char* masterData, int len);

static void updateWorkerList(char* workerList, int len);
static int resetWorkListSpace(int needNum);
static void addNewConnect(char* ip, int index);
static void deleExistConnect(int index);
static void updateActiveClientfd();
static void retryConnectMaster();
static void removeWorker(int activeIdx);
static int createSocket();
static void printCountLog();
static void getCountInfo();

//**********************************************************
//      Global Function Implement
//**********************************************************

void initClient() {
    printf("Start to run receiver component....\n");
    initCmd();
    initMasterClient();
    initWorkerList();
    setTcpKeepAlive(masterStatus.m_clientfd, 10, 10, 5);
    signal(SIGPIPE,SIG_IGN);
    requestMaster(NULL);
}

void runClient(struct buffer_s* data){
        int maxfd = resetFdset(data);
        int no = select(maxfd, &readSet, &writeSet, NULL, &select_tm );
        switch(no){
            case -1 : 
                LogWrite(ERROR,"The receiver select error!! %s ...",strerror(errno));
                break ;
            case 0 : 
                if(masterStatus.status == retry || masterStatus.status == connecting){
                    retryConnectMaster();
                }
                break ;
            default:    
                LogWrite(DEBUG,"ReadSet and WriteSet is available. readSet=%d,writeSet=%d",readSet,writeSet);                     
                dealWithMasterSet();
                dealWithWorkerSet(data);
        }
}

//struct buffer_s* fillNetflowData(struct ether_hdr* eth_hdr) {
//    // TODO here we will get the netflow data
//    if(eth_hdr){
//        return NULL;
//    }
//    struct ipv4_hdr *ip_hdr = (struct ipv4_hdr *)(eth_hdr + 1);
//    uint8_t ip_len = (ip_hdr->version_ihl & 0xf) * 4;                
//    struct udp_hdr* u_hdr = (struct udp_hdr *)((u_char *)ip_hdr + ip_len);
//    uint16_t payload_shift = sizeof(struct udp_hdr);
//    uint16_t payload_len = udp_hdr->dgram_len;
//    NetFlow5Record* 5Record = (NetFlow5Record *)((u_char *)u_hdr + payload_shift); 
//                      
//    //  data format :
//    // totalLen(1int) + ipflage(1byte) + ipAddress(4|16Byte) + netflowData(nByte)
//    short ipLen = ( ip_hdr->version_ihl >> 4 ) == 4 ? 4 : 16;
//    short totalLen = sizeof(short) + sizeof(char) + ipLen + (short)payload_len;
//    if(buffer.buffMaxLen <  totalLen){
//        free(buffer.buff);
//        buffer.buff = (char*)malloc( totalLen);
//        if(buffer.buff == NULL){
//                return NULL;
//        }
//        buffer.buffMaxLen = totalLen;
//    }
//
//    char* p = buffer.buff;
//    memcpy(p, &totalLen, sizeof(short));    
//    p = p + sizeof(short);
//    
//    *p = (char)ipLen;
//    p ++;
//    
//    memcpy(p, ip_hdr->???, ipLen);    // src IP
//    p = p + ipLen;
//    
//    memcpy(p, 5Record, payload_len);
//    buffer.bufflen = totalLen;
//    return &buffer;
//}

struct buffer_s* fillNetflowData_test(testData* eth_hdr) {
    // TODO here we will get the netflow data
    if(eth_hdr == NULL){
        return NULL;
    }
                      
    //  data format :
    // totalLen(1int) + ipflage(1byte) + ipAddress(4|16Byte) + netflowData(nByte)
    ushort payload_len = (ushort)eth_hdr->length;
    ushort ipLen = 4;
    // totalLen + char + ipLen + payLoad
    ushort totalLen = sizeof(ushort) + sizeof(char) + ipLen + payload_len;
   
    if(sendbuffer.buffMaxLen <  totalLen){
        free(sendbuffer.buff);
        sendbuffer.buff = (char*)malloc( totalLen);
        if(sendbuffer.buff == NULL){
                return NULL;
        }
        sendbuffer.buffMaxLen = totalLen;
    }

    char* p = sendbuffer.buff;
    memcpy(p, &totalLen, sizeof(short));    
    p = p + sizeof(short);
    
    *p = (char)ipLen;
    p ++;
    
    unsigned char ip[4]={192,168,1,1};
    memcpy(p, &ip, ipLen);    // src IP
    p = p + ipLen;
    
    memcpy(p, eth_hdr->data, payload_len);
    sendbuffer.bufflen = totalLen;
    return &sendbuffer;
}

//**********************************************************
//      Persional Function Implement
//**********************************************************
static void initCmd(){
    strcpy(command.req_worklist,"$req$");
    strcpy(command.delim,"&");
    strcpy(command.req_report,"$report$");
    strcpy(command.res_prefix,"$$");
    
    // init sendbuffer
    sendbuffer.buff = (char*) malloc(sizeof(char) * DEFAULT_BUFFER);
    sendbuffer.buffMaxLen = DEFAULT_BUFFER;
    sendbuffer.bufflen = 0;
    
}

static void initMasterClient(){
    printf("Try to connect with master client.\n");
    LogWrite(INFO, "Start to run master client.");
    if(tryConnectMaster() != connected){
        printf("There is no master to connect, the receiver will quit.\n");
        LogWrite(ERROR, "There is no master to connect, the receiver will quit.");
        exit(-1);
    }else{
        printf("Master connected!\n");
        LogWrite(INFO, "Master connected!");
    }
}

static void initWorkerList(){

    worker_list.baseIncNum = DEFAULT_WORKER_BASE_NUM;
    int defaultNum = worker_list.baseIncNum;
    worker_list.workerList = (char**)malloc( sizeof(char*) * defaultNum );
    memset( worker_list.workerList, 0, sizeof(char*)*defaultNum );

    worker_list.c_clientfd = (int*)malloc(sizeof(int)* defaultNum );
    worker_list.workerIP = (struct sockaddr_in*)malloc(sizeof(struct sockaddr_in)* defaultNum );
    worker_list.workersStatus =(m_status*)malloc(sizeof(m_status)* defaultNum );

    worker_list.maxNum = defaultNum;
    worker_list.activeNum = 0;
    LogWrite(INFO, "Init worker List!");
}

/*
 * try connect woth whole master
 *
 * Return:  m-status 
 *              failed:
 *              connected:
 */
static m_status tryConnectMaster(){
    int maxRetryNum = netflowConf.totalMaxTryNum;
    LogWrite(INFO, "Max retry number is %d times.", maxRetryNum);
    LogWrite(INFO, "Registed %d masters.", masterList.masterNum);

    int tryidx = 0;
    while( tryidx != maxRetryNum ){
        LogWrite(INFO,"Retry %d times to connect with whole master!", tryidx+1);
            
        // connect with all master
        int masteridx = 0;
        while( masteridx != masterList.masterNum ){
            
            struct sockaddr_in* ip = masterList.masterIP + masteridx;
            LogWrite(INFO,"try to connect with %s:%d", inet_ntoa(ip->sin_addr), ntohs(ip->sin_port));

            m_status st = trySingleMasterConnect(ip);
            if( st == connected ){
                masterStatus.status = connected;
                logDecollator();
                return connected;
            }else{
                masteridx ++;                      
            }
        } 
        logDecollator(); 
        tryidx++;
    }
    masterStatus.status = failed;
    return failed;	// no Master to connect
}

/*
 * try connect with a single master
 * 
 * Return:  m-status 
 *              failed:
 *              connected:
 *              fd_error:
 */
static m_status trySingleMasterConnect(struct sockaddr_in* ip){

    masterStatus.m_clientfd = createSocket();
    if( masterStatus.m_clientfd == -1){
        LogWrite(ERROR, "Init master client's socket error, %s", strerror(errno));
        return fd_error;
    }

    int res = connect(masterStatus.m_clientfd,  (struct sockaddr*)ip, sizeof(struct sockaddr) );
                
    if(res == 0){
        LogWrite(INFO, "Connected, Master is %s", inet_ntoa(ip->sin_addr));       
        return connected;
    }else{
        if(errno != EINPROGRESS){
            LogWrite(INFO, "Connect failed. %s", strerror(errno));
            close(masterStatus.m_clientfd);
            return failed;
        }
    }

    struct timeval tv; 
    tv.tv_sec = netflowConf.singleWaitSecond;
    tv.tv_usec = 0;
    LogWrite(INFO, "Try  wait %ds time. ", tv.tv_sec);

    errno = 0;
    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);
    FD_SET(masterStatus.m_clientfd, &readSet);
    writeSet = readSet;

    res = select(masterStatus.m_clientfd+1, &readSet, &writeSet, NULL, &tv);  	
    switch(res){
        case -1:
            LogWrite(ERROR, "Select error in connect ... %s",strerror(errno)); 
            close(masterStatus.m_clientfd);
            return failed; 

        case 0:
            LogWrite(INFO, "Select time out.");   
            close(masterStatus.m_clientfd);
            return failed;

        default:
           if(FD_ISSET(masterStatus.m_clientfd, &readSet) || FD_ISSET(masterStatus.m_clientfd, &writeSet)){       
                int no = connect(masterStatus.m_clientfd, (struct sockaddr*)ip, sizeof(struct sockaddr));      
                if( no == 0){
                    LogWrite(INFO, "Connected, Master is %s", inet_ntoa(ip->sin_addr));
                    return connected;  
                }else{
                    int err = errno;
                    if( err == EISCONN ){    
                        LogWrite(INFO, "Connected, Master is %s", inet_ntoa(ip->sin_addr));
                        return connected;   
                    }else{   
                        LogWrite(INFO, "Connect failed! %s %d", strerror(err), err);
                        close(masterStatus.m_clientfd);                  
                        return failed;
                    }  
                }                                       
            }                                        
    }
}
/**
 * 
 * @param fd
 * @param start : 启用心跳机制开始到首次心跳侦测包发送之间的空闲时间  
 * @param interval : 两次心跳侦测包之间的间隔时间   
 * @param count : 探测次数，即将几次探测失败判定为TCP断开   
 * @return 
 */
static int setTcpKeepAlive(int fd, int start, int interval, int count){
    int keepAlive = 1;
    if(fd<0 || start<0 || interval<0 || count<0) return;
    
    // set keep alive
    if( setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void*)&keepAlive, sizeof(keepAlive)) == -1){
         perror("setsockopt");   
        return -1;
    }
    
    // 启用心跳机制开始到首次心跳侦测包发送之间的空闲时间   
    if(setsockopt(fd,SOL_TCP, TCP_KEEPIDLE, (void *)&start,sizeof(start)) == -1) {   
        perror("setsockopt");   
        return -1;   
    }   
        //两次心跳侦测包之间的间隔时间   
    if(setsockopt(fd,SOL_TCP,TCP_KEEPINTVL,(void *)&interval,sizeof(interval)) == -1) {   
        perror("setsockopt");   
        return -1;   
    }   
    //探测次数，即将几次探测失败判定为TCP断开   
    if(setsockopt(fd,SOL_TCP,TCP_KEEPCNT,(void *)&count,sizeof(count)) == -1) {   
        perror("setsockopt");   
        return -1;   
    }   
    return 0;  
    
}
//-------------------------------deal with run function -------------------------------------------------

/* 
 * @Function:
 *      the function for request the worker list. command may be like as follow  :
 *              1) $req$                                        --> request the worker list
 *              2) $req$-192.168.80.1:100020     -->request the worker list except 192.168.80.1:100020
 *
 * @Param:
 *      removeIP_port : 
 *          except removeIP_port, when request the worker list.  The value can be set NULL
 *
 * @Return:
 *      void
*/
static void requestMaster(char* removeIP_port){

    unsigned short offset = sizeof(unsigned short);     //skip length itself
    
    strcpy(masterStatus.sendBuff + offset, command.req_worklist);
    offset += (unsigned short)strlen(command.req_worklist);
    
    // only request the worker list. 
    if(removeIP_port != NULL){                 
        char* p = masterStatus.sendBuff + offset;
        *p = '-';
        p++;
        strcpy(p,removeIP_port);
        offset += (unsigned short)strlen(removeIP_port);
    }
    
    // copy the length
    memcpy(masterStatus.sendBuff, &offset, sizeof(unsigned short));
    masterStatus.sendLen = offset;
    masterStatus.report = on;
    LogWrite(DEBUG, "Request worker list from master, request command length %d, content is %s", 
    *(unsigned short*)masterStatus.sendBuff, masterStatus.sendBuff + sizeof(unsigned short));
}

static int resetFdset(struct buffer_s* data){
    int fd = 0;
    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);
    fd = resetMasterFdSet(fd);
    fd = resetWorkerFdset(fd,data);
    
    // init select_time
    select_tm.tv_sec = netflowConf.receiverWaitSecond; 
    return fd + 1;
}

static int resetMasterFdSet(int curfd){
    // Focus on readSet, to get the control information from master.
    // If we need to send the data to master, we should register in writeSet

    if(masterStatus.status == connected 
            || masterStatus.status == retry 
            || masterStatus.status == connecting){
        LogWrite(DEBUG,"FD_SET-master_fd(%ld)-readSet",masterStatus.m_clientfd);
        FD_SET(masterStatus.m_clientfd, &readSet);
        
        if(masterStatus.report == on 
                || masterStatus.status == retry
                || masterStatus.status == connecting){
            LogWrite(DEBUG,"FD_SET-master_fd(%d)-writeSet",masterStatus.m_clientfd);
            FD_SET(masterStatus.m_clientfd, &writeSet);
        }
         return ( curfd > masterStatus.m_clientfd)? curfd : masterStatus.m_clientfd;
    } 
    return curfd;
}

static int resetWorkerFdset(int curfd, struct buffer_s* data){
    // Focus on readset & writeset. 
    // ReadSet only for get the disconnect information, and WriteSet for write data.
    int maxfd = curfd;
    int i;
    for(i=0; i != worker_list.activeNum; i++){
        int idx = worker_list.active_to_idx[i];
        int fd = worker_list.c_clientfd[idx];
        FD_SET(fd, &readSet);
   //     LogWrite(DEBUG,"FD_SET-worker_fd(%d)-readSet",fd);
        if(data != NULL){
            FD_SET(fd, &writeSet);
            LogWrite(DEBUG,"FD_SET-worker_fd(%d)-writeSet",fd);
        }           
        maxfd = maxfd > fd  ? maxfd : fd;
    }
    return maxfd;
}

static void dealWithMasterSet(){
    
    if(FD_ISSET(masterStatus.m_clientfd, &readSet)){
        LogWrite(DEBUG,"Deal with command data from master(readSet).");

        switch(masterStatus.status) {
            case connected:
                memset( masterStatus.recvBuff, 0, sizeof(masterStatus.recvBuff));
                
                int readCount = recv(masterStatus.m_clientfd, 
                        masterStatus.recvBuff, sizeof(masterStatus.recvBuff), 0);

                if( readCount == -1 || readCount == 0 ){        // failed connection           
                        LogWrite(WARN,"Disconnect with master[error: %s]，"
                                "and will retry to connect with master.", strerror(errno)); 
                        
                        close(masterStatus.m_clientfd);
                        masterStatus.status = retry;
                                        
                       masterStatus.retryTimes = 0;
                        masterStatus.retryMasteridx = 0;                         
                        retryConnectMaster();                 
                }else{
                     LogWrite(DEBUG,"read from tcp server, the data is %s",masterStatus.recvBuff);
                    dealWithMasterData(masterStatus.recvBuff,readCount);
                }              
                break;
                
            case retry:
            case connecting :
                 LogWrite(DEBUG,"Master is lost, and retry to connecte.");
                 retryConnectMaster();
                 break;       
            default:            
                 LogWrite(ERROR,"There is no available master to connect with. "
                         "Since we skip all disconnected masters, so this message should not been seen!");
        }
    }

    // here we will deal with sending report and request to master
    if(FD_ISSET(masterStatus.m_clientfd, &writeSet)){
        
        switch(masterStatus.status){
            case retry:
            case connecting:
                retryConnectMaster();
                break;
            case connected:{
                int countNum =  
                    send(masterStatus.m_clientfd, masterStatus.sendBuff, masterStatus.sendLen, 0);
                char* bp = masterStatus.sendBuff;
                while(countNum != masterStatus.sendLen ){
                    if( countNum == -1){        
                        LogWrite(ERROR,"Connect master error. %s",strerror(errno));
                        close(masterStatus.m_clientfd);
                        masterStatus.status = retry;
                        retryConnectMaster();                
                    }else{
                        LogWrite(WARN,"Send to master total data %d, actual data %d.", 
                                masterStatus.sendLen, countNum);
                        bp = bp + countNum;
                        countNum = send(masterStatus.m_clientfd, bp, masterStatus.sendLen - countNum, 0);
                    }
                }
                masterStatus.report = off;          
            }
                break;
        }   
    }
}

static void dealWithWorkerSet(struct buffer_s* data){

    int worker_idx = 0;
    while( worker_idx < worker_list.activeNum ){
        int idx = worker_list.active_to_idx[worker_idx];
        int fd = worker_list.c_clientfd[idx];

        LogWrite(DEBUG,"DealWithWorkerSet! activeNum=%d, idx=%d, currfd=%d, IP=%s", 
            worker_list.activeNum, idx, fd, worker_list.workerList[idx]);

        /* remove the disconnect socket */
        if( FD_ISSET(fd, &readSet)){         
            int readCount = recv(fd,masterStatus.recvBuff, sizeof(masterStatus.recvBuff), 
                    MSG_WAITALL|MSG_DONTWAIT );

            LogWrite(DEBUG,"worker read! errno=%d, errStr=%s, readCount=%d",
                errno, strerror(errno), readCount);

            if( readCount == -1 || readCount == 0 ){
    //            if(errno == ECONNRESET ){                
                    LogWrite(WARN,"Disconnect with worker %s",worker_list.workerList[idx]); 
                    FD_CLR(fd,&writeSet);
                    shutdown(fd,SHUT_WR);
                    close(fd);
                    printCountLog();

                // If there is only one worker connected with this receiver, we must request a new worker.
                // If there is more than one workers, ignore the request.
                if( worker_list.activeNum == 1 ){
                    requestMaster(worker_list.workerList[idx]);
                    LogWrite(WARN, "There is no available worker to send netflow data to. "
                            "So request worker list to master.");              
                } 

                //TODO : here, we will modify the worker_list.activeNum.
                // Here has a small bug. 
                // Suppose some assigned values:  current worker_list.activeNum = 5 , worker_idx =3.
                // when we remove a dead worker, the worker_list.activeNum = 4,
                // but now worker_idx will be set 4 after the "while" circulation,
                // that means we will skip a active worker.
                removeWorker(worker_idx);   
        //        }
            }
        }
        
      //  if(data == NULL){
     //       LogWrite(INFO,"data is null");
     //       printf("data is null \n");
      //      return;
     //   }

        /* write the netflow package to worker*/
        if( FD_ISSET(fd, &writeSet)){
            LogWrite(DEBUG,"worker write! errno=%d, errStr=%s currIdx %d, connected %d",
                errno, strerror(errno), idx, worker_list.workersStatus[idx]);
    
            statistic.intervalPackageCount ++;
            
            int no = send(fd, data->buff, data->bufflen, 0);
            LogWrite(DEBUG,"worker write! send data count =%d, errno=%d", no,errno);

            while( no != data->bufflen){
                if( no == -1){               
                    LogWrite(ERROR,"Send %s data error.",inet_ntoa(worker_list.workerIP[idx].sin_addr));
                    statistic.intervalPackageMiss ++;
                    break;
                }else{
                    LogWrite(WARN,"Send to %s total data %d, actual data %d.", 
                            inet_ntoa(worker_list.workerIP[idx].sin_addr), data->bufflen, no);
                    no = send(fd, data->buff + no, (size_t)(data->bufflen - no), 0);
                }
            }
            
       //     printf("%ld worker write! send data count =%d, errno=%s\n",++t_i, no, strerror(errno));
        }
        worker_idx ++;
    }
}

/* analysis the data from master, and deal with the data*/
static void dealWithMasterData(char* masterData, int len){

    char* p = masterData;
    p = del_left_trim(p);
    if( strncasecmp(p, command.res_prefix, strlen(command.res_prefix)) == 0 ){
        updateWorkerList(p,len);        
    }else if( strncasecmp(p, command.req_report, strlen(command.req_report)) == 0){
        getCountInfo();
        masterStatus.report = on;
    }
}

/* 
 * Function:
 *  update the  worker_list
 *    mode :
 *      $$$+2&ip1:port&ip2:port     --> add new ip's address
 *      $$$-1&ip1:port                      --> dele exist ip's address
 *      $$$+2&ip1:port&ip2:port&-3&1p3:port&ip4:port  -->add and delete
 * Param:
 *  workerList : the data from master 
 *  len: the data's length
 *
 * Return:
 *  void
*/
static void updateWorkerList(char* workerList, int len){

    char *p = workerList + strlen(command.res_prefix);
    
    while((p - workerList) != len){
         char mode = *p;

        int num = atoi(++p);
        if( num == 0 ) return;

        /* update the workerList */
        strtok(p, command.delim);    // skip the first 'delim', since the first element is ip's number
        char * ip_port = NULL;
        
        switch(mode){
            case '+' :{
                LogWrite(INFO,"Add new workerIPs, total worker num is %d", num);

                /* update the maxNum */
                if( resetWorkListSpace(num) != 0)
                    return;
                
                int i = 0;
                int count = 0;
                while(count != num){
                    ip_port = strtok(NULL, command.delim);
                    if(ip_port==NULL) return;
                    
                    p = ip_port + strlen(ip_port)+1;
                    //check if exist in current workerlist
                    if( worker_list.active_to_idx != NULL){
                        for(i=0; i<worker_list.activeNum; i++){
                            int idx = worker_list.active_to_idx[i];
                            if(strcasecmp(ip_port,worker_list.workerList[idx])==0){
                                break;
                            }
                        }
                        if(i != worker_list.activeNum){
                            break;      // exist in current worker_list
                        }
                    }
                                  
                    // add new ip into worker_list
                    for(i=0; i <  worker_list.maxNum; i++){
                         if(worker_list.workerList[i] == NULL){          // find place to insert into
                             addNewConnect(ip_port, i);
                             break;
                        }
                    }
                    count++;
                }
               
                updateActiveClientfd();
                break;
            }
            case '-':
                 LogWrite(INFO,"Delete exist workerIP , worker num : %d\n", num);
                 int count = 0;
                while(count != num){
                     ip_port = strtok(NULL, command.delim);
                     p = ip_port + strlen(ip_port)+1;
                     if(ip_port==NULL) return;
                     
                     int i =0 ;
                     for(; i < worker_list.activeNum; i++){
                         int idx = worker_list.active_to_idx[i];
                         if(strncasecmp(ip_port, worker_list.workerList[idx],strlen(ip_port)) == 0 ){
                             deleExistConnect(idx);
                             break;
                         }
                     }
                     count++;
                }             
                updateActiveClientfd();
                break;
                
            default :
                LogWrite(WARN,"Wrong Data format!");
        }
    }
}

static int resetWorkListSpace(int needNum){
    if( needNum > worker_list.maxNum - worker_list.activeNum ){
        int realNeedNum = needNum - (worker_list.maxNum - worker_list.activeNum);
        int adjustSize = worker_list.baseIncNum > realNeedNum ? worker_list.baseIncNum : realNeedNum;
        int newSize = worker_list.maxNum + adjustSize ;
		
        worker_list.workerList = (char**)realloc(worker_list.workerList, newSize);
        worker_list.c_clientfd = (int*)realloc(worker_list.c_clientfd, newSize);
        worker_list.workerIP = (struct sockaddr_in*)realloc(worker_list.workerIP, newSize);
        if( worker_list.workerList == NULL || worker_list.c_clientfd == NULL || worker_list.workerIP == NULL){
            LogWrite(ERROR,"Realloc size error.");
            return -1;
        }
        memset(worker_list.workerList + worker_list.maxNum, 0, adjustSize);
        worker_list.maxNum = newSize;       
    }
    return 0;
}

/**
 * 
 * @param ip_port
 * @param index  point to worker_list.workerList
 */
static void addNewConnect(char* ip_port, int index){
    worker_list.workerList[index] = strdup(ip_port);
    char* d = strchr(ip_port,':');
    *d='\0';
    int port = atoi(d+1);

    setAddress( worker_list.workerIP + index , ip_port, port ); 

    worker_list.c_clientfd[index] = createSocket();
    if( worker_list.c_clientfd[index] == -1 ){
        LogWrite(WARN,"Create new socket for %s:%d error, %s ", ip_port, port, strerror(errno));
        return ;
    }
    connect(worker_list.c_clientfd[index], (struct sockaddr *)(worker_list.workerIP + index), 
            sizeof(struct sockaddr));
    LogWrite(INFO,"Add the workerIP, fd=%d is %s:%d", worker_list.c_clientfd[index], ip_port, port);
    printf("Add the workerIP, fd=%d ip=%s:%d\n", index, ip_port, port);
    worker_list.activeNum ++ ;
    worker_list.workersStatus[index] = connecting;
}

/**
 * 
 * @param index point to worker_list.workerList
 */
static void deleExistConnect(int index){
    free(worker_list.workerList + index);   //free the char* point
    worker_list.workerList[index]= NULL;
    shutdown(worker_list.c_clientfd[index], SHUT_RDWR); //close socket
    close(worker_list.c_clientfd[index]);
    worker_list.activeNum -- ;
    worker_list.workersStatus[index] = closed;
}

static void updateActiveClientfd(){
    //TODO: here should be more effective
    free(worker_list.active_to_idx);
    worker_list.active_to_idx = (int*)malloc(sizeof(int) * worker_list.activeNum);

    int active_idx = 0;
    int i = 0;
    while( i != worker_list.maxNum ){
        if( worker_list.workerList[i]!= NULL ){
            // exist ip
            worker_list.active_to_idx[active_idx] = i;
            active_idx ++;
            if( active_idx > worker_list.activeNum ){
                LogWrite(ERROR,"The active num in worker list is error. Expect size is %d.",
                        worker_list.activeNum);
                return;
            }
        }
        i++;
    }
}

//----------------------- disconnected and retry ---------------------------------------------
/* disconnected and retry  */
static void retryConnectMaster(){
    if(masterStatus.status == retry){
        masterStatus.m_clientfd =  createSocket();
        if( masterStatus.m_clientfd == -1){
            LogWrite(WARN, "Init master client's socket error, %s", strerror(errno));
            masterStatus.status = fd_error;
        }else{
            masterStatus.status = connecting;
        }     
    }
    struct sockaddr_in* ip = masterList.masterIP + masterStatus.retryMasteridx;
    int res =  connect(masterStatus.m_clientfd, (struct sockaddr *)ip, sizeof(struct sockaddr));
    if( res == 0 ){
            LogWrite(INFO, "Connected immediately. Master is %s, res=0", inet_ntoa(ip->sin_addr) );
            masterStatus.status = connected;
            requestMaster(NULL);
            return;
    }
		
    int err = errno;
    switch(err){
        case EISCONN :
            LogWrite(INFO, "Connected. Master is %s", inet_ntoa(ip->sin_addr) );  
            masterStatus.status = connected;
            requestMaster(NULL);
            break;
        case EINPROGRESS :
             LogWrite(INFO,"Connecting %s ...error %s",inet_ntoa(ip->sin_addr),strerror(errno));
            masterStatus.status = connecting;
            break;
        case EALREADY:
             LogWrite(INFO,"Connecting %s, errno = %d ",inet_ntoa(ip->sin_addr),strerror(errno),errno);
             masterStatus.status = connecting;
             break;
        default:
            close(masterStatus.m_clientfd);   // close current fd.
           
            if( masterStatus.retryMasteridx == masterList.masterNum ){
                LogWrite(INFO,"Connect %s failed. errno = %d . %s. ",
                        inet_ntoa(ip->sin_addr), errno, strerror(errno));             
                masterStatus.retryMasteridx = 0;
                masterStatus.retryTimes ++;
                
                 if( masterStatus.retryTimes == netflowConf.totalMaxTryNum ){
                    LogWrite(ERROR,"There is no aliving LoadMaster to connect with."); 
                    masterStatus.status = failed;
                }else{
                    logDecollator();
                    LogWrite(INFO,"Try %dth times to connect LoadMaster.",masterStatus.retryTimes);
                    masterStatus.status = retry;
                }
                
             }else{
                LogWrite(INFO,"Connect %s failed. Errno = %d. %s, change another LoadMaster. ",
                        inet_ntoa(ip->sin_addr), errno, strerror(errno)); 
                masterStatus.retryMasteridx ++;
                masterStatus.status = retry;
            }              
    }	
}

/* remove dead worker from active worker list */
static void removeWorker(int dead_active_Idx){
    if( dead_active_Idx >= worker_list.activeNum ){
        LogWrite(ERROR,
            "The active idx %d should not >= max active number workers",
                dead_active_Idx,worker_list.activeNum);
        return;
    }
    int id = worker_list.active_to_idx[dead_active_Idx];
    close(worker_list.c_clientfd[id]);
    
    free(worker_list.workerList[id]);
    worker_list.workerList[id] = NULL;
    worker_list.activeNum --;

    int i = dead_active_Idx;
    while( i != worker_list.activeNum){
        worker_list.workerList[i] = worker_list.workerList[i+1];
        i++;
    }
}

static int createSocket(){
    int tcpfd = socket ( AF_INET, SOCK_STREAM, 0 );
    int flags = fcntl(tcpfd, F_GETFL, 0);
    if( flags < 0 ){
        LogWrite(ERROR, "Set tcp's flag NO_BLOCK error!");
        return -1;
    }
    fcntl(tcpfd, F_SETFL, flags | O_NONBLOCK);
    LogWrite(INFO, "Set tcp' socket flag : NO_BLOCK");
    return tcpfd;
}

//--------------------------- deal with log --------------------------------------------------
static void printCountLog(){
    if(netflowConf.showCount ==-1)
        return;
    if(statistic.intervalPackageCount == netflowConf.countIntervalNum ){
        char now[10]={0};
        getShortTime(now, 10);
        LogWrite(INFO, "## %s --> %s Total package num : %d  sended num: %d missed num: %d. ## ", 
            statistic.startT,now, statistic.intervalPackageCount, 
            (statistic.intervalPackageCount-statistic.intervalPackageMiss), 
            statistic.intervalPackageMiss);
        strcpy(statistic.startT,now);
        
        long TB=1099511627776;
        statistic.totalPackageCount += statistic.intervalPackageCount;
        if( statistic.totalPackageCount >= TB ){
            statistic.totalPackageT ++;
            statistic.totalPackageCount = statistic.totalPackageCount - TB;
        }
        statistic.intervalPackageCount = 0;

        if(statistic.intervalPackageMiss != 0){
            statistic.totalMissCount += statistic.intervalPackageMiss;
            if( statistic.totalMissCount >= TB ){
                statistic.totalMissCount ++;
                statistic.totalMissCount = statistic.totalMissCount - TB;
            }
            statistic.intervalPackageMiss = 0;
        }       
    }
}

static void getCountInfo(){
    // TODO: write the report into masterStatus.sendBuff
    memset(masterStatus.sendBuff, 0, sizeof(masterStatus.sendBuff));
    strcpy(masterStatus.sendBuff,"dataInfo");
}
