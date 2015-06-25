/*************************************************************************
        > File Name: receiver.c
        > Author: 
        > Mail: 
        > Created Time: Thu 14 May 2015 06:44:40 AM PDT
 ************************************************************************/
#include  "receiver.h"

#include "conf.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <errno.h>
#include <memory.h>

#include <unistd.h>
#include <signal.h> 

#define DEFAULT_BUFFER 1600
#define DEFAULT_WORKER_BASE_NUM 5
#define DEFAULT_MAX_ZERO_COUNT 10

typedef enum {
    connected, connecting, closed, retry, failed, fd_error
} m_status;

//**********************************************************
//      Persional Variabe Definition
//**********************************************************

static struct worker_list_t {
    int baseIncNum;

    char** workerList; //  IP string , like "1.2.3.4:10010"
    int* c_clientfd; // the point to client fd ( map to workerList one by one )
    struct sockaddr_in* workerIP; // ip Address
    m_status* workersStatus; // worker status  
    int maxNum; // the size of workerList we have malloc

    int activeNum; // the number of the workers that is active
    int* active_to_idx; // the idx map to active c_clentfd
} worker_list;

struct masterStatus_t {
    int m_clientfd;
    char recvBuff[1024];
    char sendBuff[1024];
    unsigned short sendLen;
    int retryMasteridx;
    int retryTimes; // retry times
    m_status status;

    enum {
        off, on
    } report;
} masterStatus;

static buffer_s sendbuffer;

static fd_set readSet;
static fd_set writeSet;
struct timeval select_tm;

static int worker_idx = 0;

//**********************************************************
//      Persional Function Statement
//**********************************************************

static void initSendBuff(void);
static void initMasterClient(void);
static void initWorkerList(void);
static m_status tryConnectMaster(void);
static m_status trySingleMasterConnect(struct sockaddr_in* ip);
static int setTcpKeepAlive(int fd, int start, int interval, int count);
static void requestMaster(char* removeIP_port);
static void updateMaxfd(int currfd);
static int resetFdset(struct buffer_s* data);
static int resetMasterFdSet(int curfd);
static int resetWorkerFdset(int curfd, struct buffer_s* data);
static void dealWithMasterSet(void);
static int dealWithWorkerSet(struct buffer_s* data);
static void dealWithMasterData(char* masterData, int len);
static void updateWorkerList(char* workerList);
static void updateRule(char* key, char* value);
static int resetWorkListSpace(int needNum);
static void addNewConnect(char* ip, int index);
static void deleExistConnect(int index);
static void updateActiveClientfd(void);
static void retryConnectMaster(void);
static void removeWorker(int activeIdx);
static int createSocket(void);

//**********************************************************
//      Global Function Implement
//**********************************************************

void initClient(void) {
    printf("Start to run receiver component....\n");
    initSendBuff();
    initMasterClient();
    initWorkerList();
    setTcpKeepAlive(masterStatus.m_clientfd, 10, 10, 5);
    signal(SIGPIPE, SIG_IGN);
    requestMaster(NULL);
}

int runClient(struct buffer_s* data) {
    int result = 0;
    int maxfd = resetFdset(data);
    int no = select(maxfd, &readSet, &writeSet, NULL, &select_tm);
    switch (no) {
        case -1:
            printf("The receiver select error!! %s ...\n", strerror(errno));
            break;
        case 0:
            if (masterStatus.status == retry || masterStatus.status == connecting) {
                retryConnectMaster();
            }
            break;
        default:
            dealWithMasterSet();
            result = dealWithWorkerSet(data);
    }
    return result;
}

#ifdef TEST

struct buffer_s* fillNetflowData(testData* eth_hdr) {
    // TODO here we will get the netflow data
    if (!eth_hdr) {
        return NULL;
    }

    //  data format :
    // totalLen(1int) + ipflage(1byte) + ipAddress(4|16Byte) + netflowData(nByte)
    ushort payload_len = (ushort) eth_hdr->length;
    ushort ipLen = 4;
    // totalLen + char + ipLen + payLoad
    ushort totalLen = sizeof (ushort) + sizeof (char) +ipLen + payload_len;

