/* ethernet.h — Ethernet frame handling */
#pragma once
#include <stdint.h>
#include "packet.h"

/* Ethernet constants */
#define ETH_ALEN        6
#define ETH_HEADER_LEN  14
#define ETH_MTU         1500
#define ETH_CRC_LEN     4

/* EtherTypes (network byte order) */
#define ETH_TYPE_IPV4   0x0008  /* 0x0800 on wire */
#define ETH_TYPE_ARP    0x0608  /* 0x0806 on wire */

/* Ethernet address */
typedef struct eth_addr {
    uint8_t bytes[ETH_ALEN];
} eth_addr_t;

/* Ethernet frame header (14 bytes, packed on wire) */
typedef struct eth_hdr {
    eth_addr_t dst;
    eth_addr_t src;
    uint16_t   ethertype;   /* in little-endian host order for struct */
} __attribute__((packed)) eth_hdr_t;

/* Broadcast address */
extern const eth_addr_t eth_broadcast;

/* Check two MAC addresses for equality */
static inline int eth_addr_eq(const eth_addr_t *a, const eth_addr_t *b)
{
    for (int i = 0; i < ETH_ALEN; i++)
        if (a->bytes[i] != b->bytes[i]) return 0;
    return 1;
}

/* Initialise the Ethernet layer (set local MAC) */
void ethernet_init(eth_addr_t local_mac);

/* Get our local MAC address */
const eth_addr_t *ethernet_get_mac(void);

/* Handle an incoming Ethernet frame */
void ethernet_rx(packet_buf_t *pkt);

/* Build and send an Ethernet frame (prepends header, calls NIC tx) */
int ethernet_tx(packet_buf_t *pkt, eth_addr_t dst, uint16_t ethertype);
