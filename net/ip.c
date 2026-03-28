/* ip.c — IPv4 protocol handling */

#include "../net/ip.h"
#include "../net/ethernet.h"
#include "../net/arp.h"
#include "../net/tcp.h"
#include "../net/udp.h"
#include "../include/log.h"
#include <stdint.h>

/* ── Module state ────────────────────────────────────────────────────────── */
uint32_t local_ip      = 0x0A00020F;  /* 10.0.2.15 (QEMU default) */
uint32_t local_netmask = 0xFFFFFF00;  /* 255.255.255.0 */
uint32_t local_gateway = 0x0A000202;  /* 10.0.2.2  (QEMU default) */

/* ── Internal helpers ────────────────────────────────────────────────────── */

static uint32_t ip_checksum_partial(const void *data, uint32_t len)
{
    const uint16_t *p = (const uint16_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len)
        sum += (uint16_t)(*(const uint8_t *)p);

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return sum;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void ip_set_config(uint32_t ip, uint32_t netmask, uint32_t gateway)
{
    local_ip = ip;
    local_netmask = netmask;
    local_gateway = gateway;
    LOG_INFO("IP: configured %x / mask %x / gw %x", ip, netmask, gateway);
}

uint16_t ip_checksum(const void *data, uint32_t len)
{
    return (uint16_t)~ip_checksum_partial(data, len);
}

uint32_t ip_from_bytes(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) |
           ((uint32_t)c << 8)  | (uint32_t)d;
}

void ip_rx(packet_buf_t *pkt)
{
    if (pkt->len < IP_HEADER_LEN) return;

    ip_hdr_t *ip = (ip_hdr_t *)packet_data(pkt);

    /* Validate version */
    if ((ip->ver_ihl >> 4) != IP_VERSION) return;

    uint32_t ihl = IP_IHL(ip);
    if (ihl < IP_HEADER_LEN || pkt->len < ihl) return;

    /* Validate total length fits in the packet */
    uint16_t total_len = ip->total_len;
    if (total_len < ihl || total_len > pkt->len) return;

    /* Trim packet to IP total length */
    pkt->len = pkt->offset + total_len;

    /* Filter: accept only our IP or broadcast */
    if (ip->dst != local_ip && ip->dst != 0xFFFFFFFF) return;

    /* Verify checksum (optional but good practice) */
    uint16_t saved_cksum = ip->checksum;
    ip->checksum = 0;
    uint16_t calc = ip_checksum(ip, ihl);
    ip->checksum = saved_cksum;
    if (calc != 0xFFFF) return;  /* bad checksum */

    /* Advance past header and dispatch to L4 */
    packet_advance(pkt, ihl);

    switch (ip->protocol) {
        case IP_PROTO_TCP:
            tcp_rx(pkt, ip->src, ip->dst);
            break;
        case IP_PROTO_UDP:
            udp_rx(pkt, ip->src, ip->dst);
            break;
        default:
            /* Unknown L4 protocol — drop */
            break;
    }
}

int ip_tx(packet_buf_t *pkt, uint32_t dst_ip, uint8_t protocol)
{
    if (!pkt) return -1;

    uint32_t payload_len = pkt->len;

    /* Prepend IP header */
    if (pkt->len + IP_HEADER_LEN > PACKET_BUF_SIZE) return -1;

    /* Shift payload right */
    for (int32_t i = (int32_t)pkt->len - 1; i >= 0; i--)
        pkt->data[i + IP_HEADER_LEN] = pkt->data[i];
    pkt->len += IP_HEADER_LEN;
    pkt->offset = 0;

    /* Fill IP header */
    ip_hdr_t *ip = (ip_hdr_t *)pkt->data;
    ip->ver_ihl    = (IP_VERSION << 4) | (IP_HEADER_LEN / 4);
    ip->tos        = 0;
    ip->total_len  = IP_HEADER_LEN + payload_len;
    ip->id         = 0;
    ip->flags_frag = 0;
    ip->ttl        = IP_TTL_DEFAULT;
    ip->protocol   = protocol;
    ip->checksum   = 0;
    ip->src        = local_ip;
    ip->dst        = dst_ip;
    ip->checksum   = ip_checksum(ip, IP_HEADER_LEN);

    /* Resolve next-hop MAC */
    uint32_t next_hop = dst_ip;
    if ((dst_ip & local_netmask) != (local_ip & local_netmask))
        next_hop = local_gateway;

    const eth_addr_t *mac = arp_lookup(next_hop);
    if (!mac) {
        /* ARP miss — send ARP request and drop this packet for now */
        arp_request(next_hop);
        return -1;
    }

    return ethernet_tx(pkt, *mac, ETH_TYPE_IPV4);
}