    if (sendbuffer.buffMaxLen < totalLen) {
        free(sendbuffer.buff);
        sendbuffer.buff = (char*) malloc(totalLen);
        if (sendbuffer.buff == NULL) {
            return NULL;
        }
        sendbuffer.buffMaxLen = totalLen;
    }

    char* p = sendbuffer.buff;
    memcpy(p, &totalLen, sizeof (short));
    p = p + sizeof (short);

    *p = (char) ipLen;
    p++;

    unsigned char ip[4] = {192, 168, 1, 1};
    memcpy(p, &ip, ipLen); // src IP
    p = p + ipLen;

    memcpy(p, eth_hdr->data, payload_len);
    sendbuffer.bufflen = totalLen;
    return &sendbuffer;
}
#else

struct buffer_s* fillNetflowData(struct ether_hdr* eth_hdr) {
    // TODO here we will get the netflow data
    if (!eth_hdr) {
        return NULL;
    }
    struct ipv4_hdr *ip_hdr = (struct ipv4_hdr *) (eth_hdr + 1);
    uint8_t ip_len = (ip_hdr->version_ihl & 0xf) * 4;
    struct udp_hdr* u_hdr = (struct udp_hdr *) ((u_char *) ip_hdr + ip_len);
    uint16_t payload_shift = sizeof (struct udp_hdr);
    uint16_t payload_len = udp_hdr->dgram_len;
    u_char *the5Record = (u_char *) u_hdr + payload_shift;
    //  data format :
    // totalLen(1int) + ipflage(1byte) + ipAddress(4|16Byte) + netflowData(nByte)
    short ipLen = (ip_hdr->version_ihl >> 4) == 4 ? 4 : 16;
    short totalLen = sizeof (short) + sizeof (char) +ipLen + (short) payload_len;
    if (buffer.buffMaxLen < totalLen) {
        free(buffer.buff);
        buffer.buff = (char*) malloc(totalLen);
        if (buffer.buff == NULL) {
            return NULL;
        }
        buffer.buffMaxLen = totalLen;
    }

    char* p = buffer.buff;
    memcpy(p, &totalLen, sizeof (short));
    p = p + sizeof (short);

    *p = (char) ipLen;
    p++;

    memcpy(p, (void *) &src_ip, ipLen); // src IP
    p = p + ipLen;

    memcpy(p, the5Record, payload_len);
    buffer.bufflen = totalLen;
    return &buffer;
}

#endif

//**********************************************************
//      Persional Function Implement
//**********************************************************

static void initSendBuff(void) {

    // init sendbuffer
    sendbuffer.buff = (char*) malloc(sizeof (char) * DEFAULT_BUFFER);
    sendbuffer.buffMaxLen = DEFAULT_BUFFER;
    sendbuffer.bufflen = 0;

}

static void initMasterClient(void) {
    printf("Try to connect with master client.\n");
    if (tryConnectMaster() != connected) {
        printf("There is no master to connect, the receiver will quit.\n");
        exit(-1);
    } else {
        printf("Master connected!\n");
    }
}

 void initWorkerList() {

    worker_list.baseIncNum = DEFAULT_WORKER_BASE_NUM;
    int defaultNum = worker_list.baseIncNum;
    worker_list.workerList = (char**) malloc(sizeof (char*) * defaultNum);
    memset(worker_list.workerList, 0, sizeof (char*)*defaultNum);

    worker_list.c_clientfd = (int*) malloc(sizeof (int)* defaultNum);
    worker_list.workerIP = (struct sockaddr_in*) malloc(sizeof (struct sockaddr_in)* defaultNum);
    worker_list.workersStatus = (m_status*) malloc(sizeof (m_status) * defaultNum);

    worker_list.maxNum = defaultNum;
    worker_list.activeNum = 0;
}

/*
 * try connect woth whole master
 *
 * Return:  m-status 
 *              failed:
 *              connected:
 */
