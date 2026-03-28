/* ip.h — IPv4 protocol handling */
#pragma once
#include <stdint.h>
#include "packet.h"

/* IP constants */
#define IP_VERSION      4
#define IP_HEADER_LEN   20
#define IP_TTL_DEFAULT  64

/* IP protocol numbers */
#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP   17

/* IPv4 header (20 bytes without options) */
typedef struct ip_hdr {
    uint8_t  ver_ihl;       /* Version (4 bits) + IHL (4 bits)          */
    uint8_t  tos;           /* Type of Service                           */
    uint16_t total_len;     /* Total length (header + payload)           */
    uint16_t id;            /* Identification                            */
    uint16_t flags_frag;    /* Flags (3 bits) + Fragment offset (13 bits)*/
    uint8_t  ttl;           /* Time To Live                              */
    uint8_t  protocol;      /* Protocol (TCP=6, UDP=17, ICMP=1)          */
    uint16_t checksum;      /* Header checksum                           */
    uint32_t src;           /* Source IP (network byte order)            */
    uint32_t dst;           /* Dest IP (network byte order)              */
} __attribute__((packed)) ip_hdr_t;

/* Convenience: extract IHL in bytes */
#define IP_IHL(hdr) (((hdr)->ver_ihl & 0x0F) * 4)

/* Our local IP configuration (network byte order) */
extern uint32_t local_ip;
extern uint32_t local_netmask;
extern uint32_t local_gateway;

/* Set local IP configuration */
void ip_set_config(uint32_t ip, uint32_t netmask, uint32_t gateway);

/* Handle an incoming IP datagram (payload after Ethernet header) */
void ip_rx(packet_buf_t *pkt);

/* Build and send an IP datagram (prepends IP header) */
int ip_tx(packet_buf_t *pkt, uint32_t dst_ip, uint8_t protocol);

/* Compute IP-style checksum over `len` bytes */
uint16_t ip_checksum(const void *data, uint32_t len);

/* Convert dotted-decimal string to uint32_t (network byte order) */
uint32_t ip_from_bytes(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
