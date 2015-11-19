/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <sys/types.h>
#include <string.h>
#include <sys/queue.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/param.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_malloc.h>
#include <rte_debug.h>
#include <rte_distributor.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_string_fns.h>
#include <rte_lpm.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_fbk_hash.h>

#include "receiver.h"
#include "conf.h"

#define IPTOSBUFFERS 12

#define RX_RING_SIZE 256
#define TX_RING_SIZE 512*4
#define NUM_MBUFS ((512*1024)-1)
#define MBUF_SIZE (2048 + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
#define RTE_RING_SZ 8192

#define MAX_PORTS 16

#define	MCAST_CLONE_PORTS	2
#define	MCAST_CLONE_SEGS	2

#define	PKT_MBUF_SIZE	(2048 + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define	NB_PKT_MBUF	8192

#define	HDR_MBUF_SIZE	(sizeof(struct rte_mbuf) + 2 * RTE_PKTMBUF_HEADROOM)
#define	NB_HDR_MBUF	(NB_PKT_MBUF * MAX_PORTS)

#define	CLONE_MBUF_SIZE	(sizeof(struct rte_mbuf))
#define	NB_CLONE_MBUF	(NB_PKT_MBUF * MCAST_CLONE_PORTS * MCAST_CLONE_SEGS * 150)

/* allow max jumbo frame 9.5 KB */
#define	JUMBO_FRAME_MAX_SIZE	0x2600

/* uncommnet below line to enable debug logs */
#define DEBUG 

#ifdef DEBUG
#define LOG_LEVEL RTE_LOG_DEBUG
#define LOG_DEBUG(log_type, fmt, args...) do {	\
	RTE_LOG(DEBUG, log_type, fmt, ##args)		\
} while (0)
#else
#define LOG_LEVEL RTE_LOG_INFO
#define LOG_DEBUG(log_type, fmt, args...) do {} while (0)
#endif

#define RTE_LOGTYPE_DISTRAPP RTE_LOGTYPE_USER1

/* mask of enabled ports */
static uint32_t enabled_port_mask;
volatile uint8_t quit_signal;
volatile uint8_t quit_signal_rx;

static volatile struct app_stats {
	struct {
		uint64_t rx_pkts;
		uint64_t returned_pkts;
		uint64_t enqueued_pkts;
	} rx __rte_cache_aligned;

	struct {
		uint64_t dequeue_pkts;
		uint64_t tx_pkts;
	} tx __rte_cache_aligned;
} app_stats;

static const struct rte_eth_conf port_conf_default = {
	.rxmode = {
		.mq_mode = ETH_MQ_RX_RSS,
		.max_rx_pkt_len = ETHER_MAX_LEN,
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_hf = ETH_RSS_IP | ETH_RSS_UDP |
				ETH_RSS_TCP | ETH_RSS_SCTP,
		}
	},
};

struct output_buffer {
	unsigned count;
	struct rte_mbuf *mbufs[BURST_SIZE];
};

static struct rte_fbk_hash_params mcast_hash_params = {
	.name = "MCAST_HASH",
	.entries = 1024,
	.entries_per_bucket = 4,
	.socket_id = 0,
	.hash_func = NULL,
	.init_val = 0,
};

#define MAX_NB_MCAST    64

struct rte_fbk_hash_table *mcast_hash = NULL;

struct mcast_group_params {
	uint32_t router_id;
	uint16_t index;
};

static struct mcast_group_params mcast_group_table[] = {
		{IPv4(59,43,55,242), 0x0},
		{IPv4(172,16,51,8), 0x1},
                {((IPv4(59,43,55,242) & 0xffffffff) << 16) | ((156) & 0xffff), 0x2},
                {((IPv4(172,16,51,8) & 0xffffffff) << 16) | ((155) & 0xffff), 0x3}
};  

#define N_MCAST_GROUPS \
	(sizeof (mcast_group_table) / sizeof (mcast_group_table[0]))


struct mcast_rule_params {
    uint32_t dest_ip;
    //can add more params
};

static struct mcast_rule_params mcast_rule_table[] = {
    {IPv4(192,168,80,55)}, 
    {IPv4(192,168,80,66)}
};

#define DB_LCORE    5

/*
 * Initialises a given port using global settings and with the rx buffers
 * coming from the mbuf_pool passed as parameter
 */
static inline int
port_init(uint8_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf = port_conf_default;
	const uint16_t rxRings = 1, txRings = 1;
	int retval;
	uint16_t q;
        
        printf("nb of lcore: %u\n", txRings + 1);        
	if (port >= rte_eth_dev_count())
		return -1;
        
	retval = rte_eth_dev_configure(port, rxRings, txRings, &port_conf);
	if (retval != 0)
		return retval;
        
	for (q = 0; q < rxRings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, RX_RING_SIZE,
						rte_eth_dev_socket_id(port),
						NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}
       
	for (q = 0; q < txRings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, TX_RING_SIZE,
						rte_eth_dev_socket_id(port),
						NULL);
		if (retval < 0)
			return retval;
	}
     
	retval = rte_eth_dev_start(port);
	if (retval < 0)
		return retval;
      
        /*
	struct rte_eth_link link;
	rte_eth_link_get_nowait(port, &link);
	if (!link.link_status) {
		sleep(1);
		rte_eth_link_get_nowait(port, &link);
	}

	if (!link.link_status) {
		printf("Link down on port %"PRIu8"\n", port);
		return 0;
	}
        */
        
	struct ether_addr addr;
	rte_eth_macaddr_get(port, &addr);
	printf("Port %u MAC: %02"PRIx8" %02"PRIx8" %02"PRIx8
			" %02"PRIx8" %02"PRIx8" %02"PRIx8"\n",
			(unsigned)port,
			addr.addr_bytes[0], addr.addr_bytes[1],
			addr.addr_bytes[2], addr.addr_bytes[3],
			addr.addr_bytes[4], addr.addr_bytes[5]);

	rte_eth_promiscuous_enable(port);
                
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
        uint8_t count, port_up, print_flag = 0;
        
        struct rte_eth_link link;
        printf("\nChecking link status");
	fflush(stdout);
        for (count = 0; count <= MAX_CHECK_TIME; count++) 
        {   
            port_up = 1;
            memset(&link, 0, sizeof(link));
            rte_eth_link_get_nowait(port, &link);
            if (print_flag == 1) 
            {
                if (link.link_status)
                    printf("Port %d Link Up - speed %u "
                            "Mbps - %s\n", (uint8_t)port,
                            (unsigned)link.link_speed,
                (link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
                    ("full-duplex") : ("half-duplex\n"));
                else
                    printf("Port %d Link Down\n",
                            (uint8_t)port);
                continue;
            }
            
            /* clear all_ports_up flag if any link down */
            if (link.link_status == 0)             
                port_up = 0;                
            
            if (print_flag == 1)
                break;
            
            if (port_up == 0)
            {
                printf(".");
                fflush(stdout);
                rte_delay_ms(CHECK_INTERVAL);
            }
            
            if (port_up == 1|| count == (MAX_CHECK_TIME - 1))
            {
                print_flag = 1;
                printf("done\n");
            }
            
        }
        
	return 0;
}

struct lcore_params {
	unsigned worker_id;
	struct rte_distributor *d;
	struct rte_ring *r;
	struct rte_mempool *mem_pool;
        uint8_t portid;
};

/* list of enabled ports */
static uint32_t l2fwd_dst_ports[RTE_MAX_ETHPORTS];

#define TIMER_MILLISECOND 2000000ULL /* around 1ms at 2 Ghz */
#define MAX_TIMER_PERIOD 86400 /* 1 day max */
static int64_t timer_period = 10 * TIMER_MILLISECOND * 1000; /* default period is 10 seconds */
#define BURST_TX_DRAIN_US 100 /* TX drain every ~100us */

