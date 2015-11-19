#include "receiver.h"

#include <stdio.h> 
#include <string.h>
#include <pthread.h>
#include <stdlib.h>

unsigned nb_rx_curidx=0;
uint16_t nb_rx_total=0;
struct rte_mbuf *bufs[BURST_SIZE];

struct ether_hdr* getData(){
    if( nb_rx_curidx == nb_rx_total){
        nb_rx_total = rte_ring_dequeue_burst(db_ring,(void *)bufs, BURST_SIZE);
         if (unlikely(nb_rx_total == 0)) {
            return NULL;
        }
        nb_rx_curidx= 0;
    }
    struct rte_mbuf *m = bufs[nb_rx_curidx++];
    struct ether_hdr *eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
    return eth_hdr;
}


// args 192.168.1.2:1121 192.168.1.2:1120
int main( int argc, char ** args ){
    if(argc < 2){
        printf("Ip should not be empty\n");
        return -1;
    }
    char temp[1024];
    memset(temp,0,1204);
    char *p = temp;
    int idx=0;
    strcpy(p,"$$+");      // $$+
    idx = 3;
    sprintf(p+3,"%d",argc-1);      // number
    p = temp + strlen(temp);
    *p++ = '&';
        
    int i =1;
    for(; i< argc; i++){
        strcpy(p,args[i]);
        p += strlen(args[i]);
        *p++='&';
    }
    
   //  char* ip_port = "$$+2&192.168.1.2:10000&192.168.1.3:10001";
    
    initClient();
    updateWorkerList(temp, strlen(temp));
    
    while(!quit_signal) {
      
        struct ether_hdr* hdr = getData();
        struct buffer_s* sendDataBuf = fillNetflowData(hdr);
        runClient(sendDataBuf);
        
        //printf("locre %u forward %u packets to db\n", rte_lcore_id(), nb_rx);
//        unsigned i; 
//        for (i = 0; i < nb_rx; i ++) {
//            struct rte_mbuf *m = bufs[i];
//            struct ether_hdr *eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
//            
//            sendData(fillNetflowData(eth_hdr));           
//            struct ipv4_hdr *ip_hdr = (struct ipv4_hdr *)(eth_hdr + 1);
//            uint8_t ip_len = (ip_hdr->version_ihl & 0xf) * 4;                
//            struct udp_hdr* u_hdr = (struct udp_hdr *)((u_char *)ip_hdr + ip_len);
//            uint16_t payload_shift = sizeof(struct udp_hdr);
//            uint16_t payload_len = udp_hdr->dgram_len;
//            NetFlow5Record *5Record = (NetFlow5Record *)((u_char *)u_hdr + payload_shift);                    
            //quit_signal = forward_to_db(dst_ip, ether_hdr);
        }
    } 
}


