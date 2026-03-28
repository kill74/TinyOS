/* udp.h — UDP protocol (minimal implementation) */
#pragma once
#include <stdint.h>
#include "packet.h"

/* UDP header (8 bytes) */
typedef struct udp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_hdr_t;

/* Max UDP sockets */
#define UDP_MAX_SOCKETS 8

/* Simple UDP receive buffer entry */
typedef struct udp_rx_entry {
    uint32_t src_ip;
    uint16_t src_port;
    uint8_t  data[1500];
    uint32_t len;
    uint8_t  valid;
} udp_rx_entry_t;

/* Initialise the UDP subsystem */
void udp_init(void);

/* Handle an incoming UDP datagram (IP payload) */
void udp_rx(packet_buf_t *pkt, uint32_t src_ip, uint32_t dst_ip);

/* Send a UDP datagram */
int udp_tx(uint32_t dst_ip, uint16_t dst_port,
           uint16_t src_port, const uint8_t *data, uint32_t len);