#define MAX_RX_QUEUE_PER_LCORE 16
#define MAX_TX_QUEUE_PER_LCORE 16
#define MAX_TX_QUEUE_PER_PORT 16

struct lcore_queue_conf {
	unsigned n_rx_port;
        unsigned n_tx_port;
        unsigned tx_port_list[MAX_TX_QUEUE_PER_LCORE];
	unsigned rx_port_list[MAX_RX_QUEUE_PER_LCORE];
	
} __rte_cache_aligned;

struct lcore_queue_conf lcore_queue_conf[RTE_MAX_LCORE];

static unsigned int l2fwd_rx_queue_per_lcore = 1;
volatile unsigned int dist = 0;

#define CONFIG_RTE_LIBRTE_RING_DEBUG

struct rte_mempool *mbuf_pool;
struct rte_mempool *header_pool, *clone_pool, *cpy_pool;
struct rte_distributor *d[RTE_MAX_ETHPORTS];
struct rte_ring *output_ring[RTE_MAX_ETHPORTS];
struct rte_ring *db_ring;

/*netflow structure defin*/

#define FLOW_VERSION_5		        5
#define DEFAULT_V5FLOWS_PER_PACKET	30

struct flow_ver5_hdr {
  u_int16_t version;         /* Current version=5*/
  u_int16_t count;           /* The number of records in PDU. */
  u_int32_t sysUptime;       /* Current time in msecs since router booted */
  u_int32_t unix_secs;       /* Current seconds since 0000 UTC 1970 */
  u_int32_t unix_nsecs;      /* Residual nanoseconds since 0000 UTC 1970 */
  u_int32_t flow_sequence;   /* Sequence number of total flows seen */
  u_int8_t  engine_type;     /* Type of flow switching engine (RP,VIP,etc.)*/
  u_int8_t  engine_id;       /* Slot number of the flow switching engine */
  u_int16_t sampleRate;      /* Packet capture sample rate */
};

struct flow_ver5_rec {
  u_int32_t srcaddr;    /* Source IP Address */
  u_int32_t dstaddr;    /* Destination IP Address */
  u_int32_t nexthop;    /* Next hop router's IP Address */
  u_int16_t input;      /* Input interface index */
  u_int16_t output;     /* Output interface index */
  u_int32_t dPkts;      /* Packets sent in Duration (milliseconds between 1st
			   & last packet in this flow)*/
  u_int32_t dOctets;    /* Octets sent in Duration (milliseconds between 1st
			   & last packet in  this flow)*/
  u_int32_t first;      /* SysUptime at start of flow */
  u_int32_t last;       /* and of last packet of the flow */
  u_int16_t srcport;    /* TCP/UDP source port number (.e.g, FTP, Telnet, etc.,or equivalent) */
  u_int16_t dstport;    /* TCP/UDP destination port number (.e.g, FTP, Telnet, etc.,or equivalent) */
  u_int8_t pad1;        /* pad to word boundary */
  u_int8_t tcp_flags;   /* Cumulative OR of tcp flags */
  u_int8_t proto;        /* IP protocol, e.g., 6=TCP, 17=UDP, etc... */
  u_int8_t tos;         /* IP Type-of-Service */
  u_int16_t src_as;     /* source peer/origin Autonomous System */
  u_int16_t dst_as;     /* dst peer/origin Autonomous System */
  u_int8_t src_mask;    /* source route's mask bits */
  u_int8_t dst_mask;    /* destination route's mask bits */
  u_int16_t pad2;       /* pad to word boundary */
};

typedef struct single_flow_ver5_rec {
  struct flow_ver5_hdr flowHeader;
  struct flow_ver5_rec flowRecord[DEFAULT_V5FLOWS_PER_PACKET+1 /* safe against buffer overflows */];
} NetFlow5Record;

#define MAX_PACKET_LEN   256

typedef struct ipAddress {
  u_int8_t ipVersion:3 /* Either 4 or 6 */,
    localHost:1, /* -L: filled up during export not before (see exportBucket()) */
    notUsed:4 /* Future use */;

  union {
    struct in6_addr ipv6;
    u_int32_t ipv4; /* Host byte code */
  } ipType;
} IpAddress;

struct generic_netflow_record {
  /* v5 */
  IpAddress srcaddr;    /* Source IP Address */
  IpAddress dstaddr;    /* Destination IP Address */
  IpAddress nexthop;    /* Next hop router's IP Address */
  u_int16_t input;      /* Input interface index */
  u_int16_t output;     /* Output interface index */
  u_int32_t sentPkts, rcvdPkts;
  u_int32_t sentOctets, rcvdOctets;
  u_int32_t first;      /* SysUptime at start of flow */
  u_int32_t last;       /* and of last packet of the flow */
  u_int16_t srcport;    /* TCP/UDP source port number (.e.g, FTP, Telnet, etc.,or equivalent) */
  u_int16_t dstport;    /* TCP/UDP destination port number (.e.g, FTP, Telnet, etc.,or equivalent) */
  u_int8_t  tcp_flags;  /* Cumulative OR of tcp flags */
  u_int8_t  proto;      /* IP protocol, e.g., 6=TCP, 17=UDP, etc... */
  u_int8_t  tos;        /* IP Type-of-Service */
  u_int8_t  minTTL, maxTTL; /* IP Time-to-Live */
  u_int32_t dst_as;     /* dst peer/origin Autonomous System */
  u_int32_t src_as;     /* source peer/origin Autonomous System */
  u_int8_t  dst_mask;   /* destination route's mask bits */
  u_int8_t  src_mask;   /* source route's mask bits */

  /* v9 */
  u_int16_t vlanId, icmpType;

  /*
    Collected info: if 0 it means they have not been
    set so we use the nprobe default (-E)
  */
  u_int8_t engine_type, engine_id;

  /* IPFIX */
  u_int32_t firstEpoch, lastEpoch;

  struct {
    /* Latency extensions */
    u_int32_t nw_latency_sec, nw_latency_usec;

    /* VoIP Extensions */
    char sip_call_id[50], sip_calling_party[50], sip_called_party[50];
  } ntop;

  struct {
    u_int8_t hasSampling;
    u_int16_t packet_len /* 103 */, original_packet_len /* 312/242 */, packet_offset /* 102 */;
    u_int32_t samplingPopulation /* 310 */, observationPointId /* 300 */;
    u_int16_t selectorId /* 302 */;
    u_char packet[MAX_PACKET_LEN] /* 104 */;

    /* Cisco NBAR 2 */
    u_int32_t nbar2_application_id /* NBAR - 95 */;
  } cisco;

  struct {
    u_int32_t l7_application_id /* 3054.110/35932 */;
    char l7_application_name[64] /* 3054.111/35933 */, /* NOT HANDLED */
      src_ip_country[4] /* 3054.120/35942 */,
      src_ip_city[64] /* 3054.125/35947 */,
      dst_ip_country[4] /* 3054.140/35962 */,
      dst_ip_city[64] /* 3054.145/35967 */,
      os_device_name[64] /* 3054.161/35983 */; /* NOT HANDLED */
  } ixia;
};

static char *
iptos(uint32_t in)
{
	static char output[IPTOSBUFFERS][3*4+3+1];
	static short which;
	u_char *p;

	p = (u_char *)&in;
	which = (which + 1 == IPTOSBUFFERS ? 0 : which + 1);
	sprintf(output[which], "%d.%d.%d.%d", p[3], p[2], p[1], p[0]);
	return output[which];
}


