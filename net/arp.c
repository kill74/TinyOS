/* arp.c — Address Resolution Protocol */

#include "../net/arp.h"
#include "../net/ethernet.h"
#include "../net/ip.h"
#include "../include/log.h"
#include "../include/timer.h"
#include <stdint.h>
#include <stddef.h>

/* ── ARP packet layout (follows Ethernet header) ─────────────────────────── */
typedef struct arp_packet {
    uint16_t htype;         /* Hardware type (1 = Ethernet)    */
    uint16_t ptype;         /* Protocol type (0x0800 = IPv4)   */
    uint8_t  hlen;          /* Hardware address length (6)     */
    uint8_t  plen;          /* Protocol address length (4)     */
    uint16_t opcode;        /* Operation                       */
    eth_addr_t sender_mac;
    uint32_t sender_ip;
    eth_addr_t target_mac;
    uint32_t target_ip;
} __attribute__((packed)) arp_packet_t;

/* ── Module state ────────────────────────────────────────────────────────── */
static arp_entry_t arp_table[ARP_TABLE_SIZE];

/* ── Internal helpers ────────────────────────────────────────────────────── */

static int arp_find(uint32_t ip)
{
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && arp_table[i].ip == ip)
            return i;
    }
    return -1;
}

static int arp_free_slot(void)
{
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!arp_table[i].valid) return i;
    }
    /* Evict oldest entry */
    uint32_t oldest = 0xFFFFFFFF;
    int idx = 0;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].timestamp < oldest) {
            oldest = arp_table[i].timestamp;
            idx = i;
        }
    }
    return idx;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void arp_init(void)
{
    for (int i = 0; i < ARP_TABLE_SIZE; i++)
        arp_table[i].valid = 0;
    LOG_INFO("ARP: table initialized (%d entries)", ARP_TABLE_SIZE);
}

void arp_rx(packet_buf_t *pkt)
{
    if (pkt->len < sizeof(arp_packet_t)) return;

    arp_packet_t *arp = (arp_packet_t *)packet_data(pkt);

    /* We only handle Ethernet + IPv4 ARP */
    if (arp->htype != ARP_HTYPE_ETH || arp->ptype != ETH_TYPE_IPV4)
        return;
    if (arp->hlen != ETH_ALEN || arp->plen != 4)
        return;

    uint16_t op = arp->opcode;

    /* Always learn the sender's MAC→IP mapping */
    arp_insert(arp->sender_ip, arp->sender_mac);

    if (op == ARP_OP_REQUEST) {
        /* Someone is asking: "Who has our IP?" */
        if (arp->target_ip == local_ip) {
            LOG_DEBUG("ARP: replying to request from %x", arp->sender_ip);

            /* Build ARP reply — reuse the same packet buffer */
            arp->opcode = ARP_OP_REPLY;
            arp->target_mac = arp->sender_mac;
            arp->target_ip  = arp->sender_ip;
            arp->sender_mac = *ethernet_get_mac();
            arp->sender_ip  = local_ip;

            /* Wrap back to Ethernet layer for transmission */
            packet_reset(pkt);
            pkt->len = sizeof(arp_packet_t);
            ethernet_tx(pkt, arp->target_mac, ETH_TYPE_ARP);
        }
    }
    /* For ARP replies, we already learned the mapping above */
}

const eth_addr_t *arp_lookup(uint32_t ip)
{
    int idx = arp_find(ip);
    if (idx >= 0)
        return &arp_table[idx].mac;
    return NULL;
}

int arp_request(uint32_t target_ip)
{
    LOG_DEBUG("ARP: requesting %x", target_ip);

    packet_buf_t *pkt = packet_alloc();
    if (!pkt) return -1;

    arp_packet_t *arp = (arp_packet_t *)pkt->data;
    arp->htype = ARP_HTYPE_ETH;
    arp->ptype = ETH_TYPE_IPV4;
    arp->hlen  = ETH_ALEN;
    arp->plen  = 4;
    arp->opcode = ARP_OP_REQUEST;
    arp->sender_mac = *ethernet_get_mac();
    arp->sender_ip  = local_ip;
    arp->target_mac = (eth_addr_t){ { 0, 0, 0, 0, 0, 0 } };
    arp->target_ip  = target_ip;

    pkt->len = sizeof(arp_packet_t);

    int rc = ethernet_tx(pkt, eth_broadcast, ETH_TYPE_ARP);
    packet_free(pkt);
    return rc;
}

void arp_insert(uint32_t ip, eth_addr_t mac)
{
    int idx = arp_find(ip);
    if (idx < 0)
        idx = arp_free_slot();

    arp_table[idx].ip = ip;
    arp_table[idx].mac = mac;
    arp_table[idx].valid = 1;
    arp_table[idx].timestamp = timer_get_ticks();
}

void arp_tick(void)
{
    uint32_t now = timer_get_ticks();
    /* Age entries older than 30 seconds (3000 ticks at 100 Hz) */
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid &&
            (now - arp_table[i].timestamp) > 3000) {
            arp_table[i].valid = 0;
        }
    }
}