static m_status tryConnectMaster(void) {
    int maxRetryNum = netflowConf.totalMaxTryNum;
    printf("Max retry number is %d times.\n", maxRetryNum);
    printf("Registed %d masters.\n", masterList.masterNum);
    //LogWrite(INFO, "Max retry number is %d times.", maxRetryNum);
    //LogWrite(INFO, "Registed %d masters.", masterList.masterNum);

    int tryidx = 0;
    while (tryidx != maxRetryNum) {
        printf("\nRetry %d times to connect with whole master!\n", tryidx + 1);
        //LogWrite(INFO,"Retry %d times to connect with whole master!", tryidx+1);

        // connect with all master
        int masteridx = 0;
        while (masteridx != masterList.masterNum) {

            struct sockaddr_in* ip = masterList.masterIP + masteridx;
            printf("Try to connect with %s:%d\n", inet_ntoa(ip->sin_addr), ntohs(ip->sin_port));
            //LogWrite(INFO,"try to connect with %s:%d", inet_ntoa(ip->sin_addr), ntohs(ip->sin_port));

            m_status st = trySingleMasterConnect(ip);
            if (st == connected) {
                masterStatus.status = connected;
                return connected;
            } else {
                masteridx++;
            }
        }
        tryidx++;
    }
    masterStatus.status = failed;
    return failed; // no Master to connect
}

/*
 * try connect with a single master
 * 
 * Return:  m-status 
 *              failed:
 *              connected:
 *              fd_error:
 */
static m_status trySingleMasterConnect(struct sockaddr_in* ip) {

    masterStatus.m_clientfd = createSocket();
    if (masterStatus.m_clientfd == -1) {
        //LogWrite(ERROR, "Init master client's socket error, %s", strerror(errno));
        printf("Init master client's socket error, %s\n", strerror(errno));
        return fd_error;
    }

    int res = connect(masterStatus.m_clientfd, (struct sockaddr*) ip, sizeof (struct sockaddr));

    if (res == 0) {
        printf("Connected, Master is %s\n", inet_ntoa(ip->sin_addr));
        //LogWrite(INFO, "Connected, Master is %s", inet_ntoa(ip->sin_addr));       
        return connected;
    } else {
        if (errno != EINPROGRESS) {
            printf("Connect failed. %s\n", strerror(errno));
            //LogWrite(INFO, "Connect failed. %s", strerror(errno));
            close(masterStatus.m_clientfd);
            return failed;
        }
    }

    struct timeval tv;
    tv.tv_sec = netflowConf.singleWaitSecond;
    tv.tv_usec = 0;

    errno = 0;
    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);
    FD_SET(masterStatus.m_clientfd, &readSet);
    writeSet = readSet;

    res = select(masterStatus.m_clientfd + 1, &readSet, &writeSet, NULL, &tv);
    switch (res) {
        case -1:
            //LogWrite(ERROR, "Select error in connect ... %s",strerror(errno)); 
            printf("Select error in connect ... %s\n", strerror(errno));
            close(masterStatus.m_clientfd);
            return failed;

        case 0:
            printf("Select time out.\n");
            //LogWrite(INFO, "Select time out.");   
            close(masterStatus.m_clientfd);
            return failed;

        default:
            if (FD_ISSET(masterStatus.m_clientfd, &readSet) || FD_ISSET(masterStatus.m_clientfd, &writeSet)) {
                int no = connect(masterStatus.m_clientfd, (struct sockaddr*) ip, sizeof (struct sockaddr));
                if (no == 0) {
                    //LogWrite(INFO, "Connected, Master is %s", inet_ntoa(ip->sin_addr));
                    printf("Connected, Master is %s\n", inet_ntoa(ip->sin_addr));
                    return connected;
                } else {
                    int err = errno;
                    if (err == EISCONN) {
                        //LogWrite(INFO, "Connected, Master is %s", inet_ntoa(ip->sin_addr));
                        printf("Connected, Master is %s\n", inet_ntoa(ip->sin_addr));
                        return connected;
                    } else {
                        //LogWrite(INFO, "Connect failed! %s %d", strerror(err), err);
                        printf("Connect failed! %s %d\n", strerror(err), err);
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
static int setTcpKeepAlive(int fd, int start, int interval, int count) {
    int keepAlive = 1;
    if (fd < 0 || start < 0 || interval < 0 || count < 0) return;

    // set keep alive
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void*) &keepAlive, sizeof (keepAlive)) == -1) {
        perror("setsockopt");
        return -1;
    }

    // 启用心跳机制开始到首次心跳侦测包发送之间的空闲时间   
    if (setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, (void *) &start, sizeof (start)) == -1) {
        perror("setsockopt");
        return -1;
    }
    //两次心跳侦测包之间的间隔时间   
    if (setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, (void *) &interval, sizeof (interval)) == -1) {
        perror("setsockopt");
        return -1;
    }
    //探测次数，即将几次探测失败判定为TCP断开   
    if (setsockopt(fd, SOL_TCP, TCP_KEEPCNT, (void *) &count, sizeof (count)) == -1) {
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
static void requestMaster(char* removeIP_port) {

    requestWorkerIPs(removeIP_port, masterStatus.sendBuff, &masterStatus.sendLen);
    
    // copy the length
    memcpy(masterStatus.sendBuff, &masterStatus.sendLen, sizeof (unsigned short));
    masterStatus.report = on;
}

static int resetFdset(struct buffer_s* data) {
    int fd = 0;
    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);
    fd = resetMasterFdSet(fd);
    fd = resetWorkerFdset(fd, data);

    // init select_time
    select_tm.tv_sec = netflowConf.receiverWaitSecond;
    return fd + 1;
}