static int
init_mcast_hash(void)
{
    uint32_t i;

    mcast_hash_params.socket_id = rte_socket_id();
    mcast_hash = rte_fbk_hash_create(&mcast_hash_params);
    if (mcast_hash == NULL) {
            return -1;
    }

    for (i = 0; i < N_MCAST_GROUPS; i ++) {
            if (rte_fbk_hash_add_key(mcast_hash,
                    mcast_group_table[i].router_id,
                    mcast_group_table[i].index) < 0) {
                    return -1;
            }
    }

    return 0;
}

static void 
deEndianRecord(struct generic_netflow_record *record) 
{
    record->last = ntohl(record->last), record->first = ntohl(record->first);

    if(record->srcaddr.ipVersion == 4) {
        record->srcaddr.ipType.ipv4 = ntohl(record->srcaddr.ipType.ipv4);
        record->dstaddr.ipType.ipv4 = ntohl(record->dstaddr.ipType.ipv4);
        record->nexthop.ipType.ipv4 = ntohl(record->nexthop.ipType.ipv4);
    }

    record->sentPkts = ntohl(record->sentPkts), record->rcvdPkts = ntohl(record->rcvdPkts);
    record->srcport = ntohs(record->srcport), record->dstport = ntohs(record->dstport);
    record->sentOctets = ntohl(record->sentOctets), record->rcvdOctets = ntohl(record->rcvdOctets);
    record->input = ntohs(record->input), record->output = ntohs(record->output);
    record->src_as = htonl(record->src_as), record->dst_as = htonl(record->dst_as);
    record->icmpType = ntohs(record->icmpType);
}


static void
quit_workers(struct rte_distributor *d, struct rte_mempool *p)
{
	const unsigned num_workers = rte_lcore_count() - 2;
	unsigned i;
	struct rte_mbuf *bufs[num_workers];
	rte_mempool_get_bulk(p, (void *)bufs, num_workers);

	for (i = 0; i < num_workers; i++)
		bufs[i]->hash.rss = i << 1;

	rte_distributor_process(d, bufs, num_workers);
	rte_mempool_put_bulk(p, (void *)bufs, num_workers);
}

static struct output_buffer tx_buffers[RTE_MAX_ETHPORTS];

static int
lcore_rx(struct lcore_params *p)
{
	struct rte_distributor *d = p->d;
	struct rte_mempool *mem_pool = p->mem_pool;
	struct rte_ring *r = p->r;
	uint8_t lcore_port = p->portid; 
        
        //struct rte_mbuf *buf;
        unsigned lcore_id = rte_lcore_id();
        //unsigned i;
        const uint8_t nb_ports = rte_eth_dev_count();
	const int socket_id = rte_socket_id();
	uint8_t port;
        
        if (lcore_id == 1)
           // return 0;
        
	for (port = 0; port < nb_ports; port++) {
		/* skip ports that are not enabled */
		if ((enabled_port_mask & (1 << port)) == 0)
			continue;

		if (rte_eth_dev_socket_id(port) > 0 &&
				rte_eth_dev_socket_id(port) != socket_id)
			printf("WARNING, port %u is on remote NUMA node to "
					"RX thread.\n\tPerformance will not "
					"be optimal.\n", port);
	}

	printf("\nCore %u doing packet RX. on Port %u\n", rte_lcore_id(), lcore_port);
	port = 0;
	while (!quit_signal_rx) {
                         
                port = lcore_port;
           
		/* skip ports that are not enabled */
		if ((enabled_port_mask & (1 << port)) == 0) {
			if (++port == nb_ports)
				port = 0;
			continue;
		}
            
                //printf("lcore port %u\n", port);
            
                struct rte_mbuf *bufs[BURST_SIZE*2];
		const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs,
				BURST_SIZE);
		app_stats.rx.rx_pkts += nb_rx;
                
                if (likely(nb_rx > 0))                    
                    rte_ring_enqueue_burst(db_ring, (void *)bufs, nb_rx);
                
                //printf("rx packets %u lcore_port: %u\n", nb_rx, port);       
                
		rte_distributor_process(d, bufs, nb_rx);
		const uint16_t nb_ret = rte_distributor_returned_pkts(d,
				bufs, BURST_SIZE*2);                                
		app_stats.rx.returned_pkts += nb_ret;
                
                //const uint16_t nb_ret = nb_rx;
                
                if (unlikely(nb_ret == 0))
                {                  
                    if (++port == nb_ports)
			port = 0;                 
                    continue;
                }
                
                /*                                               
                for (i = 0; i < nb_ret; i ++)
                {
                    buf = bufs[i];
                    rte_prefetch0(rte_pktmbuf_mtod(buf, void *));
                    buf->port = l2fwd_dst_ports[buf->port];
                }
                */
                
                //printf ("Returned %u packets on port %u\n", nb_ret, port);
                
		uint16_t sent = rte_ring_enqueue_burst(r, (void *)bufs, nb_ret);
		app_stats.rx.enqueued_pkts += sent;
		
                if (unlikely(sent < nb_ret)) 
                {
			//LOG_DEBUG(DISTRAPP, "%s:Packet loss due to full ring\n", __func__);
                    printf("%s:Packet loss due to full ring\n", __func__);
                        while (sent < nb_ret)
				rte_pktmbuf_free(bufs[sent++]);
		}
                
                //printf ("engueue %u packets on port %u\n", sent, port);
		if (++port == nb_ports)
			port = 0;
	}
        
        
        
	rte_distributor_process(d, NULL, 0);
	/* flush distributor to bring to known state */
	rte_distributor_flush(d);
	/* set worker & tx threads quit flag */
	quit_signal = 1;
	/*
	 * worker threads may hang in get packet as
	 * distributor process is not running, just make sure workers
	 * get packets till quit_signal is actually been
	 * received and they gracefully shutdown
	 */
	quit_workers(d, mem_pool);
	/* rx thread should quit at last */
        
        printf("received break signal\n");
	return 0;
}

static inline void
flush_one_port(struct output_buffer *outbuf, uint8_t outp)
{
	unsigned nb_tx = rte_eth_tx_burst(outp, 0, outbuf->mbufs,
			outbuf->count);
	app_stats.tx.tx_pkts += nb_tx;

	if (unlikely(nb_tx < outbuf->count)) {
		//LOG_DEBUG(DISTRAPP, "%s:Packet loss with tx_burst\n", __func__);
                
                //printf("%s:Packet loss with tx_burst\n", __func__);
		do {
			rte_pktmbuf_free(outbuf->mbufs[nb_tx]);
		} while (++nb_tx < outbuf->count);
	}
        
        
        
	outbuf->count = 0;
}

static inline void
flush_all_ports(struct output_buffer *tx_buffers, uint8_t nb_ports)
{
	uint8_t outp;
	for (outp = 0; outp < nb_ports; outp++) {
		/* skip ports that are not enabled */
		if ((enabled_port_mask & (1 << outp)) == 0)
			continue;

		if (tx_buffers[outp].count == 0)
			continue;

		flush_one_port(&tx_buffers[outp], outp);
	}
}


/*static int
l2fwd_send_burst(struct output_buffer *outbuf, uint8_t outp)
{
		
	unsigned nb_tx = rte_eth_tx_burst(outp, 0, outbuf->mbufs, outbuf->count);
	app_stats.tx.tx_pkts += nb_tx;
	if (unlikely(nb_tx < outbuf->count)) {
		LOG_DEBUG(DISTRAPP, "%s:Packet loss with tx_burst\n", __func__);
		do {
			rte_pktmbuf_free(outbuf->mbufs[nb_tx]);
		} while (++nb_tx < outbuf->count);
	}
	outbuf->count = 0;
        
	return 0;
}*/

