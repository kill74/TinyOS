/* arp.h — Address Resolution Protocol */
#pragma once
#include <stdint.h>
#include "ethernet.h"
#include "packet.h"

/* ARP opcodes (network byte order) */
#define ARP_OP_REQUEST  0x0100
#define ARP_OP_REPLY    0x0200

/* Hardware / protocol types */
#define ARP_HTYPE_ETH   0x0100  /* Ethernet (1 in LE) */
#define ARP_PTYPE_IPV4  0x0008  /* IPv4  (0x0800 in LE) */

/* ARP table size */
#define ARP_TABLE_SIZE  16

/* ARP table entry */
typedef struct arp_entry {
    uint32_t  ip;       /* IPv4 address (network byte order) */
    eth_addr_t mac;
    uint8_t   valid;
    uint32_t  timestamp; /* tick when this entry was added/updated */
} arp_entry_t;

/* Initialise the ARP subsystem */
void arp_init(void);

/* Handle an incoming ARP packet (raw payload, no Ethernet header) */
void arp_rx(packet_buf_t *pkt);

/* Look up MAC for a given IPv4 address. Returns NULL if not found. */
const eth_addr_t *arp_lookup(uint32_t ip);

/* Send an ARP request to resolve `target_ip` */
int arp_request(uint32_t target_ip);

/* Add/update an entry manually (used on receiving replies) */
void arp_insert(uint32_t ip, eth_addr_t mac);

/* Periodic tick — age out stale entries */
void arp_tick(void);