static int resetMasterFdSet(int curfd) {
    // Focus on readSet, to get the control information from master.
    // If we need to send the data to master, we should register in writeSet

    if (masterStatus.status == connected
            || masterStatus.status == retry
            || masterStatus.status == connecting) {
        //LogWrite(DEBUG,"FD_SET-master_fd(%d)-readSet",masterStatus.m_clientfd);
        FD_SET(masterStatus.m_clientfd, &readSet);

        if (masterStatus.report == on
                || masterStatus.status == retry
                || masterStatus.status == connecting) {
            //LogWrite(DEBUG,"FD_SET-master_fd(%d)-writeSet",masterStatus.m_clientfd);
            FD_SET(masterStatus.m_clientfd, &writeSet);
        }
        return ( curfd > masterStatus.m_clientfd) ? curfd : masterStatus.m_clientfd;
    }
    return curfd;
}

static int resetWorkerFdset(int curfd, struct buffer_s* data) {
    // Focus on readset & writeset. 
    // ReadSet only for get the disconnect information, and WriteSet for write data.
    int maxfd = curfd;
    int i;
    for (i = 0; i != worker_list.activeNum; i++) {
        int idx = worker_list.active_to_idx[i];
        int fd = worker_list.c_clientfd[idx];
        FD_SET(fd, &readSet);
        if (data != NULL) {
            FD_SET(fd, &writeSet);
        }
        maxfd = maxfd > fd ? maxfd : fd;
    }
    return maxfd;
}