static void
print_stats(void)
{
	struct rte_eth_stats eth_stats;
	unsigned i;

	printf("\nRX thread stats:\n");
	printf(" - Received:    %"PRIu64"\n", app_stats.rx.rx_pkts);
	printf(" - Processed:   %"PRIu64"\n", app_stats.rx.returned_pkts);
	printf(" - Enqueued:    %"PRIu64"\n", app_stats.rx.enqueued_pkts);

	printf("\nTX thread stats:\n");
	printf(" - Dequeued:    %"PRIu64"\n", app_stats.tx.dequeue_pkts);
	printf(" - Transmitted: %"PRIu64"\n", app_stats.tx.tx_pkts);

	for (i = 0; i < rte_eth_dev_count(); i++) {
		rte_eth_stats_get(i, &eth_stats);
		printf("\nPort %u stats:\n", i);
		printf(" - Pkts in:   %"PRIu64"\n", eth_stats.ipackets);
		printf(" - Pkts out:  %"PRIu64"\n", eth_stats.opackets);
		printf(" - In Errs:   %"PRIu64"\n", eth_stats.ierrors);
		printf(" - Out Errs:  %"PRIu64"\n", eth_stats.oerrors);
		printf(" - Mbuf Errs: %"PRIu64"\n", eth_stats.rx_nombuf);
	}
}

static int
lcore_tx(struct lcore_params *p)
{
    
        struct rte_ring *in_r = p->r;
        uint8_t port_lcore = p->portid;
	
	const uint8_t nb_ports = rte_eth_dev_count();
	//unsigned lcore_id = rte_lcore_id();
        
        //if (lcore_id == 3)
            //return 0;
        
        const int socket_id = rte_socket_id();
                     
	uint8_t port;
        
        uint64_t prev_tsc, diff_tsc, cur_tsc, timer_tsc;       
        
        
        const uint64_t drain_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * BURST_TX_DRAIN_US;
        prev_tsc = 0;
        timer_tsc = 0;
        
	for (port = 0; port < nb_ports; port++) {
		/* skip ports that are not enabled */
		if ((enabled_port_mask & (1 << port)) == 0)
			continue;

		if (rte_eth_dev_socket_id(port) > 0 &&
				rte_eth_dev_socket_id(port) != socket_id)
			printf("WARNING, port %u is on remote NUMA node to "
					"TX thread.\n\tPerformance will not "
					"be optimal.\n", port);
	}

	printf("\nCore %u doing packet TX.\n", rte_lcore_id());
	while (!quit_signal) 
        {
                            
                cur_tsc = rte_rdtsc();

		/*
		 * TX burst queue drain
		 */
		diff_tsc = cur_tsc - prev_tsc;
		if (unlikely(diff_tsc > drain_tsc)) {

			/*for (portid = 0; portid < RTE_MAX_ETHPORTS; portid++) {
				if (tx_buffers[portid].count == 0)
					continue;
                                
				struct output_buffer *outbuf;
                                outbuf = &tx_buffers[portid];
                                l2fwd_send_burst(tx_buffers, (uint8_t) portid);
				
			}*/

			/* if timer is enabled */
			if (timer_period > 0) {

				/* advance the timer */
				timer_tsc += diff_tsc;

				/* if timer has reached its timeout */
				if (unlikely(timer_tsc >= (uint64_t) timer_period)) {
								
                                        print_stats();
                                        /* reset the timer */
                                        timer_tsc = 0;
					
				}
			}

			prev_tsc = cur_tsc;
		}
            
                
		for (port = 0; port < nb_ports; port++) 
                {
                    if (port != port_lcore)
                        continue;
                    
			/* skip ports that are not enabled */
			if ((enabled_port_mask & (1 << port)) == 0)
				continue;

			struct rte_mbuf *bufs[BURST_SIZE];
			const uint16_t nb_rx = rte_ring_dequeue_burst(in_r,
					(void *)bufs, BURST_SIZE);
			app_stats.tx.dequeue_pkts += nb_rx;

			/* if we get no traffic, flush anything we have */
			/*
                        if (unlikely(nb_rx == 0)) {
				flush_all_ports(tx_buffers, nb_ports);
				continue;
			}
                        */
                        
                        if (unlikely(nb_rx == 0))
                        {
                            uint8_t outp = l2fwd_dst_ports[port];;                            
                            if (tx_buffers[outp].count == 0)
                                continue;

                            flush_one_port(&tx_buffers[outp], outp);
                        }
                        
                        //printf("degueue %u packets\n", nb_rx);
			/* for traffic we receive, queue it up for transmit */
			uint16_t i;
			_mm_prefetch(bufs[0], 0);
			_mm_prefetch(bufs[1], 0);
			_mm_prefetch(bufs[2], 0);
			for (i = 0; i < nb_rx; i++) {
				struct output_buffer *outbuf;
				uint8_t outp;
				_mm_prefetch(bufs[i + 3], 0);
				
                                /*
				 * workers should update in_port to hold the
				 * output port value
				 */
				
                                outp = bufs[i]->port;
				/* skip ports that are not enabled */
				if ((enabled_port_mask & (1 << outp)) == 0)
					continue;

				outbuf = &tx_buffers[outp];
				outbuf->mbufs[outbuf->count++] = bufs[i];                               
				if (outbuf->count == BURST_SIZE) {
					flush_one_port(outbuf, outp);
                                        
                                }
			}
		}
	}
        
        printf("tx %u quits\n", p->portid);
        
	return 0;
}

static void
int_handler(int sig_num)
{
	printf("Exiting on signal %d\n", sig_num);
	/* set quit flag for rx thread to exit */
	quit_signal_rx = 1;
}

static inline struct rte_mbuf *
mcast_out_pkt(struct rte_mbuf *pkt)
{
	struct rte_mbuf *hdr;

	/* Create new mbuf for the header. */
	if (unlikely ((hdr = rte_pktmbuf_alloc(header_pool)) == NULL))
		return (NULL);

	/* If requested, then make a new clone packet. */
	if (unlikely ((pkt = rte_pktmbuf_clone(pkt, clone_pool)) == NULL)) {
		rte_pktmbuf_free(hdr);
		return (NULL);
	}

	/* prepend new header */
	hdr->next = pkt;

	/* update header's fields */
	hdr->pkt_len = (uint16_t)(hdr->data_len + pkt->pkt_len);
	hdr->nb_segs = (uint8_t)(pkt->nb_segs + 1);

	/* copy metadata from source packet*/
	hdr->port = pkt->port;
	hdr->vlan_tci = pkt->vlan_tci;
	hdr->tx_offload = pkt->tx_offload;
	hdr->hash = pkt->hash;

	hdr->ol_flags = pkt->ol_flags;

	__rte_mbuf_sanity_check(hdr, 1);
	return (hdr);
}


    
static int
lcore_worker(struct lcore_params *p)
{
	struct rte_distributor *d = p->d;
	const unsigned id = p->worker_id;        
        struct rte_ring *r = p->r;
                
	/*
	 * for single port, xor_val will be zero so we won't modify the output
	 * port, otherwise we send traffic from 0 to 1, 2 to 3, and vice versa
	 */
	//const unsigned xor_val = (rte_eth_dev_count() > 1);
	struct rte_mbuf *buf = NULL;
                        
        
        if (rte_lcore_id() == DB_LCORE) {
            
            char ip_port[100];
            
            sprintf(ip_port, "%s", "$$+2&127.0.0.1:10000&127.0.0.1:10001");
            
            printf("Core %u forwarded copied packet to db\n", rte_lcore_id());
            printf("ip_port: %s\n", ip_port);
            fflush(stdout);            
            configure();
            initClient();
            //updateWorkerList(ip_port, strlen(ip_port));

            while(!quit_signal) {
                struct rte_mbuf *bufs[BURST_SIZE];
                const uint16_t nb_rx = rte_ring_dequeue_burst(db_ring,
                                        (void *)bufs, BURST_SIZE);
                if (unlikely(nb_rx == 0)) {
                    continue;
                }

                //printf("locre %u forward %u packets to db\n", rte_lcore_id(), nb_rx);
                unsigned i; 
                for (i = 0; i < nb_rx; i ++) {
                    struct rte_mbuf *m = bufs[i];

                    struct ether_hdr *eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
                    //struct ipv4_hdr *ip_hdr = (struct ipv4_hdr *)(eth_hdr + 1);
                    //uint8_t ip_len = (ip_hdr->version_ihl & 0xf) * 4;                
                        
                    struct buffer_s* sendDataBuf = fillNetflowData(eth_hdr);
                    runClient(sendDataBuf);
                    
                    
                }
            }
        }

        
	printf("\nCore %u acting as worker serving port %u.\n", rte_lcore_id(), p->portid);
        fflush(stdout);
	while (!quit_signal) 
        {		
            buf = rte_distributor_get_pkt(d, id, buf);                
            //buf->port ^= xor_val;               
            buf->port = l2fwd_dst_ports[buf->port];
            
            struct rte_mbuf *m = buf;
            struct ether_hdr *eth_hdr;
            
            struct rte_mbuf *mc;
            struct ether_hdr *cl_eth_hdr;

            unsigned router_nb_mcast = 0;
            unsigned id_nb_mcast = 0;
            int32_t router_hash;
            int32_t id_hash[MAX_NB_MCAST];
            
            eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
                
            if (buf->ol_flags & (PKT_RX_IPV4_HDR)) {
                struct ipv4_hdr *ip_hdr;                
                ip_hdr = (struct ipv4_hdr *)(eth_hdr + 1);
                
                uint8_t ip_len = (ip_hdr->version_ihl & 0xf) * 4;
                
                //uint16_t total_len = rte_be_to_cpu_16(ip_hdr->total_length);
                //uint16_t id = rte_be_to_cpu_16(ip_hdr->packet_id);
                //uint16_t off = rte_be_to_cpu_16(ip_hdr->fragment_offset) & 0x3fff;
                //uint32_t ip_dst = rte_be_to_cpu_32(ip_hdr->dst_addr);                                
                uint32_t ip_src = rte_be_to_cpu_32(ip_hdr->src_addr);                   
                //printf("IP: %u.%u.%u.%u\n", (ip_dst >> 24) & 0xff, (ip_dst >> 16) & 0xff, (ip_dst >> 8) & 0xff, (ip_dst & 0xff));
                
                if (likely((uint8_t)ip_hdr->next_proto_id != 6)) {
                   if (likely((u_int)ip_hdr->next_proto_id == 17)) {
                                          
                       struct udp_hdr* u_hdr = (struct udp_hdr *)((u_char *)ip_hdr + ip_len);
                       uint16_t sport = rte_be_to_cpu_16(u_hdr->src_port);
                       uint16_t dport = rte_be_to_cpu_16(u_hdr->dst_port);
                       uint16_t udp_len = rte_be_to_cpu_16(u_hdr->dgram_len);
                       uint16_t data_len = udp_len - sizeof(struct udp_hdr);
                       uint16_t payload_shift = sizeof(struct udp_hdr);
                       
                       /*
                       printf("%u %u %u %u %u\n", 
                               sport, dport, udp_len, data_len, payload_shift);
                       */
                                              
                       if ((data_len > 0) 
                               && ((dport == 2055) 
                               || (dport == 2057) 
                               || (dport == 6343) 
                               || (sport == 6343) 
                               || (dport == 9999) 
                               || (dport == 3000) 
                               || (dport == 6000) 
                               || (dport == 9996) 
                               || (dport == 15003) 
                               || (sport == 35467) 
                               || (dport == 9500) 
                               || (sport == 55298) 
                               || (dport == 9000))) 
                        {                            
                            NetFlow5Record the5Record;                                                               
                            struct generic_netflow_record record;

                            /*
                            flow_ver5_hdr* myRecord = (flow_ver5_hdr *)((u_char *)uh + payload_shift);
                            u_short version = ntohs(myRecord->version);
                            printf("version %u\n", version);
                            */

                            memcpy(&the5Record, (NetFlow5Record *)((u_char *)u_hdr + payload_shift), 
                                    data_len > sizeof(NetFlow5Record) ? sizeof(NetFlow5Record) : data_len);
                            uint16_t flowVersion = rte_be_to_cpu_16(the5Record.flowHeader.version);                                               
                            //printf("version %u\n", flowVersion);                                                
                            
                            if ((router_hash = rte_fbk_hash_lookup(mcast_hash, ip_src)) >= 0) {
                                         router_nb_mcast ++;
                                         //printf("%d %s %u\n", 
                                         //      router_hash, iptos(mcast_rule_table[router_hash].dest_ip), router_nb_mcast);
                            }
                                                        
                            
                            if (likely(flowVersion == 5))
                            {                                    
                                uint16_t i;                                    
                                uint16_t numFlows = rte_be_to_cpu_16(the5Record.flowHeader.count);                                                                                                
                                //uint32_t recordActTime = rte_be_to_cpu_32(the5Record.flowHeader.unix_secs);
                                //uint32_t recordSysUpTime = rte_be_to_cpu_32(the5Record.flowHeader.sysUptime);
                                //printf("num flows: %u record act time: %u record sys up time: %u\n", numFlows, recordActTime, recordSysUpTime);                                 				    

                                memset(&record, 0, sizeof(record));
                                record.vlanId = (u_int16_t)-1;
                                record.ntop.nw_latency_sec = record.ntop.nw_latency_usec = rte_be_to_cpu_32(0);

                                /*
                                fprintf(form, "%hu %hu %u %u %u %u %hu %hu %hu ", the5Record.flowHeader.version, the5Record.flowHeader.count, 
                                        the5Record.flowHeader.sysUptime, the5Record.flowHeader.unix_secs, the5Record.flowHeader.unix_nsecs, 
                                        the5Record.flowHeader.flow_sequence, the5Record.flowHeader.engine_type, the5Record.flowHeader.engine_id, 
                                        the5Record.flowHeader.sampleRate);
                                */
                                
                                
                                for(i=0; i<numFlows; i++) 
                                {
                                    record.srcaddr.ipType.ipv4 = the5Record.flowRecord[i].srcaddr, record.srcaddr.ipVersion = 4;
                                    record.dstaddr.ipType.ipv4 = the5Record.flowRecord[i].dstaddr, record.dstaddr.ipVersion = 4;
                                    record.nexthop.ipType.ipv4  = the5Record.flowRecord[i].nexthop, record.nexthop.ipVersion = 4;
                                    record.input       = the5Record.flowRecord[i].input;
                                    record.output      = the5Record.flowRecord[i].output;
                                    record.sentPkts    = the5Record.flowRecord[i].dPkts;
                                    record.sentOctets  = the5Record.flowRecord[i].dOctets;
                                    record.first       = the5Record.flowRecord[i].first;
                                    record.last        = the5Record.flowRecord[i].last;
                                    record.tos         = the5Record.flowRecord[i].tos;
                                    record.srcport     = the5Record.flowRecord[i].srcport;
                                    record.dstport     = the5Record.flowRecord[i].dstport;
                                    record.tcp_flags   = the5Record.flowRecord[i].tcp_flags;
                                    record.proto       = the5Record.flowRecord[i].proto;
                                    record.dst_as      = htonl(ntohs(the5Record.flowRecord[i].dst_as));
                                    record.src_as      = htonl(ntohs(the5Record.flowRecord[i].src_as));
                                    record.dst_mask    = the5Record.flowRecord[i].dst_mask;
                                    record.src_mask    = the5Record.flowRecord[i].src_mask;
                                    record.engine_type = the5Record.flowHeader.engine_type;
                                    record.engine_id   = the5Record.flowHeader.engine_id;

                                    deEndianRecord(&record);

                                    //record.sentPkts   *= readOnlyGlobals.flowCollection.sampleRate;
                                    //record.sentOctets *= readOnlyGlobals.flowCollection.sampleRate;

                                    //handleGenericFlow(0 /* fake threadId */,
                                    //                 netflow_device_ip, recordActTime,
                                    //                  recordSysUpTime, &record);
                                    /*printf("src %u.%u.%u.%u:%hu dst %u.%u.%u.%u:%hu input %u output %u\n", 
                                            (record.srcaddr.ipType.ipv4 >> 24) & 0xff,
                                            (record.srcaddr.ipType.ipv4 >> 16) & 0xff,
                                            (record.srcaddr.ipType.ipv4 >> 8) & 0xff,
                                            (record.srcaddr.ipType.ipv4) & 0xff,
                                            record.srcport, 
                                            (record.dstaddr.ipType.ipv4 >> 24) & 0xff, 
                                            (record.dstaddr.ipType.ipv4 >> 16) & 0xff, 
                                            (record.dstaddr.ipType.ipv4 >> 8) & 0xff, 
                                            (record.dstaddr.ipType.ipv4) & 0xff, 
                                            record.dstport, 
                                            record.input,
                                            record.output);
                                    */
                                    /*
                                    fprintf(form, "%hu %hu %u %u %u %u %hu %hu %hu ", the5Record.flowHeader.version, the5Record.flowHeader.count, 
                                        the5Record.flowHeader.sysUptime, the5Record.flowHeader.unix_secs, the5Record.flowHeader.unix_nsecs, 
                                        the5Record.flowHeader.flow_sequence, the5Record.flowHeader.engine_type, the5Record.flowHeader.engine_id, 
                                        the5Record.flowHeader.sampleRate);

                                    fprintf(form, "{%s %s %s %hu %hu %u %u %u %u %hu %hu %hu %hu %hu %hu %hu %hu %hu %hu %hu}\n", 
                                            iptos(record.srcaddr.ipType.ipv4), iptos(record.dstaddr.ipType.ipv4), iptos(record.nexthop.ipType.ipv4), 
                                            record.input, record.output, record.sentPkts, record.sentOctets, record.first, record.last, 
                                            record.srcport, record.dstport, the5Record.flowRecord[i].pad1, record.tcp_flags, record.proto, 
                                            the5Record.flowRecord[i].tos, record.src_as, record.dst_as, record.src_mask, record.dst_mask,
                                            the5Record.flowRecord[i].pad2);

                                    fflush(form);
                                    */
                                    
                                    int32_t hash;
                                    if ((hash = rte_fbk_hash_lookup(mcast_hash, ((ip_src & 0xffffffff) << 16) | 
                                            (record.input & 0xffff))) >= 0) {
                                         
                                        id_hash[id_nb_mcast] = hash;
                                        id_nb_mcast ++;
                                         
                                        //printf("%d %s %u\n", hash, iptos(mcast_rule_table[hash].dest_ip), id_nb_mcast);
                                    }
                                    else {
                                        //printf("failed to hash to value %d\n", hash);
                                    }
                                }                                                                                                                                                                                                                                                                       
                            }
                            else if (flowVersion == 9) {
                                printf("Netflow Version Number: %hu\n", flowVersion);
                            } //end of processing netflow                                                           
                            
                            
                            unsigned i;
                            //uint32_t nxt_hop;                            
                            struct ipv4_hdr *cl_ip_hdr;
                            router_nb_mcast = 10;
                            
                            for (i = 0; i < router_nb_mcast; i ++) {
                                if (unlikely((mc = rte_pktmbuf_alloc(cpy_pool)) != NULL)) {
                                    //rte_pktmbuf_init(cpy_pool, NULL, mc, 0);
                                    mc->port = m->port;
                                    mc->ol_flags = m->ol_flags;
                                    
                                    rte_memcpy(rte_pktmbuf_mtod(mc, void *), rte_pktmbuf_mtod(m, void *), m->data_len);
                                    mc->pkt_len = m->pkt_len;
                                    mc->data_len = m->data_len;
                                    
                                    struct ether_hdr *cpy_eth_hdr = rte_pktmbuf_mtod(mc, struct ether_hdr *);
                                    //printf("copied type: %u\n", cpy_eth_hdr->ether_type);
                                    //fflush(stdout);
                                    
                                    
                                    struct ipv4_hdr *cpy_ip_hdr = (struct ipv4_hdr *)(cpy_eth_hdr + 1);
                                    uint16_t cpy_ip_len = (cpy_ip_hdr->version_ihl & 0xf) * 4;                                    
                                    //cpy_ip_hdr->src_addr = rte_cpu_to_be_32(mcast_rule_table[router_hash].dest_ip);
                                    //cpy_ip_hdr->hdr_checksum = 0;
                                    //cpy_ip_hdr->hdr_checksum = rte_ipv4_cksum(cpy_ip_hdr);
                                                                        
                                    struct udp_hdr* cpy_u_hdr = (struct udp_hdr *)((u_char *)cpy_ip_hdr + cpy_ip_len);                                    
                                  
                                    //uint16_t sport = rte_be_to_cpu_16(cpy_u_hdr->src_port);
                                    //uint16_t dport = rte_be_to_cpu_16(cpy_u_hdr->dst_port);
                                    //uint16_t udp_len = rte_be_to_cpu_16(cpy_u_hdr->dgram_len);
                                    //uint16_t data_len = udp_len - sizeof(struct udp_hdr);
                                    uint16_t payload_shift = sizeof(struct udp_hdr);
                                    
                                    NetFlow5Record *cpy_5Record = (NetFlow5Record *)((u_char *)cpy_u_hdr + payload_shift);
                                    //uint16_t numFlows = rte_be_to_cpu_16(cpy_5Record->flowHeader.count);                                                                          
                                    uint16_t nb_flows = 20;
                                    cpy_5Record->flowHeader.count = htons(nb_flows);
                                    
                                    cpy_u_hdr->dgram_cksum = 0;
                                    cpy_ip_hdr->hdr_checksum = 0;
                                    cpy_u_hdr->dgram_cksum = rte_ipv4_udptcp_cksum(cpy_ip_hdr, cpy_u_hdr);
                                    
                                    cpy_ip_hdr->hdr_checksum = 0;
                                    cpy_ip_hdr->hdr_checksum = rte_ipv4_cksum(cpy_ip_hdr);
                                    
                                    //printf("num of flows: %u\n", rte_be_to_cpu_16(cpy_5Record->flowHeader.count));                                    
                                    
                                    uint16_t sent = rte_ring_enqueue(r, mc);
                                    app_stats.rx.enqueued_pkts += sent;
                                    
                                  //  rte_pktmbuf_free(mc);
                                }
                                else {
                                    printf("failled to allocate memmory\n");
                                }
                                                                                
                            }
                            
			    router_nb_mcast = 0;

                            for (i = 0; i < router_nb_mcast; i ++) {
                                if (unlikely((mc = rte_pktmbuf_clone(buf, clone_pool)) != NULL)) {
                                
                                    cl_eth_hdr = rte_pktmbuf_mtod(mc, struct ether_hdr *);
                                                                        
                                    cl_ip_hdr = (struct ipv4_hdr *)(cl_eth_hdr + 1);                
                                    cl_ip_hdr->src_addr = rte_cpu_to_be_32(mcast_rule_table[router_hash].dest_ip);
                                    cl_ip_hdr->hdr_checksum = 0;
                                    cl_ip_hdr->hdr_checksum = rte_ipv4_cksum(cl_ip_hdr);
                                    
                                    iptos(mcast_rule_table[router_hash].dest_ip);
                                    //nxt_hop = rte_be_to_cpu_32(cl_ip_hdr->src_addr);                                    
                                    //printf("next hop: %s\n", iptos(mcast_rule_table[router_hash].dest_ip));                                                                        
                                    //fflush(stdout);
                                    
                                    //uint8_t cl_ip_len = (cl_ip_hdr->version_ihl & 0xf) * 4;                
                                    //uint16_t total_len = rte_be_to_cpu_16(ip_hdr->total_length);
                                    //uint16_t id = rte_be_to_cpu_16(ip_hdr->packet_id);
                                    //uint16_t off = rte_be_to_cpu_16(ip_hdr->fragment_offset) & 0x3fff;
                                    //uint32_t cl_ip_dst = rte_be_to_cpu_32(cl_ip_hdr->dst_addr);                                
                                    //uint32_t ip_src = rte_be_to_cpu_32(ip_hdr->src_addr);                   
                                    //printf("IP: %u.%u.%u.%u %u\n", (cl_ip_dst >> 24) & 0xff, (cl_ip_dst >> 16) & 0xff, (cl_ip_dst >> 8) & 0xff, (cl_ip_dst & 0xff), cl_ip_len);
                                    //fflush(stdout);

                                    uint16_t sent = rte_ring_enqueue(r, mc);
                                    app_stats.rx.enqueued_pkts += sent;
                                    
                                    //rte_pktmbuf_free(mc);
                                }
                                else {

                                    printf("failed to allocate buffer\n");

                                } 
                            }
                            
                            for (i = 0; i < id_nb_mcast; i ++) {
                                if (unlikely((mc = rte_pktmbuf_clone(buf, clone_pool)) != NULL)) {
                                
                                    cl_eth_hdr = rte_pktmbuf_mtod(mc, struct ether_hdr *);
                                                                
                                    //struct ipv4_hdr *cl_ip_hdr = (struct ipv4_hdr *)(cl_eth_hdr + 1);                
                                    //uint8_t cl_ip_len = (cl_ip_hdr->version_ihl & 0xf) * 4;                
                                    //uint16_t total_len = rte_be_to_cpu_16(ip_hdr->total_length);
                                    //uint16_t id = rte_be_to_cpu_16(ip_hdr->packet_id);
                                    //uint16_t off = rte_be_to_cpu_16(ip_hdr->fragment_offset) & 0x3fff;
                                    //uint32_t cl_ip_dst = rte_be_to_cpu_32(cl_ip_hdr->dst_addr);                                
                                    //uint32_t ip_src = rte_be_to_cpu_32(ip_hdr->src_addr);                   
                                    //printf("IP: %u.%u.%u.%u %u\n", (cl_ip_dst >> 24) & 0xff, (cl_ip_dst >> 16) & 0xff, (cl_ip_dst >> 8) & 0xff, (cl_ip_dst & 0xff), cl_ip_len);
                                    //fflush(stdout);

                                    uint16_t sent = rte_ring_enqueue(r, mc);
                                    app_stats.rx.enqueued_pkts += sent;
                                    
                                    rte_pktmbuf_free(mc);
                                }
                                else {

                                    printf("failed to allocate buffer\n");

                                } 
                            }
                       }                          
                   }
                }                
            }                                                                                
        }
        
        printf("work %u quits\n", id);
	return 0;
}