static void dealWithMasterSet(void) {

    if (FD_ISSET(masterStatus.m_clientfd, &readSet)) {
        //LogWrite(DEBUG,"Deal with command data from master(readSet).");

        switch (masterStatus.status) {
            case connected:
                memset(masterStatus.recvBuff, 0, sizeof (masterStatus.recvBuff));

                unsigned short totalLen;
                int readCount = recv(masterStatus.m_clientfd,
                        &totalLen, sizeof(unsigned short),0);
                totalLen = ntohs(totalLen) -2;
                
                int curNo = recv(masterStatus.m_clientfd, masterStatus.recvBuff, totalLen, 0 ) ;
                while( curNo != totalLen){
                     if (curNo == -1 || curNo == 0) { // failed connection          
                        printf("Disconnect with master[error: %s]，and will retry to connect with master.\n",
                                strerror(errno));
                        close(masterStatus.m_clientfd);
                        masterStatus.status = retry;

                        masterStatus.retryTimes = 0;
                        masterStatus.retryMasteridx = 0;
                        retryConnectMaster();
                        break;
                    }else {
                        curNo += recv(masterStatus.m_clientfd, masterStatus.recvBuff+curNo, totalLen - curNo, 0 ) ;
                    }
                }
               dealWithMasterData(masterStatus.recvBuff, totalLen);
                break;

            case retry:
            case connecting:
                retryConnectMaster();
                break;
            default:
                printf("There is no available master to connect with.\n");
        }
    }

    // here we will deal with sending report and request to master
    if (FD_ISSET(masterStatus.m_clientfd, &writeSet)) {

        switch (masterStatus.status) {
            case retry:
            case connecting:
                retryConnectMaster();
                break;
            case connected:
            {
                int countNum =
                        send(masterStatus.m_clientfd, masterStatus.sendBuff, masterStatus.sendLen, 0);
                char* bp = masterStatus.sendBuff;
                while (countNum != masterStatus.sendLen) {
                    if (countNum == -1) {
                        printf("Connect master error. %s\n", strerror(errno));
                        close(masterStatus.m_clientfd);
                        masterStatus.status = retry;
                        retryConnectMaster();
                    } else {
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

static int dealWithWorkerSet(struct buffer_s* data) {

    int result = 0;
    if(worker_list.activeNum == 0 ) return 0;
    worker_idx = worker_idx % worker_list.activeNum;

    int idx = worker_list.active_to_idx[worker_idx];
    int fd = worker_list.c_clientfd[idx];

    /* remove the disconnect socket */
    if (FD_ISSET(fd, &readSet)) {
        int readCount = recv(fd, masterStatus.recvBuff, sizeof (masterStatus.recvBuff),
                MSG_WAITALL | MSG_DONTWAIT);

        if (readCount == -1 || readCount == 0) {
            printf("Disconnect with worker %s\n", worker_list.workerList[idx]);
            FD_CLR(fd, &writeSet);
            shutdown(fd, SHUT_WR);
            close(fd);
            
            // If there is only one worker connected with this receiver, we must request a new worker.
            // If there is more than one workers, ignore the request.
            if (worker_list.activeNum == 1) {
                requestMaster(worker_list.workerList[idx]);
                printf("There is no available worker to send netflow data to.  "
                        "So request worker list to master.\n");
            }
            removeWorker(worker_idx);
        }
    }

    /* write the netflow package to worker*/
    if (FD_ISSET(fd, &writeSet)) {
        int no = send(fd, data->buff, data->bufflen, 0);

        while (no != data->bufflen) {
            if (no == -1) {
                printf("Send %s data error.\n", inet_ntoa(worker_list.workerIP[idx].sin_addr));
                break;
            } else {
                no += send(fd, data->buff + no, (size_t) (data->bufflen - no), 0);
            }
        }
        result = 1;
    }

    worker_idx++;
    return result;
}

/* analysis the data from master, and deal with the data*/
/**
 * 
 *workerlistMsg  mode :
 *      $$1&+2;ip1:port;ip2:port     --> add new ip's address
 *      $$1&-1;ip1:port                      --> dele exist ip's address
 *      $$1&+2;ip1:port;ip2:port&-3;1p3:port;ip4:port  -->add and delete
 * 
 * ruleMsg mode:
 *      $$2&ip1:1;ip2:1;ip3:2&ipA,ipB
 * @param masterData
 * @param len
 */
void dealWithMasterData(char* masterData, int len) {

    int totalNum = 0;
    char* data = masterData;
    data = del_left_trim(data);
    int* pos = getGroupDataPos(data, &totalNum);
    int* ppos = pos;
    if(pos == NULL){
        return;
    }
    int type = atoi(data+(*ppos++));
    int curNum = 1;    //  skip type
    switch(type){
        case workerlistMsg:
            while(curNum < totalNum){
                updateWorkerList(data + (*ppos++));
                curNum ++;
            }
            break;
        case ruleMsg:
            updateRule(data+pos[1], data+pos[2]);
            break;
        default:
            printf("Unknow message type.\n");
    }
    free(pos);
}

static void updateRule(char* key, char* value){
    printf("%s-->%s",key,value);
}

/* 
 * Function:
 *  update the  worker_list
 *    mode :
 *      +2;ip1:port;ip2:port     --> add new ip's address
 *      -1;ip1:port                      --> dele exist ip's address
 * Param:
 *  workerList : the data from master 
 *  len: the data's length
 *
 * Return:
 *  void
 */
static void updateWorkerList(char* workerList) {

    int totalNum = 0;
    int* pos = getInnerDataPos(workerList, &totalNum);
    int* p = pos;
    int len = strlen(workerList);
    
    char mode = *(workerList + (*p));
    char num = atoi(workerList + (*p++) + 1);
    if( num == 0) return;
    int curNum = 1;     // skip mode
    
    switch(mode){
        case '+':{
             /* update the maxNum */
            if (resetWorkListSpace(num) != 0) return;        
            while(curNum < totalNum){
                int i;
                char* ip_port = workerList + (*p++);
            
                //check if exist in current workerlist
                if (worker_list.active_to_idx != NULL) {
                    for (i = 0; i < worker_list.activeNum; i++) {
                        int idx = worker_list.active_to_idx[i];
                        if (strcasecmp(ip_port, worker_list.workerList[idx]) == 0) {
                            break;
                        }
                    }
                    // exist in current worker_list
                    if (i != worker_list.activeNum) {
                        continue; 
                    }
                }

                // add new ip into worker_list
                for (i = 0; i < worker_list.maxNum; i++) {
                    if (worker_list.workerList[i] == NULL) { // find place to insert into
                        addNewConnect(ip_port, i);
                        break;
                    }
                }
                curNum++;
            }
            updateActiveClientfd();
        }
        break;

        case '-':{
            int maxNum =  worker_list.activeNum;
            while(curNum < totalNum){
                char* ip_port = workerList + (*p++);               
                int i = 0;
                for (; i < maxNum; i++) {
                    int idx = worker_list.active_to_idx[i];
                    if(worker_list.workerList[idx] == NULL) continue;
                    if (strncasecmp(ip_port, worker_list.workerList[idx], strlen(ip_port)) == 0) {
                        deleExistConnect(idx);
                        printf("delete exist ip %s\n",ip_port);
                        break;
                    }
                }
                curNum ++;
            }
            updateActiveClientfd();
            
            // check the current worker list
            if(worker_list.activeNum == 0){
                requestMaster(NULL);
            }
        }       
        break;
        
        default:
            printf("Unknow format\n");
    }
    free(pos);
}

static int resetWorkListSpace(int needNum) {
    if (needNum > worker_list.maxNum - worker_list.activeNum) {
        int realNeedNum = needNum - (worker_list.maxNum - worker_list.activeNum);
        int adjustSize = worker_list.baseIncNum > realNeedNum ? worker_list.baseIncNum : realNeedNum;
        int newSize = worker_list.maxNum + adjustSize;

        worker_list.workerList = (char**) realloc(worker_list.workerList, newSize);
        worker_list.c_clientfd = (int*) realloc(worker_list.c_clientfd, newSize);
        worker_list.workerIP = (struct sockaddr_in*) realloc(worker_list.workerIP, newSize);
        if (worker_list.workerList == NULL || worker_list.c_clientfd == NULL || worker_list.workerIP == NULL) {
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
static void addNewConnect(char* ip_port, int index) {
    worker_list.workerList[index] = strdup(ip_port);
    char* d = strchr(ip_port, ':');
    *d = '\0';
    int port = atoi(d + 1);

    setAddress(worker_list.workerIP + index, ip_port, port);

    worker_list.c_clientfd[index] = createSocket();
    if (worker_list.c_clientfd[index] == -1) {
        return;
    }
    connect(worker_list.c_clientfd[index], (struct sockaddr *) (worker_list.workerIP + index),
            sizeof (struct sockaddr));
    printf("Add the workerIP, fd=%d ip=%s:%d\n", index, ip_port, port);
    worker_list.activeNum++;
    worker_list.workersStatus[index] = connecting;
}

/**
 * 
 * @param index point to worker_list.workerList
 */
static void deleExistConnect(int index) {
    free(worker_list.workerList [index]); //free the char* point
    worker_list.workerList[index] = NULL;
    shutdown(worker_list.c_clientfd[index], SHUT_RDWR); //close socket
    close(worker_list.c_clientfd[index]);
    worker_list.activeNum--;
    worker_list.workersStatus[index] = closed;
}

static void updateActiveClientfd(void) {
    //TODO: here should be more effective
    free(worker_list.active_to_idx);
    worker_list.active_to_idx = (int*) malloc(sizeof (int) * worker_list.activeNum);

    int active_idx = 0;
    int i = 0;
    while (i != worker_list.maxNum) {
        if (worker_list.workerList[i] != NULL) {
            // exist ip
            worker_list.active_to_idx[active_idx] = i;
            active_idx++;
            if (active_idx > worker_list.activeNum) {
                return;
            }
        }
        i++;
    }
}

//----------------------- disconnected and retry ---------------------------------------------

/* disconnected and retry  */
static void retryConnectMaster(void) {
    if (masterStatus.status == retry) {
        masterStatus.m_clientfd = createSocket();
        if (masterStatus.m_clientfd == -1) {
            printf("Init master client's socket error, %s\n", strerror(errno));
            masterStatus.status = fd_error;
        } else {
            masterStatus.status = connecting;
        }
    }

    struct sockaddr_in* ip = masterList.masterIP + masterStatus.retryMasteridx;
    int res = connect(masterStatus.m_clientfd, (struct sockaddr *) ip, sizeof (struct sockaddr));
    if (res == 0) {
        printf("Connected immediately. Master is %s, res=0\n", inet_ntoa(ip->sin_addr));
        masterStatus.status = connected;
        requestMaster(NULL);
        return;
    }

    int err = errno;
    switch (err) {
        case EISCONN:
            printf("Connected. Master is %s\n", inet_ntoa(ip->sin_addr));
            masterStatus.status = connected;
            requestMaster(NULL);
            break;
        case EINPROGRESS:
            printf("Connecting %s ...error %s\n", inet_ntoa(ip->sin_addr), strerror(errno));
            masterStatus.status = connecting;
            break;
        case EALREADY:
            printf("Connecting %s, errno = %s, %d\n ", inet_ntoa(ip->sin_addr), strerror(errno), errno);
            masterStatus.status = connecting;
            break;
        default:
            close(masterStatus.m_clientfd); // close current fd.
            masterStatus.retryMasteridx++;
            
            if (masterStatus.retryMasteridx == masterList.masterNum) {
                printf("Connect %s failed. errno = %d . %s. \n",
                        inet_ntoa(ip->sin_addr), errno, strerror(errno));
                masterStatus.retryMasteridx = 0;
                masterStatus.retryTimes++;

                if (masterStatus.retryTimes == netflowConf.totalMaxTryNum) {
                    printf("There is no aliving LoadMaster to connect with.\n");
                    masterStatus.status = failed;
                } else {
                    printf("Try %dth times to connect LoadMaster.\n", masterStatus.retryTimes);
                    masterStatus.status = retry;
                }
            } else {
                printf("Connect %s failed. Errno = %d. %s, change another LoadMaster.\n ",
                        inet_ntoa(ip->sin_addr), errno, strerror(errno));
                masterStatus.status = retry;
            }
    }
}

/* remove dead worker from active worker list */
static void removeWorker(int dead_active_Idx) {
    if (dead_active_Idx >= worker_list.activeNum) {
        return;
    }
    int id = worker_list.active_to_idx[dead_active_Idx];
    close(worker_list.c_clientfd[id]);

    free(worker_list.workerList[id]);
    worker_list.workerList[id] = NULL;
    worker_list.activeNum--;

    int i = dead_active_Idx;
    while (i != worker_list.activeNum) {
        worker_list.workerList[i] = worker_list.workerList[i + 1];
        i++;
    }
}

static int createSocket(void) {
    int tcpfd = socket(AF_INET, SOCK_STREAM, 0);
    int flags = fcntl(tcpfd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    fcntl(tcpfd, F_SETFL, flags | O_NONBLOCK);
    return tcpfd;
}