static int
launch_one_lcore(__attribute__((unused)) void *dummy)
{
    struct lcore_queue_conf *conf;
   
    unsigned lcore_id, i;
    uint8_t port;
        
    lcore_id = rte_lcore_id();
    conf = &lcore_queue_conf[lcore_id];
    
              
    if (conf->n_rx_port == 0 && conf->n_tx_port == 0)
    {   
               
        /*
        if (locre_id == DB_CORE) {
            
            struct lcore_params *p = rte_malloc(NULL, sizeof(*p), 0);
            if (!p)
                rte_panic("malloc failure\n");                           
        
            *p = (struct lcore_params){lcore_id, NULL, NULL, mbuf_pool, DB_CORE};
            locre_fowared_db(p);            
        }
        */
                            
        struct lcore_params *p = rte_malloc(NULL, sizeof(*p), 0);
        if (!p)
                rte_panic("malloc failure\n");                           
        
        *p = (struct lcore_params){lcore_id, d[dist], output_ring[dist], mbuf_pool, dist};
        
        if (++dist == rte_eth_dev_count())
            dist = 0;
        
        printf("lcore id %u for worker\n", lcore_id);
        lcore_worker(p);
                       
    }
    else if (conf->n_rx_port)
    {        
        for (i = 0; i < conf->n_rx_port; i ++)
        {
            port = conf->rx_port_list[i];            
            struct lcore_params *p = rte_malloc(NULL, sizeof(*p), 0);
            if (!p)
                rte_panic("malloc failure\n");
            *p = (struct lcore_params){lcore_id, d[port], output_ring[port], mbuf_pool, port};
            
            printf("lcore id %u for rx port %u\n", lcore_id, port);
            lcore_rx(p);                       
            
        }        
    }
    else if (conf->n_tx_port)
    {
        for (i = 0; i < conf->n_tx_port; i ++)
        {
            port = conf->tx_port_list[i];
            struct lcore_params *p = rte_malloc(NULL, sizeof(*p), 0);
            if (!p)
                rte_panic("malloc failure\n");
            *p = (struct lcore_params){lcore_id, NULL, output_ring[port], mbuf_pool, port};
                        
            printf("lcore id %u for tx port %u\n", lcore_id, port);
            lcore_tx(p);
            
         
        }
            
    }
    
    return 0;
}

/* display usage */
static void
print_usage(const char *prgname)
{
	printf("%s [EAL options] -- -p PORTMASK\n"
			"  -p PORTMASK: hexadecimal bitmask of ports to configure\n",
			prgname);
}


static int
parse_portmask(const char *portmask)
{
	char *end = NULL;
	unsigned long pm;

	/* parse hexadecimal string */
	pm = strtoul(portmask, &end, 16);
	if ((portmask[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	if (pm == 0)
		return -1;

	return pm;
}

/* Parse the argument given in the command line of the application */
static int
parse_args(int argc, char **argv)
{
	int opt;
	char **argvopt;
	int option_index;
	char *prgname = argv[0];
	static struct option lgopts[] = {
		{NULL, 0, 0, 0}
	};

	argvopt = argv;

	while ((opt = getopt_long(argc, argvopt, "p:",
			lgopts, &option_index)) != EOF) {

		switch (opt) {
		/* portmask */
		case 'p':
			enabled_port_mask = parse_portmask(optarg);
			if (enabled_port_mask == 0) {
				printf("invalid portmask\n");
				print_usage(prgname);
				return -1;
			}
			break;

		default:
			print_usage(prgname);
			return -1;
		}
	}

	if (optind <= 1) {
		print_usage(prgname);
		return -1;
	}

	argv[optind-1] = prgname;

	optind = 0; /* reset getopt lib */
	return 0;
}

/* Main function, does initialization and calls the per-lcore functions */
int
main(int argc, char *argv[])
{
	//struct rte_mempool *mbuf_pool;
	//struct rte_distributor *d;
	//struct rte_ring *output_ring;
	unsigned lcore_id;// worker_id = 0;
	unsigned nb_ports;
	uint8_t portid;
	uint8_t nb_ports_available;

	/* catch ctrl-c so we can print on exit */
	signal(SIGINT, int_handler);

	/* init EAL */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
	argc -= ret;
	argv += ret;

	/* parse application arguments (after the EAL ones) */
	ret = parse_args(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid distributor parameters\n");

	if (rte_lcore_count() < 3)
		rte_exit(EXIT_FAILURE, "Error, This application needs at "
				"least 3 logical cores to run:\n"
				"1 lcore for packet RX and distribution\n"
				"1 lcore for packet TX\n"
				"and at least 1 lcore for worker threads\n");

	nb_ports = rte_eth_dev_count();
        
	if (nb_ports == 0)
		rte_exit(EXIT_FAILURE, "Error: no ethernet ports detected\n");
	if (nb_ports != 1 && (nb_ports & 1))
		rte_exit(EXIT_FAILURE, "Error: number of ports must be even, except "
				"when using a single port\n");

	mbuf_pool = rte_mempool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
			MBUF_SIZE, MBUF_CACHE_SIZE,
			sizeof(struct rte_pktmbuf_pool_private),
			rte_pktmbuf_pool_init, NULL,
			rte_pktmbuf_init, NULL,
			rte_socket_id(), 0);
	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
        
        cpy_pool = rte_mempool_create("CPY_POOL", NUM_MBUFS * nb_ports,
			MBUF_SIZE, MBUF_CACHE_SIZE,
			sizeof(struct rte_pktmbuf_pool_private),
			rte_pktmbuf_pool_init, NULL,
			rte_pktmbuf_init, NULL,
			rte_socket_id(), 0);
        
	if (cpy_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
        
        
        header_pool = rte_mempool_create("header_pool", NB_HDR_MBUF,
	    HDR_MBUF_SIZE, 32, 0, NULL, NULL, rte_pktmbuf_init, NULL,
	    rte_socket_id(), 0);

	if (header_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot init header mbuf pool\n");

	clone_pool = rte_mempool_create("clone_pool", NB_CLONE_MBUF,
	    CLONE_MBUF_SIZE, 32, 0, NULL, NULL, rte_pktmbuf_init, NULL,
	    rte_socket_id(), 0);

	if (clone_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot init clone mbuf pool\n");
                
	nb_ports_available = nb_ports;

	/* initialize all ports */
	for (portid = 0; portid < nb_ports; portid++) {
		/* skip ports that are not enabled */
		if ((enabled_port_mask & (1 << portid)) == 0) {
			printf("\nSkipping disabled port %d\n", portid);
			nb_ports_available--;
			continue;
		}
		/* init port */
		printf("Initializing port %u... done\n", (unsigned) portid);

		if (port_init(portid, mbuf_pool) != 0)
			rte_exit(EXIT_FAILURE, "Cannot initialize port %"PRIu8"\n",
					portid);
                
               
	}
        
        unsigned nb_ports_in_mask = 0;
        uint8_t last_port = 0;
        struct rte_eth_dev_info dev_info;
        struct lcore_queue_conf *qconf;
        unsigned rx_lcore_id, tx_lcore_id;
        rx_lcore_id = 0;
        qconf = NULL;
        
        for (portid = 0; portid < nb_ports; portid++) 
        {
           /* skip ports that are not enabled */
            if ((enabled_port_mask & (1 << portid)) == 0)
                    continue;

            if (nb_ports_in_mask % 2) {
                    l2fwd_dst_ports[portid] = last_port;
                    l2fwd_dst_ports[last_port] = portid;
            }
            else
                    last_port = portid;

            nb_ports_in_mask++;

            rte_eth_dev_info_get(portid, &dev_info);
        }
        
        for (portid = 0; portid < nb_ports; portid ++)
        {
            /* skip ports that are not enabled */
            if ((enabled_port_mask & (1 << portid)) == 0)
                    continue;
            
            while (rte_lcore_is_enabled(rx_lcore_id) == 0 ||
		       lcore_queue_conf[rx_lcore_id].n_rx_port ==
		       l2fwd_rx_queue_per_lcore) {
			rx_lcore_id++;
			if (rx_lcore_id >= RTE_MAX_LCORE)
				rte_exit(EXIT_FAILURE, "Not enough cores\n");
		}

            if (qconf != &lcore_queue_conf[rx_lcore_id])
                    /* Assigned a new logical core in the loop above. */
                    qconf = &lcore_queue_conf[rx_lcore_id];

            qconf->rx_port_list[qconf->n_rx_port] = portid;
            qconf->n_rx_port++;
            printf("Lcore %u: RX port %u\n", rx_lcore_id, (unsigned) portid);
        }
        
        tx_lcore_id = rx_lcore_id + 1;
        
        for (portid = 0; portid < nb_ports; portid ++)
        {
            /* skip ports that are not enabled */
            if ((enabled_port_mask & (1 << portid)) == 0)
                    continue;
            
            while (rte_lcore_is_enabled(tx_lcore_id) == 0 ||
		       lcore_queue_conf[tx_lcore_id].n_tx_port ==
		       l2fwd_rx_queue_per_lcore) {
			tx_lcore_id++;
			if (tx_lcore_id >= RTE_MAX_LCORE)
				rte_exit(EXIT_FAILURE, "Not enough cores\n");
		}

            if (qconf != &lcore_queue_conf[tx_lcore_id])
                    /* Assigned a new logical core in the loop above. */
                    qconf = &lcore_queue_conf[tx_lcore_id];

            qconf->tx_port_list[qconf->n_tx_port] = portid;
            qconf->n_tx_port++;
            printf("Lcore %u: TX port %u\n", tx_lcore_id, (unsigned) portid); 
        }
        
        
        if (nb_ports_in_mask % 2) {
		printf("Notice: odd number of ports in portmask.\n");
		l2fwd_dst_ports[last_port] = last_port;
	}
        
	if (!nb_ports_available) {
		rte_exit(EXIT_FAILURE,
				"All available ports are disabled. Please set portmask.\n");
	}
        
        for (portid = 0; portid < nb_ports; portid ++)
        {
            char name[32];
            snprintf(name, sizeof(name), "PKT_DIST_%u", portid);
            d[portid] = rte_distributor_create(name, rte_socket_id(),
			(rte_lcore_count() - nb_ports*2)/2);
            
            if (d[portid] == NULL)
                rte_exit(EXIT_FAILURE, "Cannot create distributor\n");
        }
        
        /*
	d = rte_distributor_create("PKT_DIST", rte_socket_id(),
			rte_lcore_count() - 4);
	if (d == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create distributor\n");
        */
	/*
	 * scheduler ring is read only by the transmitter core, but written to
	 * by multiple threads
	 */
        
        for (portid = 0; portid < nb_ports; portid ++)
        {
            char name[32];
            snprintf(name, sizeof(name), "Output_ring_%u", portid);
            output_ring[portid] = rte_ring_create(name, RTE_RING_SZ,
			rte_socket_id(), RING_F_SC_DEQ);
            if (output_ring[portid] == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create output ring\n");
            
            rte_ring_dump(stdout, output_ring[portid]);
        }
        
        db_ring = rte_ring_create("db_ring", RTE_RING_SZ, rte_socket_id(), RING_F_SC_DEQ);
        if (db_ring == NULL)
            rte_exit(EXIT_FAILURE, "Cannot create output ring\n");
        /*
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (worker_id == rte_lcore_count() - 2)
			rte_eal_remote_launch((lcore_function_t *)lcore_tx,
					output_ring, lcore_id);
		else {
			struct lcore_params *p =
					rte_malloc(NULL, sizeof(*p), 0);
			if (!p)
				rte_panic("malloc failure\n");
			*p = (struct lcore_params){worker_id, d, output_ring, mbuf_pool};

			rte_eal_remote_launch((lcore_function_t *)lcore_worker,
					p, lcore_id);
		}
		worker_id++;
	}
        */
	/* call lcore_main on master core only */
	//struct lcore_params p = { 0, d, output_ring, mbuf_pool, 0};
	//lcore_rx(&p);
                        
        /* initialize the multicast hash */
	int retval = init_mcast_hash();
	if (retval != 0)
		rte_exit(EXIT_FAILURE, "Cannot build the multicast hash\n");
        
        
        rte_eal_mp_remote_launch(launch_one_lcore, NULL, CALL_MASTER);
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (rte_eal_wait_lcore(lcore_id) < 0)
			return -1;
	}

	print_stats();
	return 0;
}
